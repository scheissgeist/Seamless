// Network Hooks - Winsock interception + Server redirect
//
// Two functions:
// 1. Hook Winsock connect() to redirect game connections from FromSoft → custom server
// 2. Patch hostname + RSA key in game memory so the game resolves to our server

// WinSock2 MUST be included before Windows.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

#include "../../include/hooks.h"
#include "../../include/addresses.h"
#include "../../include/utils.h"
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace DS2Coop::Hooks;
using namespace DS2Coop::Utils;
using namespace DS2Coop::Addresses;

// ============================================================================
// Winsock redirect state
// ============================================================================
static int (WSAAPI* g_originalConnect)(SOCKET s, const sockaddr* name, int namelen) = nullptr;
static bool g_gameOnline = false;
static bool g_redirectActive = false;
static std::string g_redirectIP = "127.0.0.1";
static uint16_t g_redirectPort = 50031;

// ============================================================================
// Hooked Winsock connect() — redirects FromSoft server to custom server
// ============================================================================
static int WSAAPI ConnectHook(SOCKET s, const sockaddr* name, int namelen) {
    if (name && name->sa_family == AF_INET) {
        sockaddr_in* addr = const_cast<sockaddr_in*>(reinterpret_cast<const sockaddr_in*>(name));
        uint16_t port = ntohs(addr->sin_port);

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));

        LOG_INFO("[NET] Game connecting to %s:%u", ipStr, port);

        // Redirect login server connections
        if (port == DS2_LOGIN_PORT && g_redirectActive) {
            LOG_INFO("[NET] REDIRECTING from FromSoft server to custom server %s:%u",
                     g_redirectIP.c_str(), g_redirectPort);

            // Rewrite destination IP
            inet_pton(AF_INET, g_redirectIP.c_str(), &addr->sin_addr);
            // Rewrite destination port
            addr->sin_port = htons(g_redirectPort);

            char newIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, newIp, sizeof(newIp));
            LOG_INFO("[NET] Connection redirected to %s:%u", newIp, g_redirectPort);

            g_gameOnline = true;
        } else if (port == DS2_LOGIN_PORT) {
            LOG_INFO("[NET] Detected DS2 login server connection (port %u) — redirect OFF", DS2_LOGIN_PORT);
            g_gameOnline = true;
        }
    }

    return g_originalConnect(s, name, namelen);
}

// ============================================================================
// WinsockHooks public interface
// ============================================================================
void WinsockHooks::SetServerRedirect(const std::string& ip, uint16_t port) {
    g_redirectIP = ip;
    g_redirectPort = port;
    g_redirectActive = true;
    LOG_INFO("[NET] Server redirect configured: %s:%u", ip.c_str(), port);
}

bool WinsockHooks::IsRedirectActive() {
    return g_redirectActive;
}

bool WinsockHooks::InstallHooks() {
    LOG_INFO("Installing Winsock hooks...");

    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2) {
        ws2 = LoadLibraryA("ws2_32.dll");
    }

    if (!ws2) {
        LOG_WARNING("ws2_32.dll not loaded yet");
        return true;
    }

    void* connectAddr = GetProcAddress(ws2, "connect");
    if (!connectAddr) {
        LOG_WARNING("Could not find connect() in ws2_32.dll");
        return true;
    }

    if (HookManager::GetInstance().InstallHook(
        connectAddr,
        reinterpret_cast<void*>(&ConnectHook),
        reinterpret_cast<void**>(&g_originalConnect)
    )) {
        LOG_INFO("  HOOKED Winsock connect()");
        return true;
    }

    LOG_WARNING("  Failed to hook connect() (non-critical)");
    return true;
}

void WinsockHooks::UninstallHooks() {
    LOG_INFO("Uninstalling Winsock hooks...");
}

// ============================================================================
// Server Redirect — Hostname + RSA key patching in game memory
//
// Adapted from ds3os DS2_ReplaceServerAddressHook.cpp
// DS2's hostname is NOT encrypted (unlike DS3), so we can patch directly.
// ============================================================================

