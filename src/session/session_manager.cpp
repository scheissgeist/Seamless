// Session manager - tracks players, drives sync loops
//
// Coordinates between PeerManager (networking) and PlayerSync/ProgressSync.

#include "../../include/session.h"
#include "../../include/network.h"
#include "../../include/sync.h"
#include "../../include/utils.h"
#include <algorithm>
#include <chrono>

using namespace DS2Coop::Session;
using namespace DS2Coop::Utils;

SessionManager& SessionManager::GetInstance() {
    static SessionManager instance;
    return instance;
}

bool SessionManager::Initialize() {
    if (m_initialized) return true;

    LOG_INFO("Initializing session manager...");

    m_state = SessionState::Disconnected;
    m_initialized = true;

    LOG_INFO("Session manager initialized");
    return true;
}

void SessionManager::Shutdown() {
    if (!m_initialized) return;

    LOG_INFO("Shutting down session manager...");

    LeaveSession();
    m_initialized = false;

    LOG_INFO("Session manager shut down");
}

bool SessionManager::CreateSession(const std::string& password) {
    if (!m_initialized) return false;

    LOG_INFO("Creating new session...");

    TransitionToState(SessionState::Connecting);

    m_isHost = true;
    m_sessionPassword = password;

    // Create network session
    auto& peerMgr = Network::PeerManager::GetInstance();
    if (!peerMgr.CreateSession(password)) {
        LOG_ERROR("Failed to create network session");
        TransitionToState(SessionState::Error);
        return false;
    }

    // Use PeerManager's generated player ID
    m_localPlayerId = peerMgr.GetLocalPlayerId();

    // Add local player
    SessionPlayer localPlayer{};
    localPlayer.playerId = m_localPlayerId;
    localPlayer.playerName = "Host";
    localPlayer.isAlive = true;
    localPlayer.isReady = true;
    localPlayer.soulLevel = 0;
    localPlayer.health = 0;
    localPlayer.maxHealth = 0;
    localPlayer.x = localPlayer.y = localPlayer.z = 0.0f;

    m_players.push_back(localPlayer);
    m_playerMap[localPlayer.playerId] = &m_players.back();

    // Initialize sync systems
    Sync::PlayerSync::GetInstance().Initialize();
    Sync::ProgressSync::GetInstance().Initialize();

    TransitionToState(SessionState::Connected);

    // Auto-grant soapstones so players can summon immediately
    Sync::PlayerSync::GetInstance().GrantSoapstones();

    LOG_INFO("Session created. Local player ID: %llu", m_localPlayerId);
    return true;
}

bool SessionManager::JoinSession(const std::string& address, const std::string& password) {
    if (!m_initialized) return false;

    LOG_INFO("Joining session at %s...", address.c_str());

    TransitionToState(SessionState::Connecting);

    m_isHost = false;
    m_sessionPassword = password;

    // Parse address (ip or ip:port)
    size_t colonPos = address.find(':');
    std::string ip = address.substr(0, colonPos);
    uint16_t port = 27015;
    if (colonPos != std::string::npos) {
        port = static_cast<uint16_t>(std::stoi(address.substr(colonPos + 1)));
    }

    // Join network session
    auto& peerMgr = Network::PeerManager::GetInstance();
    if (!peerMgr.JoinSession(ip, port, password)) {
        LOG_ERROR("Failed to join network session");
        TransitionToState(SessionState::Error);
        return false;
    }

    // Use PeerManager's generated player ID
    m_localPlayerId = peerMgr.GetLocalPlayerId();

    // Add local player
    SessionPlayer localPlayer{};
    localPlayer.playerId = m_localPlayerId;
    localPlayer.playerName = "Player";
    localPlayer.isAlive = true;
    localPlayer.isReady = true;
    localPlayer.soulLevel = 0;
    localPlayer.health = 0;
    localPlayer.maxHealth = 0;
    localPlayer.x = localPlayer.y = localPlayer.z = 0.0f;

    m_players.push_back(localPlayer);
    m_playerMap[localPlayer.playerId] = &m_players.back();

    // Initialize sync systems
    Sync::PlayerSync::GetInstance().Initialize();
    Sync::ProgressSync::GetInstance().Initialize();

    TransitionToState(SessionState::Connected);

    // Auto-grant soapstones so players can summon immediately
    Sync::PlayerSync::GetInstance().GrantSoapstones();

    LOG_INFO("Joined session. Local player ID: %llu", m_localPlayerId);
    return true;
}

void SessionManager::LeaveSession() {
    if (m_state == SessionState::Disconnected) return;

    LOG_INFO("Leaving session...");

    // Leave network session
    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.LeaveSession();

    // Clear players
    m_players.clear();
    m_playerMap.clear();

    TransitionToState(SessionState::Disconnected);

    LOG_INFO("Left session");
}

void SessionManager::Update(float deltaTime) {
    if (!IsActive()) return;

    // Update networking (receive packets, send heartbeats)
    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.Update();

    // Update player sync (reads game memory, broadcasts position/state)
    auto& playerSync = Sync::PlayerSync::GetInstance();
    playerSync.Update(deltaTime);
}

// ============================================================================
// Player management
// ============================================================================

