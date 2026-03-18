// In-game ImGui overlay menu for Seamless Co-op
//
// Rendered via the DX11 Present hook in renderer.cpp.
// INSERT toggles the menu open/closed.
//
// States:
//   Main    — status + HOST / JOIN / LEAVE buttons
//   Host    — enter password, see your IP, start hosting
//   Join    — enter IP + password, connect
//
// No popup windows, no alt-tab, no file editing.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#include "imgui.h"
#include "../../include/ui.h"
#include "../../include/session.h"
#include "../../include/network.h"
#include "../../include/hooks.h"
#include "../../include/sync.h"
#include "../../include/utils.h"

#include <sstream>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

using namespace DS2Coop::Utils;
using namespace DS2Coop::Session;
using namespace DS2Coop::Network;

namespace DS2Coop::UI {

// ============================================================================
// Helper: copy text to clipboard
// ============================================================================
static void CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
    }
    CloseClipboard();
}

// ============================================================================
// Helper: fetch public IP (async, runs once in background)
// ============================================================================
static std::string g_publicIP;
static std::atomic<bool> g_publicIPFetched{false};
static std::atomic<bool> g_publicIPFetching{false};

static void FetchPublicIPThread() {
    // Use raw Winsock to hit a simple IP echo service
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("api.ipify.org", "80", &hints, &result) != 0) {
        WSACleanup();
        g_publicIPFetched = true;
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        WSACleanup();
        g_publicIPFetched = true;
        return;
    }

    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) != 0) {
        closesocket(sock);
        freeaddrinfo(result);
        WSACleanup();
        g_publicIPFetched = true;
        return;
    }
    freeaddrinfo(result);

    const char* request = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nConnection: close\r\n\r\n";
    send(sock, request, (int)strlen(request), 0);

    char buf[512] = {};
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = recv(sock, buf + total, sizeof(buf) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    closesocket(sock);
    WSACleanup(); // balance the WSAStartup at the top of this function

    // Parse IP from HTTP response body (last line after \r\n\r\n)
    char* body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        // Trim whitespace
        while (*body && (*body == ' ' || *body == '\r' || *body == '\n')) body++;
        char* end = body;
        while (*end && *end != '\r' && *end != '\n' && *end != ' ') end++;
        *end = '\0';
        if (strlen(body) >= 7 && strlen(body) <= 15) { // basic IP length check
            g_publicIP = body;
        }
    }
    g_publicIPFetched = true;
}

static void EnsurePublicIPFetched() {
    bool expected = false;
    if (g_publicIPFetching.compare_exchange_strong(expected, true)) {
        std::thread(FetchPublicIPThread).detach();
    }
}

// ============================================================================
// Helper: get local IP
// ============================================================================
static std::string GetLocalIP() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return "unknown";

    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) return "unknown";

    std::string hamachiIP, lanIP, other;
    for (auto* p = result; p; p = p->ai_next) {
        if (p->ai_family != AF_INET) continue;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, ip, sizeof(ip));
        std::string s(ip);
        if (s.substr(0,4) == "127.") continue;
        if (s.substr(0,3) == "25." || s.substr(0,2) == "5.") hamachiIP = s;
        else if (s.substr(0,8) == "192.168." || s.substr(0,3) == "10." || s.substr(0,4) == "172.")
            { if (lanIP.empty()) lanIP = s; }
        else if (other.empty()) other = s;
    }
    freeaddrinfo(result);
    if (!hamachiIP.empty()) return hamachiIP;
    if (!lanIP.empty())     return lanIP;
    if (!other.empty())     return other;
    return "unknown";
}

// ============================================================================
// Overlay singleton
// ============================================================================
Overlay& Overlay::GetInstance() {
    static Overlay instance;
    return instance;
}

void Overlay::Initialize() {
    if (m_initialized) return;
    // Actual ImGui init happens in renderer.cpp when Present is first called
    m_initialized = true;
    LOG_INFO("Overlay initialized");
}

void Overlay::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;
    m_notifications.clear();
}

void Overlay::ShowConnectionMenu() {
    m_visible = true;
    m_currentState = MenuState::Main;
}

void Overlay::ShowPlayerList() {
    m_visible = true;
    m_currentState = MenuState::PlayerList;
}

void Overlay::ShowNotification(const std::string& message, float duration) {
    Notification n;
    n.message = message;
    n.timeRemaining = duration;
    n.id = m_nextNotifId++;
    {
        std::lock_guard<std::mutex> lock(m_notifMutex);
        m_notifications.push_back(n);
    }
    LOG_INFO("Notification: %s", message.c_str());
}

