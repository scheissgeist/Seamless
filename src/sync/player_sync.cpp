// Player Synchronization - ACTUAL GAME MEMORY READS
//
// This file reads player position/health/state from the game's memory
// using the resolved GameManagerImp base pointer and verified offsets
// from the Bob Edition cheat table.
//
// Pointer chain: GameManagerImp -> +0x38 (PlayerData) -> offsets

#include "../../include/sync.h"
#include "../../include/session.h"
#include "../../include/network.h"
#include "../../include/addresses.h"
#include "../../include/address_resolver.h"
#include "../../include/hooks.h"
#include "../../include/pattern_scanner.h"
#include "../../include/utils.h"
#include <chrono>
#include <cfloat>

using namespace DS2Coop::Sync;
using namespace DS2Coop::Utils;
using namespace DS2Coop::Addresses;

PlayerSync& PlayerSync::GetInstance() {
    static PlayerSync instance;
    return instance;
}

bool PlayerSync::Initialize() {
    if (m_initialized) return true;

    LOG_INFO("Initializing player sync...");

    // Verify we have the GameManagerImp address
    auto& resolver = DS2Coop::AddressResolver::GetInstance();
    if (!resolver.GetGameManagerImp()) {
        LOG_WARNING("GameManagerImp not resolved - player sync will use session data only");
    } else {
        LOG_INFO("Player sync will read from GameManagerImp at 0x%p",
                 reinterpret_cast<void*>(resolver.GetGameManagerImp()));
    }

    m_initialized = true;
    LOG_INFO("Player sync initialized");
    return true;
}

void PlayerSync::Shutdown() {
    if (!m_initialized) return;
    LOG_INFO("Shutting down player sync...");
    m_initialized = false;
}

// ============================================================================
// Read player data directly from game memory
// ============================================================================

static bool ReadPlayerDataBase(uintptr_t& outPlayerData) {
    auto& resolver = DS2Coop::AddressResolver::GetInstance();
    uintptr_t gmImp = resolver.GetGameManagerImp();
    if (!gmImp) return false;

    // GameManagerImp -> +0x38 -> PlayerData pointer
    uintptr_t playerDataPtr = 0;
    if (!Memory::Read<uintptr_t>(gmImp + Offsets::GameManager::PlayerData, &playerDataPtr)) {
        return false;
    }

    if (!playerDataPtr) return false;
    outPlayerData = playerDataPtr;
    return true;
}

static bool ReadCharacterNameRaw(uintptr_t playerData, wchar_t* nameBuf, int maxChars) {
    __try {
        for (int i = 0; i < maxChars; i++) {
            wchar_t ch = 0;
            if (!Memory::Read<wchar_t>(playerData + Offsets::GameManager::CharacterName + i * 2, &ch))
                break;
            if (ch == 0) break;
            nameBuf[i] = ch;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Try reading a wchar name from an address, convert to UTF-8. Returns "" on failure.
static std::string WcharToUtf8(const wchar_t* buf) {
    if (!buf || buf[0] == 0) return "";
    if (buf[0] < 0x20) return ""; // not printable — likely a pointer, not a name
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &result[0], len, nullptr, nullptr);
    return result;
}

static std::string ReadCharacterName() {
    // Character name offset not yet verified for this game version.
    // Return empty so callers fall back to "Host"/"Player".
    return "";
}

static bool ReadPlayerPosition(float& x, float& y, float& z, float& rotY) {
    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) return false;

    bool ok = true;
    ok &= Memory::Read<float>(playerData + Offsets::GameManager::PositionX, &x);
    ok &= Memory::Read<float>(playerData + Offsets::GameManager::PositionY, &y);
    ok &= Memory::Read<float>(playerData + Offsets::GameManager::PositionZ, &z);
    // Rotation is optional — don't fail the whole read if it's wrong
    if (!Memory::Read<float>(playerData + Offsets::GameManager::RotationY, &rotY))
        rotY = 0.0f;
    return ok;
}

static bool ReadPlayerHealth(int32_t& health, int32_t& maxHealth) {
    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) return false;

    bool ok = true;
    ok &= Memory::Read<int32_t>(playerData + Offsets::GameManager::Health, &health);
    ok &= Memory::Read<int32_t>(playerData + Offsets::GameManager::MaxHealth, &maxHealth);
    return ok;
}

static bool ReadPlayerLevel(uint32_t& level) {
    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) return false;

    return Memory::Read<uint32_t>(playerData + Offsets::GameManager::Level, &level);
}

static bool ReadPlayerStamina(float& stamina) {
    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) return false;

    return Memory::Read<float>(playerData + Offsets::GameManager::Stamina, &stamina);
}

// ============================================================================
// Sync update loop
// ============================================================================