void SessionManager::AddPlayer(uint64_t playerId, const std::string& name) {
    // Don't add ourselves
    if (playerId == m_localPlayerId) return;

    // Don't add duplicates
    if (m_playerMap.find(playerId) != m_playerMap.end()) {
        LOG_DEBUG("Player %llu already in session", playerId);
        return;
    }

    SessionPlayer newPlayer{};
    newPlayer.playerId = playerId;
    newPlayer.playerName = name;
    newPlayer.isAlive = true;
    newPlayer.isReady = true;
    newPlayer.soulLevel = 0;
    newPlayer.health = 0;
    newPlayer.maxHealth = 0;
    newPlayer.x = newPlayer.y = newPlayer.z = 0.0f;

    m_players.push_back(newPlayer);
    m_playerMap[playerId] = &m_players.back();

    LOG_INFO("Player joined session: %s (ID: %llu) [Total: %zu players]",
             name.c_str(), playerId, m_players.size());
}

void SessionManager::RemovePlayer(uint64_t playerId) {
    auto it = m_playerMap.find(playerId);
    if (it == m_playerMap.end()) return;

    std::string name = it->second->playerName;
    m_playerMap.erase(it);

    m_players.erase(
        std::remove_if(m_players.begin(), m_players.end(),
            [playerId](const SessionPlayer& p) { return p.playerId == playerId; }),
        m_players.end()
    );

    LOG_INFO("Player left session: %s (ID: %llu) [Total: %zu players]",
             name.c_str(), playerId, m_players.size());
}

SessionPlayer* SessionManager::GetPlayer(uint64_t playerId) {
    auto it = m_playerMap.find(playerId);
    return (it != m_playerMap.end()) ? it->second : nullptr;
}

SessionPlayer* SessionManager::GetLocalPlayer() {
    return GetPlayer(m_localPlayerId);
}

void SessionManager::UpdatePlayerPosition(uint64_t playerId, float x, float y, float z) {
    SessionPlayer* player = GetPlayer(playerId);
    if (player) {
        player->x = x;
        player->y = y;
        player->z = z;
    }
}

void SessionManager::UpdatePlayerHealth(uint64_t playerId, int32_t health, int32_t maxHealth) {
    SessionPlayer* player = GetPlayer(playerId);
    if (player) {
        player->health = health;
        player->maxHealth = maxHealth;
        player->isAlive = (health > 0);
    }
}

void SessionManager::NotifyPlayerDeath(uint64_t playerId) {
    SessionPlayer* player = GetPlayer(playerId);
    if (player) {
        player->isAlive = false;
        LOG_INFO("Player %s died", player->playerName.c_str());

        Network::PacketHeader deathPacket{};
        deathPacket.magic = 0x44533243;
        deathPacket.type = Network::PacketType::PlayerDeath;
        deathPacket.size = sizeof(Network::PacketHeader);
        deathPacket.timestamp = 0;

        auto& peerMgr = Network::PeerManager::GetInstance();
        peerMgr.BroadcastPacket(&deathPacket);
    }
}

void SessionManager::NotifyPlayerRespawn(uint64_t playerId) {
    SessionPlayer* player = GetPlayer(playerId);
    if (player) {
        player->isAlive = true;
        LOG_INFO("Player %s respawned", player->playerName.c_str());

        Network::PacketHeader respawnPacket{};
        respawnPacket.magic = 0x44533243;
        respawnPacket.type = Network::PacketType::PlayerRespawn;
        respawnPacket.size = sizeof(Network::PacketHeader);
        respawnPacket.timestamp = 0;

        auto& peerMgr = Network::PeerManager::GetInstance();
        peerMgr.BroadcastPacket(&respawnPacket);
    }
}

void SessionManager::OnBossDefeated(uint32_t bossId) {
    LOG_INFO("Boss %u defeated in session", bossId);

    Network::BossDefeatedPacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::BossDefeated;
    packet.header.size = sizeof(Network::BossDefeatedPacket);
    packet.bossId = bossId;
    packet.defeatTime = 0;

    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.BroadcastPacket(&packet.header);
}

void SessionManager::OnBonfireRested(uint32_t bonfireId) {
    LOG_INFO("Bonfire %u rested", bonfireId);

    Network::EventFlagPacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::BonfireRest;
    packet.header.size = sizeof(Network::EventFlagPacket);
    packet.flagId = bonfireId;
    packet.flagValue = true;

    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.BroadcastPacket(&packet.header);
}

void SessionManager::OnFogGateEntered(uint32_t fogGateId) {
    LOG_INFO("Fog gate %u entered", fogGateId);

    Network::EventFlagPacket packet{};
    packet.header.magic = 0x44533243;
    packet.header.type = Network::PacketType::FogGateTransition;
    packet.header.size = sizeof(Network::EventFlagPacket);
    packet.flagId = fogGateId;
    packet.flagValue = true;

    auto& peerMgr = Network::PeerManager::GetInstance();
    peerMgr.BroadcastPacket(&packet.header);
}

void SessionManager::PreventDisconnection() {
    m_preventDisconnect = true;
    LOG_INFO("Session disconnection prevention enabled");
}

void SessionManager::AllowDisconnection() {
    m_preventDisconnect = false;
    LOG_INFO("Session disconnection prevention disabled");
}

void SessionManager::TransitionToState(SessionState newState) {
    if (m_state == newState) return;
    LOG_INFO("Session state: %d -> %d", static_cast<int>(m_state), static_cast<int>(newState));
    m_state = newState;
}

void SessionManager::SynchronizePlayers() {
    // Driven by PlayerSync::Update now
}