// ============================================================================
// Main render — called every frame from HookedPresent
// ============================================================================
void Overlay::Render() {
    // Always render notifications (even when menu is closed)
    RenderNotifications();

    if (!m_visible) return;

    // Center the window on screen
    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f;
    float cy = io.DisplaySize.y * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always); // auto height

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoScrollbar;

    switch (m_currentState) {
        case MenuState::Main:       RenderMainMenu();  break;
        case MenuState::Host:       RenderHostMenu();  break;
        case MenuState::Join:       RenderJoinMenu();  break;
        case MenuState::PlayerList: RenderPlayerList(); break;
        default: break;
    }
}

// ============================================================================
// Main menu
// ============================================================================
void Overlay::RenderMainMenu() {
    auto& sessionMgr = SessionManager::GetInstance();
    bool inSession = sessionMgr.IsActive();

    const char* title = inSession ? "SEAMLESS CO-OP  [ACTIVE]###main" : "SEAMLESS CO-OP###main";
    if (!ImGui::Begin(title, &m_visible, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    if (inSession) {
        auto players = sessionMgr.GetPlayers();

        ImGui::TextDisabled("Session Active");
        ImGui::Separator();
        ImGui::Text("Players: %zu", players.size());

        for (const auto& p : players) {
            bool isLocal = (p.playerId == 0 ||
                            p.playerId == PeerManager::GetInstance().GetLocalPlayerId());
            ImGui::Text("  %s%s",
                        p.playerName.c_str(),
                        isLocal ? "  (you)" : "");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Seamless tools
        ImGui::TextDisabled("Tools");
        ImGui::Separator();

        if (ImGui::Button("Grant Soapstones", ImVec2(-1, 0))) {
            if (DS2Coop::Sync::PlayerSync::GetInstance().GrantSoapstones()) {
                ShowNotification("Soapstones added to inventory!", 4.0f);
            } else {
                ShowNotification("Could not grant items. Try again in-game.", 4.0f);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Leave Session", ImVec2(-1, 0))) {
            sessionMgr.LeaveSession();
            DS2Coop::Hooks::ProtobufHooks::SetSeamlessActive(false);
            m_visible = false;
            ShowNotification("Left session.", 3.0f);
        }

    } else {
        // Not in session
        ImGui::TextDisabled("Not in a session.");
        ImGui::Spacing();

        if (ImGui::Button("Host Session", ImVec2(-1, 0))) {
            m_currentState = MenuState::Host;
            m_inputPassword[0] = '\0';
            m_inputIP[0] = '\0';
        }

        ImGui::Spacing();

        if (ImGui::Button("Join Session", ImVec2(-1, 0))) {
            m_currentState = MenuState::Join;
            m_inputPassword[0] = '\0';
            m_inputIP[0] = '\0';
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("INSERT to close");

    ImGui::End();
}

// ============================================================================
// Host menu
// ============================================================================
void Overlay::RenderHostMenu() {
    if (!ImGui::Begin("HOST SESSION###main", &m_visible, ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    // Show IPs so they can share them
    static std::string cachedLocalIP;
    if (cachedLocalIP.empty()) cachedLocalIP = GetLocalIP();
    EnsurePublicIPFetched();

    // IP addresses hidden by default (streamer safety)
    static bool showIPs = false;

    if (!showIPs) {
        if (ImGui::Button("Show IP Addresses", ImVec2(-1, 0))) {
            showIPs = true;
        }
        ImGui::TextDisabled("IPs hidden (click to reveal)");
    } else {
        if (ImGui::Button("Hide IP Addresses", ImVec2(-1, 0))) {
            showIPs = false;
        }

        ImGui::TextDisabled("Share with friends:");

        // Public IP
        if (g_publicIPFetched && !g_publicIP.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.2f, 1.0f));
            ImGui::Text("  Public:  %s : 27015", g_publicIP.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##pub")) {
                CopyToClipboard(g_publicIP);
                ShowNotification("Public IP copied!", 2.0f);
            }
        } else if (g_publicIPFetched) {
            ImGui::TextDisabled("  Public:  (could not detect)");
        } else {
            ImGui::TextDisabled("  Public:  fetching...");
        }

        // LAN / Hamachi IP
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.2f, 1.0f));
        ImGui::Text("  LAN:     %s : 27015", cachedLocalIP.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy##lan")) {
            CopyToClipboard(cachedLocalIP);
            ShowNotification("LAN IP copied!", 2.0f);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Friends on the same network use LAN IP.");
    ImGui::TextDisabled("Friends online need Public IP + port 27015 forwarded.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Session password:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##password", m_inputPassword, sizeof(m_inputPassword));

    ImGui::Spacing();

    // Validate
    bool canHost = strlen(m_inputPassword) > 0;

    if (!canHost) {
        ImGui::TextDisabled("Enter a password to continue.");
        ImGui::Spacing();
    }

    ImGui::BeginDisabled(!canHost);
    if (ImGui::Button("Start Hosting", ImVec2(-1, 0))) {
        auto& sessionMgr = SessionManager::GetInstance();
        if (sessionMgr.CreateSession(m_inputPassword)) {
            // Seamless mode activates automatically when the first peer connects
            ShowNotification(std::string("Hosting! Waiting for players..."), 6.0f);
            m_currentState = MenuState::Main;
        } else {
            ShowNotification("Failed to create session. Check log.", 4.0f);
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(-1, 0))) {
        m_currentState = MenuState::Main;
    }

    ImGui::End();
}

// ============================================================================
// Join menu
// ============================================================================
void Overlay::RenderJoinMenu() {
    if (!ImGui::Begin("JOIN SESSION###main", &m_visible, ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Host's IP address:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ip", m_inputIP, sizeof(m_inputIP),
                     ImGuiInputTextFlags_CharsNoBlank);

    ImGui::Spacing();
    ImGui::TextDisabled("Session password:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##pw", m_inputPassword, sizeof(m_inputPassword));

    ImGui::Spacing();

    bool canJoin = strlen(m_inputIP) > 0 && strlen(m_inputPassword) > 0;

    if (!canJoin) {
        ImGui::TextDisabled("Fill in IP and password to continue.");
        ImGui::Spacing();
    }

    ImGui::BeginDisabled(!canJoin);
    if (ImGui::Button("Connect", ImVec2(-1, 0))) {
        auto& sessionMgr = SessionManager::GetInstance();
        if (sessionMgr.JoinSession(m_inputIP, m_inputPassword)) {
            // Seamless mode activates automatically when host confirms the handshake
            ShowNotification("Connecting... waiting for host response.", 5.0f);
            m_currentState = MenuState::Main;
        } else {
            ShowNotification("Connection failed. Check IP/password.", 4.0f);
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(-1, 0))) {
        m_currentState = MenuState::Main;
    }

    ImGui::End();
}

// ============================================================================
// Player list (unused state, integrated into Main now)
// ============================================================================
void Overlay::RenderPlayerList() {
    m_currentState = MenuState::Main;
}

// ============================================================================
// Notifications — rendered as a small stack in the bottom-left
// Shown even when the menu is closed
// ============================================================================
void Overlay::RenderNotifications() {
    // Snapshot under lock so the update thread can push notifications safely
    std::vector<Notification> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_notifMutex);
        if (m_notifications.empty()) return;
        snapshot = m_notifications;
    }

    float dt = ImGui::GetIO().DeltaTime;

    ImGuiWindowFlags notifFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGuiIO& io = ImGui::GetIO();
    float y = io.DisplaySize.y - 20.0f;

    for (auto it = snapshot.end(); it != snapshot.begin(); ) {
        --it;
        if (it->timeRemaining <= 0.0f) continue;

        float alpha = it->timeRemaining < 1.0f ? it->timeRemaining : 1.0f;
        ImGui::SetNextWindowBgAlpha(alpha * 0.78f);
        ImGui::SetNextWindowPos(ImVec2(20.0f, y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));

        char id[32];
        snprintf(id, sizeof(id), "##notif%u", it->id);
        ImGui::Begin(id, nullptr, notifFlags);
        ImGui::TextUnformatted(it->message.c_str());
        // Use actual rendered window height for gap so notifications don't overlap
        float winH = ImGui::GetWindowHeight();
        ImGui::End();

        y -= winH + 4.0f;
    }

    // Tick timers and remove expired entries under lock
    {
        std::lock_guard<std::mutex> lock(m_notifMutex);
        for (auto& n : m_notifications) n.timeRemaining -= dt;
        m_notifications.erase(
            std::remove_if(m_notifications.begin(), m_notifications.end(),
                [](const Notification& n) { return n.timeRemaining <= 0.0f; }),
            m_notifications.end()
        );
    }
}

// HandleInput is now done inside renderer.cpp's HookedPresent / HookedWndProc
void Overlay::HandleInput() {}

} // namespace DS2Coop::UI
