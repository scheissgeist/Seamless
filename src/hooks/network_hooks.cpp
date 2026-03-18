// Network Hooks - Winsock interception
//
// This hooks the Winsock connect() function, following the ds3os approach.
// Purpose: detect when the game connects to FromSoftware's login server
// (port 50031) so we know the network stack is active.

// WinSock2 MUST be included before Windows.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#include "../../include/hooks.h"
#include "../../include/addresses.h"
#include "../../include/utils.h"

using namespace DS2Coop::Hooks;
using namespace DS2Coop::Utils;
using namespace DS2Coop::Addresses;

// Original connect function
static int (WSAAPI* g_originalConnect)(SOCKET s, const sockaddr* name, int namelen) = nullptr;

// Track connection state
static bool g_gameOnline = false;

// ============================================================================
// Hooked Winsock connect()
// ============================================================================
static int WSAAPI ConnectHook(SOCKET s, const sockaddr* name, int namelen) {
    if (name && name->sa_family == AF_INET) {
        const sockaddr_in* addr = reinterpret_cast<const sockaddr_in*>(name);
        uint16_t port = ntohs(addr->sin_port);

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));

        LOG_INFO("[NET] Game connecting to %s:%u", ipStr, port);

        if (port == DS2_LOGIN_PORT) {
            LOG_INFO("[NET] Detected DS2 login server connection (port %u)", DS2_LOGIN_PORT);
            LOG_INFO("[NET] Game is in ONLINE mode");
            g_gameOnline = true;
        }
    }

    return g_originalConnect(s, name, namelen);
}

// ============================================================================
// Installation - uses MinHook to detour Winsock connect()
// ============================================================================
bool WinsockHooks::InstallHooks() {
    LOG_INFO("Installing Winsock hooks...");

    // Hook connect() from ws2_32.dll
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2) {
        ws2 = LoadLibraryA("ws2_32.dll");
    }

    if (!ws2) {
        LOG_WARNING("ws2_32.dll not loaded yet (game may not have initialized networking)");
        return true; // Non-critical
    }

    void* connectAddr = GetProcAddress(ws2, "connect");
    if (!connectAddr) {
        LOG_WARNING("Could not find connect() in ws2_32.dll");
        return true; // Non-critical
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
    return true; // Non-critical failure
}

void WinsockHooks::UninstallHooks() {
    LOG_INFO("Uninstalling Winsock hooks...");
}
