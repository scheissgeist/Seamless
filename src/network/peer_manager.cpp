// Peer-to-peer networking for seamless co-op
//
// UDP-based P2P with handshake, heartbeat, and timeout.
// Host creates a session with a password.
// Client sends handshake with password -> host validates -> sends response.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include "../../include/network.h"
#include "../../include/utils.h"
#include <chrono>
#include <algorithm>

using namespace DS2Coop::Network;
using namespace DS2Coop::Utils;

constexpr uint32_t PACKET_MAGIC = 0x44533243; // 'DS2C'
constexpr uint64_t HEARTBEAT_INTERVAL_MS = 5000;
constexpr uint64_t TIMEOUT_DURATION_MS = 15000;

static uint64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

PeerManager& PeerManager::GetInstance() {
    static PeerManager instance;
    return instance;
}

bool PeerManager::Initialize(uint16_t port) {
    if (m_initialized) return true;

    LOG_INFO("Initializing peer manager on port %u...", port);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("Failed to initialize Winsock");
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create socket: %d", WSAGetLastError());
        WSACleanup();
        return false;
    }

    // Non-blocking mode
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        LOG_ERROR("Failed to set non-blocking mode");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to bind socket on port %u: %d", port, WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return false;
    }

    m_socket = reinterpret_cast<void*>(sock);
    m_port = port;
    m_localPlayerId = static_cast<uint64_t>(GetCurrentProcessId()) ^ NowMs();
    m_initialized = true;

    LOG_INFO("Peer manager initialized, local ID: %llu, port: %u", m_localPlayerId, port);
    return true;
}

void PeerManager::Shutdown() {
    if (!m_initialized) return;

    LOG_INFO("Shutting down peer manager...");

    LeaveSession();

    if (m_socket) {
        closesocket(reinterpret_cast<SOCKET>(m_socket));
        m_socket = nullptr;
    }

    WSACleanup();
    m_initialized = false;
}

bool PeerManager::CreateSession(const std::string& password) {
    if (!m_initialized) return false;

    LOG_INFO("Creating session with password...");

    m_isHost = true;
    m_connected = true;
    m_sessionPassword = password;

    LOG_INFO("Session created, waiting for peers on port %u...", m_port);
    return true;
}

bool PeerManager::JoinSession(const std::string& address, uint16_t port, const std::string& password) {
    if (!m_initialized) return false;

    LOG_INFO("Joining session at %s:%u...", address.c_str(), port);

    m_sessionPassword = password;

    // Build handshake packet
    HandshakePacket handshake{};
    handshake.header.magic = PACKET_MAGIC;
    handshake.header.type = PacketType::Handshake;
    handshake.header.size = sizeof(HandshakePacket);
    handshake.header.sequence = 0;
    handshake.header.timestamp = NowMs();
    handshake.version = 1;
    handshake.playerId = m_localPlayerId;
    strncpy_s(handshake.playerName, "Player", sizeof(handshake.playerName));
    strncpy_s(handshake.password, password.c_str(), sizeof(handshake.password));

    // Send to host
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &hostAddr.sin_addr);

    SOCKET sock = reinterpret_cast<SOCKET>(m_socket);
    if (sendto(sock, reinterpret_cast<const char*>(&handshake), sizeof(handshake), 0,
               reinterpret_cast<sockaddr*>(&hostAddr), sizeof(hostAddr)) == SOCKET_ERROR) {
        LOG_ERROR("Failed to send handshake: %d", WSAGetLastError());
        return false;
    }

    // Register host as a peer
    PeerInfo hostPeer{};
    hostPeer.playerId = 0; // Will be set when host responds
    hostPeer.playerName = "Host";
    hostPeer.address = hostAddr.sin_addr.s_addr;
    hostPeer.port = port;
    hostPeer.lastHeartbeat = NowMs();
    hostPeer.connected = true;
    m_peers.push_back(hostPeer);

    m_isHost = false;
    m_connected = true;

    LOG_INFO("Handshake sent, waiting for response...");
    return true;
}

