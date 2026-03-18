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
#include "../../include/utils.h"
#include <chrono>

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

    m_positionSyncTimer += deltaTime;
    m_stateSyncTimer += deltaTime;

    if (m_positionSyncTimer >= POSITION_SYNC_INTERVAL) {
        SyncLocalPlayerPosition();
        m_positionSyncTimer = 0.0f;
    }

    if (m_stateSyncTimer >= STATE_SYNC_INTERVAL) {
        SyncLocalPlayerState();
        m_stateSyncTimer = 0.0f;
    }
}

// ============================================================================
// Position sync - reads ACTUAL game memory
// ============================================================================
void PlayerSync::SyncLocalPlayerPosition() {
    auto& sessionMgr = Session::SessionManager::GetInstance();
    if (!sessionMgr.IsActive()) return;

    auto* localPlayer = sessionMgr.GetLocalPlayer();
    if (!localPlayer) return;

    float x = 0, y = 0, z = 0, rotY = 0;

    // Try reading from actual game memory first
    if (ReadPlayerPosition(x, y, z, rotY)) {
        localPlayer->x = x;
        localPlayer->y = y;
        localPlayer->z = z;
    } else {
        x = localPlayer->x;
        y = localPlayer->y;
        z = localPlayer->z;
        LOG_DEBUG("Could not read player position from game memory, using cached");
    }

    // Build and broadcast position packet
    Network::PlayerPositionPacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::PlayerPosition;
    packet.header.size = sizeof(Network::PlayerPositionPacket);
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

    auto* localPlayer = sessionMgr.GetLocalPlayer();
    if (!localPlayer) return;

    int32_t health = 0, maxHealth = 0;
    float stamina = 0;
    uint32_t soulLevel = 0;

    // Read actual values from game memory
    bool gotHealth = ReadPlayerHealth(health, maxHealth);
    bool gotLevel = ReadPlayerLevel(soulLevel);
    ReadPlayerStamina(stamina);

    if (gotHealth) {
        localPlayer->health = health;
        localPlayer->maxHealth = maxHealth;
        localPlayer->isAlive = (health > 0);
    } else {
        health = localPlayer->health;
        maxHealth = localPlayer->maxHealth;
    }

    if (gotLevel) {
        localPlayer->soulLevel = soulLevel;
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
// Grant White Sign Soapstone + Small White Sign Soapstone
// Scans inventory for empty slots and writes item IDs
// ============================================================================
bool PlayerSync::GrantSoapstones() {
    uintptr_t playerData = 0;
    if (!ReadPlayerDataBase(playerData)) {
        LOG_ERROR("GrantSoapstones: could not read PlayerData base");
        return false;
    }

    uintptr_t invBase = playerData + Offsets::GameManager::InventoryBase;
    uint32_t itemsToGrant[] = {
        Addresses::ItemIDs::WhiteSignSoapstone,
        Addresses::ItemIDs::SmallWhiteSignSoapstone
    };

    for (uint32_t itemId : itemsToGrant) {
        // Check if player already has this item (scan first 256 slots)
        bool alreadyHas = false;
        for (int i = 0; i < 256; i++) {
            uint32_t slotItem = 0;
            if (Memory::Read<uint32_t>(invBase + i * Offsets::GameManager::InventorySlotSize, &slotItem)) {
                if (slotItem == itemId) {
                    alreadyHas = true;
                    break;
                }
            }
        }

        if (alreadyHas) {
            LOG_INFO("GrantSoapstones: player already has item 0x%08X", itemId);
            continue;
        }

        // Find first empty slot (item ID == 0)
        bool granted = false;
        for (int i = 0; i < 256; i++) {
            uint32_t slotItem = 0;
            uintptr_t slotAddr = invBase + i * Offsets::GameManager::InventorySlotSize;
            if (Memory::Read<uint32_t>(slotAddr, &slotItem) && slotItem == 0) {
                if (Memory::Write<uint32_t>(slotAddr, itemId)) {
                    LOG_INFO("GrantSoapstones: wrote item 0x%08X to slot %d", itemId, i);
                    granted = true;
                    break;
                }
            }
        }

        if (!granted) {
            LOG_WARNING("GrantSoapstones: could not find empty slot for 0x%08X", itemId);
        }
    }

    return true;
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
        LOG_INFO("MaxPhantomTimer: AllottedTime set to %.0f", maxTime);
        return true;
    }

    LOG_ERROR("MaxPhantomTimer: failed to write AllottedTime");
    return false;
}
