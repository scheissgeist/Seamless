// Protobuf Interception Hooks - THE CORE OF SEAMLESS CO-OP
//
// This file implements the ds3os approach: instead of finding individual game
// functions (CreateSession, DestroySession, etc.), we hook the protobuf
// serialization layer that ALL network messages pass through.
//
// When the game tries to send a disconnect/leave message after a boss kill
// or player death, we intercept it here and silently drop it. The game
// thinks the message was sent, but the session stays alive.
//
// Sources:
//   - ds3os/Source/Injector/Hooks/DarkSouls2/DS2_LogProtobufsHook.cpp
//   - AOB patterns verified in DS2 SotFS by TLeonardUK

#include "../../include/hooks.h"
#include "../../include/addresses.h"
#include "../../include/session.h"
#include "../../include/network.h"
#include "../../include/utils.h"
#include "../../include/pattern_scanner.h"
#include "MinHook.h"
#include <typeinfo>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

using namespace DS2Coop::Hooks;
using namespace DS2Coop::Utils;
using namespace DS2Coop::Addresses;

// ============================================================================
// State
// ============================================================================
static std::atomic<bool> g_seamlessActive{false};
static std::atomic<uint32_t> g_blockedCount{0};
static std::atomic<uint32_t> g_totalCount{0};

// Steam ID whitelist — only signs from these players are shown
static std::vector<std::string> g_sessionSteamIds;
static std::mutex g_steamIdMutex;
static std::string g_localSteamId;

// ============================================================================
// Get local Steam ID from steam_api64.dll
// ============================================================================
typedef uint64_t(*SteamID_t)();

