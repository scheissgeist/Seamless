#include "address_resolver.h"
#include "addresses.h"
#include "pattern_scanner.h"
#include "utils.h"

namespace DS2Coop {

bool AddressResolver::Initialize() {
    using namespace Utils;
    using namespace Addresses;
    
    Logger::GetInstance().LogInfo("=== Address Resolution Starting ===");
    
    // Find GameManagerImp
    Logger::GetInstance().LogInfo("Searching for GameManagerImp...");
    uintptr_t gm_pattern_addr = PatternScanner::FindPattern(
        GAME_MANAGER_IMP.pattern,
        GAME_MANAGER_IMP.mask,
        nullptr  // Search in main module (DarkSoulsII.exe)
    );
    
    if (gm_pattern_addr) {
        // Resolve RIP-relative address
        uintptr_t gm_ptr_addr = PatternScanner::ResolveRIP(
            gm_pattern_addr,
            GAME_MANAGER_IMP.offset_from_match,
            GAME_MANAGER_IMP.pointer_offset
        );
        
        // Read the pointer
        if (PatternScanner::ReadPointer(gm_ptr_addr, m_game_manager_imp)) {
            Logger::GetInstance().LogInfo("GameManagerImp resolved: 0x%p -> 0x%p",
                                           reinterpret_cast<void*>(gm_ptr_addr),
                                           reinterpret_cast<void*>(m_game_manager_imp));
        } else {
            Logger::GetInstance().LogError("Failed to read GameManagerImp pointer!");
        }
    } else {
        Logger::GetInstance().LogError("GameManagerImp pattern not found!");
    }
    
    // Find NetSessionManager
    Logger::GetInstance().LogInfo("Searching for NetSessionManager...");
    uintptr_t ns_pattern_addr = PatternScanner::FindPattern(
        NET_SESSION_MANAGER.pattern,
        NET_SESSION_MANAGER.mask,
        nullptr
    );
    
    if (ns_pattern_addr) {
        uintptr_t ns_ptr_addr = PatternScanner::ResolveRIP(
            ns_pattern_addr,
            NET_SESSION_MANAGER.offset_from_match,
            NET_SESSION_MANAGER.pointer_offset
        );
        
        if (PatternScanner::ReadPointer(ns_ptr_addr, m_net_session_manager)) {
            Logger::GetInstance().LogInfo("NetSessionManager resolved: 0x%p -> 0x%p",
                                           reinterpret_cast<void*>(ns_ptr_addr),
                                           reinterpret_cast<void*>(m_net_session_manager));
        } else {
            Logger::GetInstance().LogError("Failed to read NetSessionManager pointer!");
        }
    } else {
        Logger::GetInstance().LogError("NetSessionManager pattern not found!");
    }
    
    // Find KatanaMainApp (optional, not critical)
    Logger::GetInstance().LogInfo("Searching for KatanaMainApp...");
    uintptr_t ka_pattern_addr = PatternScanner::FindPattern(
        KATANA_MAIN_APP.pattern,
        KATANA_MAIN_APP.mask,
        nullptr
    );
    
    if (ka_pattern_addr) {
        uintptr_t ka_ptr_addr = PatternScanner::ResolveRIP(
            ka_pattern_addr,
            KATANA_MAIN_APP.offset_from_match,
            KATANA_MAIN_APP.pointer_offset
        );
        
        if (PatternScanner::ReadPointer(ka_ptr_addr, m_katana_main_app)) {
            Logger::GetInstance().LogInfo("KatanaMainApp resolved: 0x%p -> 0x%p",
                                           reinterpret_cast<void*>(ka_ptr_addr),
                                           reinterpret_cast<void*>(m_katana_main_app));
        }
    }
    
    Logger::GetInstance().LogInfo("=== Address Resolution Complete ===");
    Logger::GetInstance().LogInfo("Results:");
    Logger::GetInstance().LogInfo("  GameManagerImp:     0x%p %s",
                                   reinterpret_cast<void*>(m_game_manager_imp),
                                   m_game_manager_imp ? "[OK]" : "[FAILED]");
    Logger::GetInstance().LogInfo("  NetSessionManager:  0x%p %s",
                                   reinterpret_cast<void*>(m_net_session_manager),
                                   m_net_session_manager ? "[OK]" : "[FAILED]");
    Logger::GetInstance().LogInfo("  KatanaMainApp:      0x%p %s",
                                   reinterpret_cast<void*>(m_katana_main_app),
                                   m_katana_main_app ? "[OK]" : "[OPTIONAL]");
    
    return IsValid();
}

} // namespace DS2Coop