// Search game memory for a wide string
static std::vector<uintptr_t> SearchWideString(const wchar_t* needle) {
    std::vector<uintptr_t> results;

    HMODULE gameModule = GetModuleHandleA("DarkSoulsII.exe");
    if (!gameModule) {
        LOG_ERROR("[REDIRECT] DarkSoulsII.exe module not found");
        return results;
    }

    MODULEINFO modInfo = {};
    GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo));

    uintptr_t base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size_t moduleSize = modInfo.SizeOfImage;
    size_t needleLen = wcslen(needle);
    size_t needleBytes = needleLen * sizeof(wchar_t);

    for (size_t i = 0; i < moduleSize - needleBytes; i++) {
        if (memcmp(reinterpret_cast<void*>(base + i), needle, needleBytes) == 0) {
            results.push_back(base + i);
        }
    }

    return results;
}

// Search game memory for an ASCII string
static std::vector<uintptr_t> SearchAsciiString(const char* needle) {
    std::vector<uintptr_t> results;

    HMODULE gameModule = GetModuleHandleA("DarkSoulsII.exe");
    if (!gameModule) {
        LOG_ERROR("[REDIRECT] DarkSoulsII.exe module not found");
        return results;
    }

    MODULEINFO modInfo = {};
    GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo));

    uintptr_t base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size_t moduleSize = modInfo.SizeOfImage;
    size_t needleLen = strlen(needle);

    for (size_t i = 0; i < moduleSize - needleLen; i++) {
        if (memcmp(reinterpret_cast<void*>(base + i), needle, needleLen) == 0) {
            results.push_back(base + i);
        }
    }

    return results;
}

bool ServerRedirect::PatchHostname(const std::string& newHostname) {
    LOG_INFO("[REDIRECT] Patching server hostname to: %s", newHostname.c_str());

    // DS2 stores the hostname as a wide string in memory
    // The original is: L"frpg2-steam64-ope-login.fromsoftware-game.net"
    const wchar_t* originalHostname = DS2_SERVER_HOSTNAME;

    // Wait for SteamStub DRM to unpack — the hostname may not be in readable
    // memory immediately at DLL load time
    int attempts = 0;
    const int maxAttempts = 60; // 30 seconds max wait

    while (attempts < maxAttempts) {
        auto matches = SearchWideString(originalHostname);

        bool patched = false;
        for (uintptr_t addr : matches) {
            // Check if memory is writable
            MEMORY_BASIC_INFORMATION info;
            if (VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info)) == 0) {
                continue;
            }

            // Make writable if needed
            DWORD oldProtect = 0;
            bool madeWritable = false;
            if ((info.Protect & PAGE_READWRITE) == 0 && (info.Protect & PAGE_EXECUTE_READWRITE) == 0) {
                if (!VirtualProtect(reinterpret_cast<void*>(addr),
                    (wcslen(originalHostname) + 1) * sizeof(wchar_t),
                    PAGE_READWRITE, &oldProtect)) {
                    continue;
                }
                madeWritable = true;
            }

            // Convert hostname to wide string and write it
            // ds3os flips endian for each wchar — we follow the same approach
            std::wstring wideHostname(newHostname.begin(), newHostname.end());

            wchar_t* ptr = reinterpret_cast<wchar_t*>(addr);
            for (size_t i = 0; i < wideHostname.size() + 1; i++) {
                wchar_t chr = (i < wideHostname.size()) ? wideHostname[i] : L'\0';

                // Flip endian (ds3os does this — FromSoft stores them byte-swapped)
                char* source = reinterpret_cast<char*>(&chr);
                std::swap(source[0], source[1]);

                memcpy(ptr, source, sizeof(wchar_t));
                ptr++;
            }

            // Restore protection
            if (madeWritable) {
                VirtualProtect(reinterpret_cast<void*>(addr),
                    (wcslen(originalHostname) + 1) * sizeof(wchar_t),
                    oldProtect, &oldProtect);
            }

            LOG_INFO("[REDIRECT] Patched hostname at 0x%p", reinterpret_cast<void*>(addr));
            patched = true;
        }

        if (patched) {
            LOG_INFO("[REDIRECT] Hostname patching complete");
            return true;
        }

        attempts++;
        Sleep(500);
    }

    LOG_ERROR("[REDIRECT] Failed to find hostname in game memory after %d attempts", maxAttempts);
    return false;
}

