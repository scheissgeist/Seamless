#include "pattern_scanner.h"
#include "utils.h"
#include <Psapi.h>
#include <vector>

#pragma comment(lib, "Psapi.lib")

namespace DS2Coop {
namespace Utils {

bool PatternScanner::ComparePattern(const uint8_t* data, const char* pattern, const char* mask) {
    for (; *mask; ++mask, ++data, ++pattern) {
        if (*mask == 'x' && *data != static_cast<uint8_t>(*pattern)) {
            return false;
        }
    }
    return true;
}

bool PatternScanner::GetModuleInfo(const char* module_name, uintptr_t& base, size_t& size) {
    HMODULE module = nullptr;
    
    if (module_name == nullptr) {
        module = GetModuleHandleA(NULL);
    } else {
        module = GetModuleHandleA(module_name);
    }
    
    if (!module) {
        return false;
    }
    
    MODULEINFO mod_info = {};
    if (!GetModuleInformation(GetCurrentProcess(), module, &mod_info, sizeof(MODULEINFO))) {
        return false;
    }
    
    base = reinterpret_cast<uintptr_t>(mod_info.lpBaseOfDll);
    size = static_cast<size_t>(mod_info.SizeOfImage);
    
    return true;
}

uintptr_t PatternScanner::FindPattern(const char* pattern, const char* mask, const char* module_name) {
    uintptr_t base = 0;
    size_t size = 0;
    
    if (!GetModuleInfo(module_name, base, size)) {
        Logger::GetInstance().LogError("PatternScanner: Failed to get module info");
        return 0;
    }
    
    const auto mask_length = strlen(mask);
    const auto* scan_bytes = reinterpret_cast<const uint8_t*>(pattern);
    
    Logger::GetInstance().LogInfo("Scanning for pattern in module (base: 0x%p, size: 0x%zX)", 
                                   reinterpret_cast<void*>(base), size);
    
    for (size_t i = 0; i < size - mask_length; ++i) {
        const auto* current = reinterpret_cast<const uint8_t*>(base + i);
        
        if (ComparePattern(current, pattern, mask)) {
            uintptr_t result = base + i;
            Logger::GetInstance().LogInfo("Pattern found at: 0x%p (offset: 0x%zX)", 
                                           reinterpret_cast<void*>(result), i);
            return result;
        }
    }
    
    Logger::GetInstance().LogError("Pattern not found!");
    return 0;
}

std::vector<uintptr_t> PatternScanner::FindPatternAll(const char* pattern, const char* mask, const char* module_name) {
    std::vector<uintptr_t> results;
    
    uintptr_t base = 0;
    size_t size = 0;
    
    if (!GetModuleInfo(module_name, base, size)) {
        Logger::GetInstance().LogError("PatternScanner: Failed to get module info");
        return results;
    }
    
    const auto mask_length = strlen(mask);
    const auto* scan_bytes = reinterpret_cast<const uint8_t*>(pattern);
    
    for (size_t i = 0; i < size - mask_length; ++i) {
        const auto* current = reinterpret_cast<const uint8_t*>(base + i);
        
        if (ComparePattern(current, pattern, mask)) {
            results.push_back(base + i);
        }
    }
    
    Logger::GetInstance().LogInfo("Found %zu matches for pattern", results.size());
    return results;
}

uintptr_t PatternScanner::ResolveRIP(uintptr_t instruction_addr, int offset, int instruction_size) {
    // Read the RIP-relative offset (4 bytes)
    int32_t rip_offset = *reinterpret_cast<int32_t*>(instruction_addr + offset);
    
    // Calculate absolute address:
    // RIP (after instruction) + offset
    uintptr_t rip = instruction_addr + instruction_size;
    uintptr_t absolute_addr = rip + rip_offset;
    
    Logger::GetInstance().LogInfo("RIP resolution: instruction=0x%p, offset=%d, size=%d -> absolute=0x%p",
                                   reinterpret_cast<void*>(instruction_addr),
                                   offset,
                                   instruction_size,
                                   reinterpret_cast<void*>(absolute_addr));
    
    return absolute_addr;
}

bool PatternScanner::ReadPointer(uintptr_t address, uintptr_t& out_value) {
    __try {
        out_value = *reinterpret_cast<uintptr_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Logger::GetInstance().LogError("Failed to read pointer at 0x%p", reinterpret_cast<void*>(address));
        return false;
    }
}

} // namespace Utils
} // namespace DS2Coop