void PlayerSync::Update(float deltaTime) {
    if (!m_initialized) return;

    __try {
        m_positionSyncTimer += deltaTime;
        m_stateSyncTimer += deltaTime;
        m_phantomTimerRefresh += deltaTime;

        if (m_positionSyncTimer >= POSITION_SYNC_INTERVAL) {
            SyncLocalPlayerPosition();
            m_positionSyncTimer = 0.0f;
        }

        if (m_stateSyncTimer >= STATE_SYNC_INTERVAL) {
            SyncLocalPlayerState();
            m_stateSyncTimer = 0.0f;
        }

        // Silently keep phantom timer maxed and phantom type normal (every 5 seconds)
        if (m_phantomTimerRefresh >= 5.0f) {
            MaxPhantomTimer();
            EnableSummoning();
            m_phantomTimerRefresh = 0.0f;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("PlayerSync::Update CRASHED (exception 0x%08X) — disabling sync",
                  GetExceptionCode());
        m_initialized = false;
    }
}

// ============================================================================
// Position sync - reads ACTUAL game memory
// ============================================================================
void PlayerSync::SyncLocalPlayerPosition() {
    auto& sessionMgr = Session::SessionManager::GetInstance();
    if (!sessionMgr.IsActive()) return;

    // Copy local player data under lock to avoid racing with network thread
    auto players = sessionMgr.GetPlayers();
    Session::SessionPlayer* localPlayer = nullptr;
    uint64_t localId = Network::PeerManager::GetInstance().GetLocalPlayerId();
    for (auto& p : players) {
        if (p.playerId == localId) { localPlayer = &p; break; }
    }
    if (!localPlayer) return;

    float x = 0, y = 0, z = 0, rotY = 0;

    // Try reading from actual game memory first
    if (ReadPlayerPosition(x, y, z, rotY)) {
        // Store back to session manager (the copy is discarded, so write directly)
        sessionMgr.UpdatePlayerPosition(localId, x, y, z);
    } else {
        x = localPlayer->x;
        y = localPlayer->y;
        z = localPlayer->z;
        LOG_DEBUG("Could not read player position from game memory, using cached");
    }

    // Build and broadcast position packet
    static uint32_t posSequence = 0;
    Network::PlayerPositionPacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::PlayerPosition;
    packet.header.size = sizeof(Network::PlayerPositionPacket);
    packet.header.sequence = ++posSequence;
    packet.header.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    packet.playerId = localPlayer->playerId;
    packet.x = x;
    packet.y = y;
    packet.z = z;
    packet.rotX = 0.0f;
    packet.rotY = rotY;
    packet.rotZ = 0.0f;
    packet.animation = 0;

    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.BroadcastPacket(&packet.header);

    LOG_DEBUG("Synced position: (%.2f, %.2f, %.2f)", x, y, z);
}

// ============================================================================
// State sync - reads ACTUAL game memory
// ============================================================================
void PlayerSync::SyncLocalPlayerState() {
    auto& sessionMgr = Session::SessionManager::GetInstance();
    if (!sessionMgr.IsActive()) return;

    auto players = sessionMgr.GetPlayers();
    Session::SessionPlayer* localPlayer = nullptr;
    uint64_t localId = Network::PeerManager::GetInstance().GetLocalPlayerId();
    for (auto& p : players) {
        if (p.playerId == localId) { localPlayer = &p; break; }
    }
    if (!localPlayer) return;

    int32_t health = 0, maxHealth = 0;
    float stamina = 0;
    uint32_t soulLevel = 0;

    // Read actual values from game memory
    bool gotHealth = ReadPlayerHealth(health, maxHealth);
    bool gotLevel = ReadPlayerLevel(soulLevel);
    ReadPlayerStamina(stamina);

    if (gotHealth) {
        sessionMgr.UpdatePlayerHealth(localId, health, maxHealth);
    } else {
        health = localPlayer->health;
        maxHealth = localPlayer->maxHealth;
    }

    if (gotLevel) {
        sessionMgr.UpdatePlayerLevel(localId, soulLevel);
    } else {
        soulLevel = localPlayer->soulLevel;
    }

    // Build and broadcast state packet
    Network::PlayerStatePacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::PlayerState;
    packet.header.size = sizeof(Network::PlayerStatePacket);
    packet.header.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    packet.playerId = localPlayer->playerId;
    packet.health = health;
    packet.maxHealth = maxHealth;
    packet.stamina = static_cast<int32_t>(stamina);
    packet.maxStamina = 100;
    packet.souls = 0;
    packet.soulLevel = soulLevel;

    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.BroadcastPacket(&packet.header);

    LOG_DEBUG("Synced state: HP %d/%d, SL %u", health, maxHealth, soulLevel);
}

// ============================================================================
// Remote player updates (from network)
// ============================================================================

void PlayerSync::ApplyRemotePlayerPosition(uint64_t playerId, float x, float y, float z,
                                           float rotX, float rotY, float rotZ) {
    LOG_DEBUG("Remote player %llu position: (%.2f, %.2f, %.2f)", playerId, x, y, z);
    auto& sessionMgr = Session::SessionManager::GetInstance();
    sessionMgr.UpdatePlayerPosition(playerId, x, y, z);
}

void PlayerSync::ApplyRemotePlayerState(uint64_t playerId, int32_t health, int32_t maxHealth,
                                        int32_t stamina, int32_t maxStamina) {
    LOG_DEBUG("Remote player %llu state: HP %d/%d", playerId, health, maxHealth);
    auto& sessionMgr = Session::SessionManager::GetInstance();
    sessionMgr.UpdatePlayerHealth(playerId, health, maxHealth);
}

void PlayerSync::SyncAnimation(uint64_t playerId, uint32_t animationId) {
    LOG_DEBUG("Animation sync for player %llu: %u", playerId, animationId);
}

void PlayerSync::SyncEquipment(uint64_t playerId) {
    LOG_DEBUG("Equipment sync for player %llu", playerId);
}

// ============================================================================
// Item struct for the game's internal ItemGive function (16 bytes)
// ============================================================================
#pragma pack(push, 1)
struct DS2ItemStruct {
    int32_t  type;       // 3 = consumable
    int32_t  itemId;     // e.g. 0x03B280B0
    float    durability; // FLT_MAX
    int16_t  quantity;   // count
    uint8_t  upgrade;    // 0-10
    uint8_t  infusion;   // 0 = none
};
#pragma pack(pop)

// x64 fastcall: void ItemGive(void* bag, DS2ItemStruct* items, int count, int mode)
typedef void (__fastcall *ItemGiveFunc)(void* bag, DS2ItemStruct* items, int count, int mode);
static ItemGiveFunc g_itemGiveFunc = nullptr;
static bool g_itemGiveScanned = false;

// ============================================================================
// Resolve the ItemGive function and AvailableItemBag pointer
// ============================================================================
static bool ResolveItemGive(uintptr_t& outBag) {
    // Find ItemGive function via AOB (only scan once)
    if (!g_itemGiveScanned) {
        g_itemGiveScanned = true;
        uintptr_t addr = DS2Coop::Utils::PatternScanner::FindPattern(
            ItemGib::ITEM_GIVE_PATTERN,
            ItemGib::ITEM_GIVE_MASK,
            nullptr);
        if (addr) {
            g_itemGiveFunc = reinterpret_cast<ItemGiveFunc>(addr);
            LOG_INFO("ItemGive function found at %p", reinterpret_cast<void*>(addr));
        } else {
            LOG_WARNING("ItemGive function not found — soapstone grant unavailable");
        }
    }

    if (!g_itemGiveFunc) return false;

    // Resolve AvailableItemBag: [BaseA] -> +0xA8 -> +0x10 -> +0x10
    auto& resolver = DS2Coop::AddressResolver::GetInstance();
    uintptr_t baseA = resolver.GetGameManagerImp();
    if (!baseA) { LOG_WARNING("ResolveItemGive: BaseA is null"); return false; }

    uintptr_t ptr1 = 0, ptr2 = 0, bag = 0;
    if (!Memory::Read<uintptr_t>(baseA + ItemGib::AvailItemBag_Off1, &ptr1) || !ptr1) {
        LOG_WARNING("ResolveItemGive: ptr1 failed (BaseA=%p +0x%X)", (void*)baseA, ItemGib::AvailItemBag_Off1);
        return false;
    }
    if (!Memory::Read<uintptr_t>(ptr1 + ItemGib::AvailItemBag_Off2, &ptr2) || !ptr2) {
        LOG_WARNING("ResolveItemGive: ptr2 failed (ptr1=%p +0x%X)", (void*)ptr1, ItemGib::AvailItemBag_Off2);
        return false;
    }
    if (!Memory::Read<uintptr_t>(ptr2 + ItemGib::AvailItemBag_Off3, &bag) || !bag) {
        LOG_WARNING("ResolveItemGive: bag failed (ptr2=%p +0x%X)", (void*)ptr2, ItemGib::AvailItemBag_Off3);
        return false;
    }

    LOG_INFO("ResolveItemGive: BaseA=%p -> ptr1=%p -> ptr2=%p -> bag=%p",
             (void*)baseA, (void*)ptr1, (void*)ptr2, (void*)bag);
    outBag = bag;
    return true;
}

// ============================================================================
// Grant White Sign Soapstone + Small White Sign Soapstone
// Calls the game's internal ItemGive function
// ============================================================================
bool PlayerSync::GrantSoapstones() {
    uintptr_t bag = 0;
    if (!ResolveItemGive(bag)) {
        LOG_WARNING("GrantSoapstones: could not resolve ItemGive or AvailableItemBag");
        return false;
    }

    DS2ItemStruct items[2] = {};

    // White Sign Soapstone
    items[0].type       = ItemCategory::Consumable;
    items[0].itemId     = ItemIDs::WhiteSignSoapstone;
    items[0].durability = FLT_MAX;
    items[0].quantity   = 1;
    items[0].upgrade    = 0;
    items[0].infusion   = 0;

    // Small White Sign Soapstone
    items[1].type       = ItemCategory::Consumable;
    items[1].itemId     = ItemIDs::SmallWhiteSignSoapstone;
    items[1].durability = FLT_MAX;
    items[1].quantity   = 1;
    items[1].upgrade    = 0;
    items[1].infusion   = 0;

    LOG_INFO("GrantSoapstones: calling ItemGive at %p with bag=%p, 2 items",
             reinterpret_cast<void*>(g_itemGiveFunc), reinterpret_cast<void*>(bag));

    __try {
        // Give items one at a time to isolate which one crashes (if any)
        g_itemGiveFunc(reinterpret_cast<void*>(bag), &items[0], 1, 0);
        LOG_INFO("GrantSoapstones: White Sign Soapstone given");

        g_itemGiveFunc(reinterpret_cast<void*>(bag), &items[1], 1, 0);
        LOG_INFO("GrantSoapstones: Small White Sign Soapstone given");

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("GrantSoapstones: ItemGive crashed (exception 0x%08X)",
                  GetExceptionCode());
        // Disable further attempts
        g_itemGiveFunc = nullptr;
        return false;
    }
}

// ============================================================================
// Max out phantom AllottedTime so the summon never expires
// ============================================================================
bool PlayerSync::MaxPhantomTimer() {
    auto& resolver = DS2Coop::AddressResolver::GetInstance();
    uintptr_t netSession = resolver.GetNetSessionManager();
    if (!netSession) {
        LOG_ERROR("MaxPhantomTimer: NetSessionManager not resolved");
        return false;
    }

    // NetSessionManager -> +0x18 (SessionPointer) -> +0x17C (AllottedTime)
    uintptr_t sessionPtr = 0;
    if (!Memory::Read<uintptr_t>(netSession + Offsets::NetSession::SessionPointer, &sessionPtr) || !sessionPtr) {
        LOG_WARNING("MaxPhantomTimer: no active session pointer");
        return false;
    }

    // Set AllottedTime to a huge value (float, in seconds)
    float maxTime = 99999.0f;
    if (Memory::Write<float>(sessionPtr + Offsets::NetSession::AllottedTime, maxTime)) {
        LOG_DEBUG("MaxPhantomTimer: AllottedTime set to %.0f", maxTime);
        return true;
    }

    LOG_DEBUG("MaxPhantomTimer: failed to write AllottedTime");
    return false;
}

// ============================================================================
// Enable summoning regardless of hollow state
// Zeroes the hollowing byte so summon signs are visible and usable.
// This also restores max HP (hollowing reduces it in DS2).
// Only runs while seamless mode is active.
// ============================================================================
void PlayerSync::EnableSummoning() {
    if (!DS2Coop::Hooks::ProtobufHooks::IsSeamlessActive()) return;

    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) return;

    // Set TeamType to 0 (Host/Normal) — this makes phantoms:
    // - Appear solid (not ghostly white)
    // - Able to rest at bonfires
    // - Able to talk to NPCs
    // - Able to pick up items
    // - Able to open doors and use levers
    __try {
        uint8_t teamType = 0xFF;
        if (Memory::Read<uint8_t>(playerData + Offsets::GameManager::TeamType, &teamType)) {
            if (teamType != 0x00) {
                Memory::Write<uint8_t>(playerData + Offsets::GameManager::TeamType, (uint8_t)0x00);
                LOG_INFO("EnableSummoning: set TeamType to Host (was 0x%02X)", teamType);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Also set PhantomType to 0 (no phantom visual effect) via NetSession
    auto& resolver = DS2Coop::AddressResolver::GetInstance();
    uintptr_t netSession = resolver.GetNetSessionManager();
    if (!netSession) return;

    __try {
        uintptr_t sessionPtr = 0;
        if (Memory::Read<uintptr_t>(netSession + Offsets::NetSession::SessionPointer, &sessionPtr) && sessionPtr) {
            uint32_t phantomType = 0xFF;
            if (Memory::Read<uint32_t>(sessionPtr + Offsets::NetSession::PhantomType, &phantomType)) {
                if (phantomType != 0) {
                    Memory::Write<uint32_t>(sessionPtr + Offsets::NetSession::PhantomType, (uint32_t)0);
                    LOG_INFO("EnableSummoning: set PhantomType to Normal (was %u)", phantomType);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

std::string PlayerSync::GetLocalCharacterName() {
    return ReadCharacterName();
}
