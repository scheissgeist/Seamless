// Hook system for DS2 Seamless Co-op
//
// Strategy: Hook at the protobuf serialization layer (verified AOB from ds3os)
// rather than guessing internal game function addresses.
//
// The game sends ALL network messages through protobuf serialize/parse.
// By hooking those two functions, we can intercept and block disconnect
// messages (opcodes 0x03F9, 0x03EB, 0x03E9) to keep sessions alive.

#pragma once
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>
#include <atomic>

namespace DS2Coop::Hooks {

// ============================================================================
// Hook Manager (MinHook wrapper)
// ============================================================================
class HookManager {
public:
    static HookManager& GetInstance();

    bool Initialize();
    void Shutdown();

    bool InstallHook(void* targetFunc, void* detourFunc, void** originalFunc);
    bool RemoveHook(void* targetFunc);
    bool EnableHooks();
    bool DisableHooks();

private:
    HookManager() = default;
    ~HookManager() = default;
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

    bool m_initialized = false;
};

// ============================================================================
// Protobuf Interception Hooks (core of the seamless co-op mechanism)
//
// ds3os discovered that DS2 routes all network messages through protobuf
// serialize/parse functions with known AOB patterns. By hooking these:
//
// 1. We intercept outgoing SerializeWithCachedSizesToArray calls
// 2. We use RTTI to get the protobuf class name (message type)
// 3. If it's a disconnect/leave message, we block it
// 4. The game thinks it sent the disconnect, but nothing went out
// 5. Session stays alive through boss kills, deaths, area transitions
// ============================================================================
namespace ProtobufHooks {
    bool InstallHooks();
    void UninstallHooks();

    // Function signatures from ds3os reverse engineering
    using SerializeFunc = uint8_t*(__fastcall*)(void* thisPtr, uint8_t* target);
    using ParseFunc = bool(__fastcall*)(void* thisPtr, void* data, int size);

    // Control whether disconnect messages are blocked
    void SetSeamlessActive(bool active);
    bool IsSeamlessActive();

    // Stats for debugging
    uint32_t GetBlockedMessageCount();
    uint32_t GetTotalMessageCount();

    // Sign filtering — only show summon signs from session members
    void AddSessionSteamId(const std::string& steamId);
    void ClearSessionSteamIds();
    std::string GetLocalSteamId();
}

// ============================================================================
// Winsock Hooks (connection monitoring)
// Hooks Winsock connect() to detect when the game connects to FromSoft servers.
// ============================================================================
namespace WinsockHooks {
    bool InstallHooks();
    void UninstallHooks();
}

// ============================================================================
// Game State Hooks (secondary - for detecting events locally)
// These are optional and use pattern scanning to find game functions.
// If they fail, the mod still works through protobuf interception alone.
// ============================================================================
namespace GameState {
    bool InstallHooks();
    void UninstallHooks();

    using PlayerDeathFunc = void(__fastcall*)(void* playerPtr);
    using BossDefeatedFunc = void(__fastcall*)(void* bossPtr);
}

} // namespace DS2Coop::Hooks
