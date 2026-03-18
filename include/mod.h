#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

namespace DS2Coop {

// Version information
constexpr const char* MOD_VERSION = "1.0.0";
constexpr const char* MOD_NAME = "Dark Souls 2 Seamless Co-op";

// Game version support
enum class GameVersion {
    Unknown,
    SteamLatest,      // Latest Steam version
    CalibrationVer112 // Calibration 1.12
};

// Configuration
struct ModConfig {
    bool enabled = true;
    bool debug_logging = false;
    uint16_t max_players = 4;
    uint16_t port = 27015;
    bool allow_invasions = false;
    bool sync_bonfires = true;
    bool sync_items = true;
    bool sync_enemies = false;
    // Custom server redirect
    std::string server_ip = "127.0.0.1";    // IP of the ds3os custom server
    uint16_t server_port = 50031;            // Login port of custom server
    bool use_custom_server = true;           // Enable server redirect
};

// Main mod class
class SeamlessCoopMod {
public:
    static SeamlessCoopMod& GetInstance();
    
    bool Initialize();
    void Shutdown();
    
    bool IsInitialized() const { return m_initialized; }
    GameVersion GetGameVersion() const { return m_gameVersion; }
    const ModConfig& GetConfig() const { return m_config; }
    
    void LoadConfig();
    void SaveConfig();

private:
    SeamlessCoopMod() = default;
    ~SeamlessCoopMod() = default;
    SeamlessCoopMod(const SeamlessCoopMod&) = delete;
    SeamlessCoopMod& operator=(const SeamlessCoopMod&) = delete;
    
    bool DetectGameVersion();
    bool InstallHooks();
    void UninstallHooks();
    
    bool m_initialized = false;
    GameVersion m_gameVersion = GameVersion::Unknown;
    ModConfig m_config;
    HANDLE m_updateThread = nullptr;
};

} // namespace DS2Coop

