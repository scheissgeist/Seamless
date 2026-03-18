#pragma once

#include <cstdint>
#include <unordered_set>
#include <unordered_map>

namespace DS2Coop::Sync {

// Progress synchronization manager
class ProgressSync {
public:
    static ProgressSync& GetInstance();
    
    bool Initialize();
    void Shutdown();
    
    // Event flags synchronization
    void SyncEventFlag(uint32_t flagId, bool value);
    bool GetEventFlag(uint32_t flagId);
    void RequestEventFlagSync();
    
    // Boss defeat synchronization
    void SyncBossDefeat(uint32_t bossId);
    bool IsBossDefeated(uint32_t bossId);
    
    // Bonfire synchronization
    void SyncBonfire(uint32_t bonfireId, bool lit);
    bool IsBonfireLit(uint32_t bonfireId);
    void SyncAllBonfires();
    
    // Item pickup synchronization (optional)
    void SyncItemPickup(uint32_t itemId, uint32_t locationId);
    bool IsItemPickedUp(uint32_t itemId, uint32_t locationId);
    
    // Fog gate synchronization
    void NotifyFogGateEntry(uint32_t fogGateId);
    void WaitForPartyAtFogGate(uint32_t fogGateId);

private:
    ProgressSync() = default;
    ~ProgressSync() = default;
    ProgressSync(const ProgressSync&) = delete;
    ProgressSync& operator=(const ProgressSync&) = delete;
    
    bool m_initialized = false;
    std::unordered_map<uint32_t, bool> m_eventFlags;
    std::unordered_set<uint32_t> m_defeatedBosses;
    std::unordered_set<uint32_t> m_litBonfires;
    std::unordered_map<uint64_t, bool> m_pickedItems; // Combined itemId and locationId
};

// Player synchronization manager
class PlayerSync {
public:
    static PlayerSync& GetInstance();
    
    bool Initialize();
    void Shutdown();
    
    void Update(float deltaTime);
    
    // Position synchronization
    void SyncLocalPlayerPosition();
    void ApplyRemotePlayerPosition(uint64_t playerId, float x, float y, float z, float rotX, float rotY, float rotZ);
    
    // State synchronization
    void SyncLocalPlayerState();
    void ApplyRemotePlayerState(uint64_t playerId, int32_t health, int32_t maxHealth, int32_t stamina, int32_t maxStamina);
    
    // Animation synchronization
    void SyncAnimation(uint64_t playerId, uint32_t animationId);
    
    // Equipment synchronization
    void SyncEquipment(uint64_t playerId);

    // Seamless helpers — grant items and extend phantom timer
    bool GrantSoapstones();
    bool MaxPhantomTimer();

private:
    PlayerSync() = default;
    ~PlayerSync() = default;
    PlayerSync(const PlayerSync&) = delete;
    PlayerSync& operator=(const PlayerSync&) = delete;
    
    bool m_initialized = false;
    float m_positionSyncTimer = 0.0f;
    float m_stateSyncTimer = 0.0f;
    
    static constexpr float POSITION_SYNC_INTERVAL = 0.05f; // 20 times per second
    static constexpr float STATE_SYNC_INTERVAL = 0.5f;     // 2 times per second
};

} // namespace DS2Coop::Sync

