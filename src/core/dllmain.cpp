#include <Windows.h>
#include "../include/mod.h"
#include "../include/utils.h"

using namespace DS2Coop;
using namespace DS2Coop::Utils;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);

        Logger::GetInstance().Initialize(L"ds2_seamless_coop.log");

        LOG_INFO("========================================");
        LOG_INFO("DLL_PROCESS_ATTACH - DLL IS LOADING!");
        LOG_INFO("DS2 Seamless Co-op Mod v%s", MOD_VERSION);
        LOG_INFO("DLL Module Handle: 0x%p", hModule);
        LOG_INFO("Process ID: %lu", GetCurrentProcessId());
        LOG_INFO("========================================");

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            LOG_INFO("Mod initialization thread started...");
            Sleep(3000);
            LOG_INFO("Calling SeamlessCoopMod::Initialize()...");
            auto& mod = SeamlessCoopMod::GetInstance();
            if (mod.Initialize()) {
                LOG_INFO("========================================");
                LOG_INFO("MOD INITIALIZED SUCCESSFULLY!");
                LOG_INFO("Press INSERT in-game for co-op menu");
                LOG_INFO("========================================");
            } else {
                LOG_ERROR("========================================");
                LOG_ERROR("MOD INITIALIZATION FAILED!");
                LOG_ERROR("Check ds2_seamless_coop.log for details");
                LOG_ERROR("========================================");
            }
            return 0;
        }, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
        else LOG_ERROR("FATAL: CreateThread failed — mod will not initialize");

        LOG_INFO("DllMain returning TRUE (success)");
        break;
    }
        
    case DLL_PROCESS_DETACH:
        LOG_INFO("Shutting down mod...");
        SeamlessCoopMod::GetInstance().Shutdown();
        Logger::GetInstance().Shutdown();
        break;
    }
    return TRUE;
}