bool ServerRedirect::PatchRSAKey(const std::string& newPublicKey) {
    LOG_INFO("[REDIRECT] Patching RSA public key...");

    // FromSoft's original RSA public key (hardcoded in DS2 binary)
    const char* originalKey =
        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCAKCAQEAxSeDuBTm3AytrIOGjDKpwJY+437i1F8leMBASVkknYdzM5HB4z8X\n"
        "YTXDylr/N6XAhgr/LcFFZ68yQNQ4AquriMONB+TWUiX0xu84ixYH3AqRtIVqLQbQ\n"
        "xKZsTfyCRC94n9EnvPeS+ueM495YhLIJQBf9T2aCeoHZBFDh2CghJQCdyd4dOT/E\n"
        "9ZxPImwj1t2fZkkKo4smpGk7GcCask2SGsnk/P2jUJxsOyFlCojaW1IldPxn+lXH\n"
        "dlgHSLjQvMlWiZ2SmOwvJqPWMv6XyUXYqsOdejRJJQjV7jeDzYG8trX+bSQxnTAw\n"
        "ENjvjslEcjBmzOCiqFTA/9H1jMjReZpI/wIBAw==\n"
        "-----END RSA PUBLIC KEY-----\n";

    int attempts = 0;
    const int maxAttempts = 60;

    while (attempts < maxAttempts) {
        auto matches = SearchAsciiString(originalKey);

        bool patched = false;
        for (uintptr_t addr : matches) {
            MEMORY_BASIC_INFORMATION info;
            if (VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info)) == 0) {
                continue;
            }

            DWORD oldProtect = 0;
            bool madeWritable = false;
            if ((info.Protect & PAGE_READWRITE) == 0 && (info.Protect & PAGE_EXECUTE_READWRITE) == 0) {
                if (!VirtualProtect(reinterpret_cast<void*>(addr),
                    strlen(originalKey) + 1,
                    PAGE_READWRITE, &oldProtect)) {
                    continue;
                }
                madeWritable = true;
            }

            // Copy new key over old key
            size_t copyLen = newPublicKey.size() + 1;
            if (copyLen > strlen(originalKey) + 1) {
                LOG_ERROR("[REDIRECT] New RSA key is longer than original — cannot patch");
                continue;
            }

            memcpy(reinterpret_cast<void*>(addr), newPublicKey.c_str(), copyLen);

            // Zero-fill remaining bytes if new key is shorter
            size_t remaining = strlen(originalKey) + 1 - copyLen;
            if (remaining > 0) {
                memset(reinterpret_cast<void*>(addr + copyLen), 0, remaining);
            }

            if (madeWritable) {
                VirtualProtect(reinterpret_cast<void*>(addr),
                    strlen(originalKey) + 1,
                    oldProtect, &oldProtect);
            }

            LOG_INFO("[REDIRECT] Patched RSA key at 0x%p", reinterpret_cast<void*>(addr));
            patched = true;
        }

        if (patched) {
            LOG_INFO("[REDIRECT] RSA key patching complete");
            return true;
        }

        attempts++;
        Sleep(500);
    }

    LOG_ERROR("[REDIRECT] Failed to find RSA key in game memory after %d attempts", maxAttempts);
    return false;
}

bool ServerRedirect::Install(const std::string& serverIp, const std::string& publicKeyPath) {
    LOG_INFO("[REDIRECT] Installing server redirect to %s", serverIp.c_str());

    // Read the public key from file
    std::string publicKey;
    std::ifstream keyFile(publicKeyPath);
    if (keyFile.is_open()) {
        publicKey.assign(std::istreambuf_iterator<char>(keyFile),
                         std::istreambuf_iterator<char>());
        keyFile.close();
        LOG_INFO("[REDIRECT] Loaded public key from %s (%zu bytes)", publicKeyPath.c_str(), publicKey.size());
    } else {
        LOG_ERROR("[REDIRECT] Could not open public key file: %s", publicKeyPath.c_str());
        return false;
    }

    // Patch hostname — the game will resolve this IP instead of FromSoft's server
    if (!PatchHostname(serverIp)) {
        LOG_ERROR("[REDIRECT] Hostname patching failed");
        return false;
    }

    // Patch RSA key — the game will use our server's key for encryption
    if (!PatchRSAKey(publicKey)) {
        LOG_ERROR("[REDIRECT] RSA key patching failed");
        return false;
    }

    LOG_INFO("[REDIRECT] Server redirect installed successfully");
    return true;
}