void PeerManager::LeaveSession() {
    if (!m_connected) return;

    LOG_INFO("Leaving session...");

    // Send disconnect to all peers
    PacketHeader disconnectPacket{};
    disconnectPacket.magic = PACKET_MAGIC;
    disconnectPacket.type = PacketType::Disconnect;
    disconnectPacket.size = sizeof(PacketHeader);
    disconnectPacket.sequence = 0;
    disconnectPacket.timestamp = NowMs();

    BroadcastPacket(&disconnectPacket);

    m_peers.clear();
    m_connected = false;
    m_isHost = false;
    m_sessionPassword.clear();

    LOG_INFO("Session left");
}

void PeerManager::Update() {
    if (!m_initialized || !m_connected) return;

    HandleIncomingPackets();
    SendHeartbeats();
    CheckTimeouts();
}

bool PeerManager::SendPacket(const PacketHeader* packet, uint64_t targetPlayerId) {
    if (!m_initialized || !m_connected || !packet) return false;

    SOCKET sock = reinterpret_cast<SOCKET>(m_socket);

    for (const auto& peer : m_peers) {
        if ((targetPlayerId == 0 || peer.playerId == targetPlayerId) && peer.connected) {
            sockaddr_in peerAddr{};
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_addr.s_addr = peer.address;
            peerAddr.sin_port = htons(peer.port);

            sendto(sock, reinterpret_cast<const char*>(packet), packet->size, 0,
                   reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));

            if (targetPlayerId != 0) return true;
        }
    }

    return true;
}

void PeerManager::BroadcastPacket(const PacketHeader* packet) {
    if (!m_initialized || !m_connected || !packet) return;

    SOCKET sock = reinterpret_cast<SOCKET>(m_socket);

    for (const auto& peer : m_peers) {
        if (peer.connected) {
            sockaddr_in peerAddr{};
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_addr.s_addr = peer.address;
            peerAddr.sin_port = htons(peer.port);

            sendto(sock, reinterpret_cast<const char*>(packet), packet->size, 0,
                   reinterpret_cast<sockaddr*>(&peerAddr), sizeof(peerAddr));
        }
    }
}

void PeerManager::HandleIncomingPackets() {
    SOCKET sock = reinterpret_cast<SOCKET>(m_socket);
    char buffer[8192];
    sockaddr_in senderAddr{};
    int senderAddrLen = sizeof(senderAddr);

    while (true) {
        int received = recvfrom(sock, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&senderAddr), &senderAddrLen);

        if (received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                LOG_ERROR("Socket error: %d", error);
            }
            break;
        }

        if (received < static_cast<int>(sizeof(PacketHeader))) continue;

        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
        if (header->magic != PACKET_MAGIC) continue;

        // Handle handshake specially — it contains the peer identity
        if (header->type == PacketType::Handshake && received >= static_cast<int>(sizeof(HandshakePacket))) {
            HandshakePacket* hs = reinterpret_cast<HandshakePacket*>(buffer);
            HandleHandshakePacket(hs, senderAddr);
            continue;
        }

        // For other packets, update the peer's heartbeat
        for (auto& peer : m_peers) {
            if (peer.address == senderAddr.sin_addr.s_addr) {
                peer.lastHeartbeat = NowMs();
                break;
            }
        }

        // Build PeerInfo for packet handler
        PeerInfo senderInfo{};
        senderInfo.address = senderAddr.sin_addr.s_addr;
        senderInfo.port = ntohs(senderAddr.sin_port);
        senderInfo.lastHeartbeat = NowMs();
        senderInfo.connected = true;

        // Find player ID
        for (const auto& peer : m_peers) {
            if (peer.address == senderAddr.sin_addr.s_addr) {
                senderInfo.playerId = peer.playerId;
                senderInfo.playerName = peer.playerName;
                break;
            }
        }

        PacketHandler::GetInstance().HandlePacket(header, senderInfo);
    }
}