static uint64_t SafeCallGetSteamID(void* userIface) {
    typedef uint64_t(__fastcall* Fn)(void*);
    void** vt = *reinterpret_cast<void***>(userIface);
    auto fn = reinterpret_cast<Fn>(vt[2]);
    __try { return fn(userIface); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static std::string GetLocalSteamIdInternal() {
    if (!g_localSteamId.empty()) return g_localSteamId;

    HMODULE hSteam = GetModuleHandleW(L"steam_api64.dll");
    if (!hSteam) return "";

    typedef void*(*GetSteamUser_t)();
    auto SteamUser = reinterpret_cast<GetSteamUser_t>(GetProcAddress(hSteam, "SteamAPI_SteamUser_v023"));
    if (!SteamUser) SteamUser = reinterpret_cast<GetSteamUser_t>(GetProcAddress(hSteam, "SteamAPI_SteamUser_v021"));
    if (!SteamUser) return "";

    void* user = SteamUser();
    if (!user) return "";

    uint64_t steamId = SafeCallGetSteamID(user);
    if (steamId == 0) return "";

    g_localSteamId = std::to_string(steamId);
    LOG_INFO("[STEAM] Local Steam ID: %s", g_localSteamId.c_str());
    return g_localSteamId;
}

// ============================================================================
// Check if raw protobuf bytes contain a steam_id from our session
// Field 5 (player_steam_id) has wire key 0x2A (field 5, type 2 = length-delimited)
// ============================================================================
static bool ContainsSessionSteamId(const uint8_t* data, int size) {
    std::lock_guard<std::mutex> lock(g_steamIdMutex);
    if (g_sessionSteamIds.empty()) return true; // no filter active

    // Scan for wire key 0x2A followed by varint length then steam ID string
    for (int i = 0; i < size - 20; i++) {
        if (data[i] == 0x2A) {
            // Read varint length
            int len = 0;
            int j = i + 1;
            if (j < size) {
                len = data[j] & 0x7F;
                if (data[j] & 0x80 && j + 1 < size) {
                    len |= (data[j + 1] & 0x7F) << 7;
                    j++;
                }
                j++;
            }
            // Steam IDs are 17 digits starting with "7656119"
            if (len >= 17 && len <= 20 && j + len <= size) {
                std::string candidate(reinterpret_cast<const char*>(data + j), len);
                if (candidate.substr(0, 7) == "7656119") {
                    // Found a Steam ID — check if it's in our session
                    for (const auto& sid : g_sessionSteamIds) {
                        if (sid == candidate) return true;
                    }
                    // Steam ID found but not in session — block this sign
                    return false;
                }
            }
        }
    }

    return true; // no steam ID found in message — allow through
}

// Original function pointers
static ProtobufHooks::SerializeFunc g_originalSerialize = nullptr;
static ProtobufHooks::ParseFunc g_originalParse = nullptr;

// ============================================================================
// RTTI Helper - extract class name from MSVC vtable
// This is how ds3os identifies which protobuf message is being serialized.
//
// MSVC x64 RTTI layout:
//   object -> vtable pointer (first 8 bytes)
//   vtable[-1] -> pointer to RTTICompleteObjectLocator
//   COL -> pTypeDescriptor (offset within module)
//   TypeDescriptor -> decorated name string
//
// The ds3os project uses this same technique (GetRttiNameFromObject).
// ============================================================================

// MSVC RTTI structures (simplified)
struct _RTTITypeDescriptor {
    void* pVFTable;
    void* spare;
    char name[1]; // Decorated name, e.g. ".?AVRequestNotifyDisconnectSession@@"
};

struct _RTTICompleteObjectLocator {
    uint32_t signature;       // 1 for x64
    uint32_t offset;
    uint32_t cdOffset;
    int32_t  pTypeDescriptor; // RVA to TypeDescriptor (relative to module base in x64)
    int32_t  pClassHierarchy; // RVA to ClassHierarchyDescriptor
    int32_t  pSelf;           // RVA to this COL (for module base calculation)
};

static const char* GetRttiClassName(void* obj) {
    if (!obj) return "unknown";

    __try {
        // Read vtable pointer
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(obj);
        if (!vtable) return "unknown";

        // vtable[-1] points to RTTICompleteObjectLocator
        auto* col = reinterpret_cast<_RTTICompleteObjectLocator*>(vtable[-1]);
        if (!col) return "unknown";

        // For x64 (signature == 1), TypeDescriptor is an RVA from module base.
        // Calculate module base from the COL's pSelf field.
        if (col->signature == 1) {
            uintptr_t colAddr = reinterpret_cast<uintptr_t>(col);
            uintptr_t moduleBase = colAddr - static_cast<uint32_t>(col->pSelf);
            auto* typeDesc = reinterpret_cast<_RTTITypeDescriptor*>(
                moduleBase + static_cast<uint32_t>(col->pTypeDescriptor));
            return typeDesc->name; // ".?AVClassName@@"
        }

        // x86 fallback (signature == 0) - direct pointer
        auto* typeDesc = reinterpret_cast<_RTTITypeDescriptor*>(
            static_cast<uintptr_t>(col->pTypeDescriptor));
        return typeDesc->name;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return "unknown";
    }
}

// ============================================================================
// Check if a message class name corresponds to a disconnect/leave message
// ============================================================================
// Messages to block when SENDING (outgoing — serialize hook)
// Block ALL disconnect/leave messages. This is aggressive but necessary —
// the game sends these after boss kills, deaths, and area transitions.
static bool IsOutgoingDisconnect(const char* className) {
    if (!className) return false;
    if (strstr(className, "DisconnectSession")) return true;
    if (strstr(className, "LeaveSession")) return true;
    if (strstr(className, "LeaveGuestPlayer")) return true;
    return false;
}

// Messages to block when RECEIVING (incoming — parse hook)
// Block everything the server sends that would end our session.
static bool IsIncomingDisconnect(const char* className) {
    if (!className) return false;
    if (strstr(className, "DisconnectSession")) return true;
    if (strstr(className, "LeaveSession")) return true;
    if (strstr(className, "LeaveGuestPlayer")) return true;
    if (strstr(className, "BanishPlayer")) return true;
    if (strstr(className, "ReturnToOwnWorld")) return true;
    if (strstr(className, "RemovePlayer")) return true;
    if (strstr(className, "BreakInTarget")) return true;
    return false;
}

// ============================================================================
// Check if a message is a death notification (we log but don't block)
// ============================================================================
static bool IsDeathMessage(const char* className) {
    if (!className) return false;
    if (strstr(className, "NotifyDeath")) return true;
    if (strstr(className, "NotifyKillPlayer")) return true;
    return false;
}

// ============================================================================
// HOOKED: SerializeWithCachedSizesToArray
//
// Every outgoing protobuf message passes through this function.
// We inspect the message type via RTTI and block disconnect messages.
// ============================================================================
static uint8_t* __fastcall SerializeHook(void* thisPtr, uint8_t* target) {
    g_totalCount++;

    const char* className = GetRttiClassName(thisPtr);

    // If seamless mode is active, block disconnect messages
    if (g_seamlessActive.load()) {
        if (IsOutgoingDisconnect(className)) {
            LOG_INFO("[SEAMLESS] Seamless is ON, blocking: %s", className);
            g_blockedCount++;
            LOG_INFO("[SEAMLESS] BLOCKED outgoing message: %s (total blocked: %u)",
                     className, g_blockedCount.load());

            // Return target unchanged - the message is "serialized" to nothing.
            // The game thinks it sent the message, but the buffer is untouched.
            return target;
        }

        if (IsDeathMessage(className)) {
            LOG_INFO("[SEAMLESS] Death notification sent (allowed): %s", className);
            // We allow death messages through - the session should survive deaths
            // because we block the disconnect that would normally follow.
        }
    }

    // Log ALL session-related messages
    if (strstr(className, "Session") || strstr(className, "Sign") ||
        strstr(className, "Guest") || strstr(className, "BreakIn") ||
        strstr(className, "Leave") || strstr(className, "Return") ||
        strstr(className, "Banish") || strstr(className, "Remove") ||
        strstr(className, "Phantom") || strstr(className, "Summon")) {
        LOG_INFO("[PROTOBUF >>] %s (seamless=%s)", className,
                 g_seamlessActive.load() ? "ON" : "OFF");
    }

    // Call original for all non-blocked messages
    return g_originalSerialize(thisPtr, target);
}

// ============================================================================
// HOOKED: ParseFromArray
//
// Every incoming protobuf message passes through this function.
// We log interesting messages for debugging and event detection.
// ============================================================================
static bool __fastcall ParseHook(void* thisPtr, void* data, int size) {
    // Call original first so the object is populated
    bool result = g_originalParse(thisPtr, data, size);

    if (result) {
        const char* className = GetRttiClassName(thisPtr);

        // Log session-related incoming messages
        if (strstr(className, "Session") || strstr(className, "Guest") ||
            strstr(className, "Sign") || strstr(className, "BreakIn")) {
            LOG_DEBUG("[PROTOBUF <<] %s (size: %d)", className, size);
        }

        // If we receive a disconnect push from the server while seamless is active,
        // return false so the game thinks the parse failed and ignores it.
        if (g_seamlessActive.load() && IsIncomingDisconnect(className)) {
            LOG_INFO("[SEAMLESS] BLOCKED incoming disconnect from server: %s", className);
            g_blockedCount++;
            return false;
        }

        // Filter summon signs — only show signs from players in our session
        if (g_seamlessActive.load() && data && size > 0 &&
            (strstr(className, "SummonSign") || strstr(className, "SignList") ||
             strstr(className, "PushSign"))) {
            if (!ContainsSessionSteamId(static_cast<const uint8_t*>(data), size)) {
                LOG_DEBUG("[SEAMLESS] Filtered sign from non-session player");
                return false;
            }
        }
    }

    return result;
}

// ============================================================================
// Installation
// ============================================================================
bool ProtobufHooks::InstallHooks() {
    LOG_INFO("==========================================");
    LOG_INFO("Installing Protobuf Interception Hooks...");
    LOG_INFO("==========================================");

    int hooked = 0;

    // Find SerializeWithCachedSizesToArray via AOB scan
    LOG_INFO("Scanning for SerializeWithCachedSizesToArray...");
    uintptr_t serializeAddr = PatternScanner::FindPattern(
        ProtobufPatterns::SERIALIZE_PATTERN,
        ProtobufPatterns::SERIALIZE_MASK,
        nullptr
    );

    if (serializeAddr) {
        LOG_INFO("  Found at: 0x%p", reinterpret_cast<void*>(serializeAddr));
        if (HookManager::GetInstance().InstallHook(
            reinterpret_cast<void*>(serializeAddr),
            reinterpret_cast<void*>(&SerializeHook),
            reinterpret_cast<void**>(&g_originalSerialize)
        )) {
            LOG_INFO("  HOOKED SerializeWithCachedSizesToArray");
            hooked++;
        } else {
            LOG_ERROR("  Failed to hook SerializeWithCachedSizesToArray");
        }
    } else {
        LOG_ERROR("  SerializeWithCachedSizesToArray pattern NOT FOUND");
        LOG_ERROR("  This is the critical hook - seamless co-op cannot work without it.");
        LOG_ERROR("  Your game version may have a different protobuf implementation.");
    }

    // Find ParseFromArray via AOB scan
    LOG_INFO("Scanning for ParseFromArray...");
    uintptr_t parseAddr = PatternScanner::FindPattern(
        ProtobufPatterns::PARSE_PATTERN,
        ProtobufPatterns::PARSE_MASK,
        nullptr
    );

    if (parseAddr) {
        LOG_INFO("  Found at: 0x%p", reinterpret_cast<void*>(parseAddr));
        if (HookManager::GetInstance().InstallHook(
            reinterpret_cast<void*>(parseAddr),
            reinterpret_cast<void*>(&ParseHook),
            reinterpret_cast<void**>(&g_originalParse)
        )) {
            LOG_INFO("  HOOKED ParseFromArray");
            hooked++;
        } else {
            LOG_ERROR("  Failed to hook ParseFromArray");
        }
    } else {
        LOG_WARNING("  ParseFromArray pattern NOT FOUND (non-critical, logging only)");
    }

    LOG_INFO("==========================================");
    LOG_INFO("Protobuf Hooks Result: %d/2 installed", hooked);
    LOG_INFO("==========================================");

    if (hooked >= 1) {
        LOG_INFO("Protobuf interception active.");
        if (hooked == 1) {
            LOG_WARNING("Only serialize hook installed. Incoming message logging unavailable.");
        }
        return true;
    }

    LOG_ERROR("No protobuf hooks installed. Seamless co-op will NOT work.");
    LOG_ERROR("The mod will still load but cannot prevent disconnections.");
    return false;
}

void ProtobufHooks::UninstallHooks() {
    LOG_INFO("Uninstalling protobuf hooks...");
    g_seamlessActive = false;
}

void ProtobufHooks::SetSeamlessActive(bool active) {
    bool prev = g_seamlessActive.exchange(active);
    if (prev != active) {
        LOG_INFO("[SEAMLESS] Disconnect blocking %s", active ? "ENABLED" : "DISABLED");
    }
}

bool ProtobufHooks::IsSeamlessActive() {
    return g_seamlessActive.load();
}

uint32_t ProtobufHooks::GetBlockedMessageCount() {
    return g_blockedCount.load();
}

uint32_t ProtobufHooks::GetTotalMessageCount() {
    return g_totalCount.load();
}

void ProtobufHooks::AddSessionSteamId(const std::string& steamId) {
    std::lock_guard<std::mutex> lock(g_steamIdMutex);
    // Don't add duplicates
    for (const auto& s : g_sessionSteamIds) {
        if (s == steamId) return;
    }
    g_sessionSteamIds.push_back(steamId);
    LOG_INFO("[SEAMLESS] Added session Steam ID: %s (total: %zu)", steamId.c_str(), g_sessionSteamIds.size());
}

void ProtobufHooks::ClearSessionSteamIds() {
    std::lock_guard<std::mutex> lock(g_steamIdMutex);
    g_sessionSteamIds.clear();
    LOG_INFO("[SEAMLESS] Cleared session Steam ID whitelist");
}

std::string ProtobufHooks::GetLocalSteamId() {
    return GetLocalSteamIdInternal();
}