void PeerManager::HandleHandshakePacket(const HandshakePacket* hs, const sockaddr_in& senderAddr) {
    LOG_INFO("Received handshake from %s (ID: %llu)", hs->playerName, hs->playerId);

    // Version check
    if (hs->version != 1) {
        LOG_ERROR("Version mismatch from peer: expected 1, got %u", hs->version);
        return;
    }

    if (m_isHost) {
        // Host: validate password and accept/reject
        if (m_sessionPassword != hs->password) {
            LOG_WARNING("Peer rejected: wrong password");
            // Send rejection (a disconnect packet)
            PacketHeader reject{};
            reject.magic = PACKET_MAGIC;
            reject.type = PacketType::Disconnect;
            reject.size = sizeof(PacketHeader);
            reject.timestamp = NowMs();

            SOCKET sock = reinterpret_cast<SOCKET>(m_socket);
            sendto(sock, reinterpret_cast<const char*>(&reject), sizeof(reject), 0,
                   reinterpret_cast<const sockaddr*>(&senderAddr), sizeof(senderAddr));
            return;
        }

        // Password matches - register peer
        bool alreadyKnown = false;
        for (auto& peer : m_peers) {
            if (peer.playerId == hs->playerId) {
                peer.lastHeartbeat = NowMs();
                peer.connected = true;
                alreadyKnown = true;
                break;
            }
        }

        if (!alreadyKnown) {
            PeerInfo newPeer{};
            newPeer.playerId = hs->playerId;
            newPeer.playerName = hs->playerName;
            newPeer.address = senderAddr.sin_addr.s_addr;
            newPeer.port = ntohs(senderAddr.sin_port);
            newPeer.lastHeartbeat = NowMs();
            newPeer.connected = true;
            m_peers.push_back(newPeer);

            LOG_INFO("Peer accepted: %s (ID: %llu) from port %u",
                     hs->playerName, hs->playerId, newPeer.port);
        }

        // Send handshake response back so the joiner knows we accepted
        HandshakePacket response{};
        response.header.magic = PACKET_MAGIC;
        response.header.type = PacketType::Handshake;
        response.header.size = sizeof(HandshakePacket);
        response.header.sequence = 1; // Sequence 1 = response
        response.header.timestamp = NowMs();
        response.version = 1;
        response.playerId = m_localPlayerId;
        strncpy_s(response.playerName, "Host", sizeof(response.playerName));
        strncpy_s(response.password, m_sessionPassword.c_str(), sizeof(response.password));

        SOCKET sock = reinterpret_cast<SOCKET>(m_socket);
        sendto(sock, reinterpret_cast<const char*>(&response), sizeof(response), 0,
               reinterpret_cast<const sockaddr*>(&senderAddr), sizeof(senderAddr));

        LOG_INFO("Sent handshake response to %s", hs->playerName);
    } else {
        // Client: this is the host's response to our handshake
        LOG_INFO("Received handshake response from host (ID: %llu)", hs->playerId);

        // Update the host peer entry with the real player ID
        for (auto& peer : m_peers) {
            if (peer.address == senderAddr.sin_addr.s_addr) {
                peer.playerId = hs->playerId;
                peer.playerName = hs->playerName;
                peer.lastHeartbeat = NowMs();
                peer.connected = true;
                break;
            }
        }

        LOG_INFO("Connected to host: %s", hs->playerName);
    }

    // Forward to packet handler for session manager integration
    PeerInfo senderInfo{};
    senderInfo.playerId = hs->playerId;
    senderInfo.playerName = hs->playerName;
    senderInfo.address = senderAddr.sin_addr.s_addr;
    senderInfo.port = ntohs(senderAddr.sin_port);
    senderInfo.lastHeartbeat = NowMs();
    senderInfo.connected = true;

    PacketHandler::GetInstance().HandlePacket(&hs->header, senderInfo);
}

void PeerManager::SendHeartbeats() {
    uint64_t now = NowMs();

    if (now - m_lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;

    PacketHeader heartbeat{};
    heartbeat.magic = PACKET_MAGIC;
    heartbeat.type = PacketType::Heartbeat;
    heartbeat.size = sizeof(PacketHeader);
    heartbeat.sequence = 0;
    heartbeat.timestamp = now;

    BroadcastPacket(&heartbeat);
    m_lastHeartbeatMs = now;
}

void PeerManager::CheckTimeouts() {
    uint64_t now = NowMs();

    for (auto& peer : m_peers) {
        if (peer.connected && (now - peer.lastHeartbeat > TIMEOUT_DURATION_MS)) {
            LOG_WARNING("Peer %s (ID: %llu) timed out", peer.playerName.c_str(), peer.playerId);
            peer.connected = false;
        }
    }
}
