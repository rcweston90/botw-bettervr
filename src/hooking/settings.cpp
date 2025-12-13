#include "cemu_hooks.h"
#include "instance.h"
#include "hooking/entity_debugger.h"

std::mutex g_settingsMutex;
data_VRSettingsIn g_settings = {};

uint64_t CemuHooks::s_memoryBaseAddress = 0;
std::atomic_uint32_t CemuHooks::s_framesSinceLastCameraUpdate = 0;

void CemuHooks::hook_UpdateSettings(PPCInterpreter_t* hCPU) {
    // Log::print("Updated settings!");
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t ppc_settingsOffset = hCPU->gpr[5];
    uint32_t ppc_tableOfCutsceneEventSettings = hCPU->gpr[6];
    data_VRSettingsIn settings = {};

    if (auto& debugger = VRManager::instance().Hooks->m_entityDebugger) {
        debugger->UpdateEntityMemory();
    }

    readMemory(ppc_settingsOffset, &settings);

    std::lock_guard lock(g_settingsMutex);
    g_settings = settings;
    ++s_framesSinceLastCameraUpdate;

    static bool logSettings = true;
    if (logSettings) {
        Log::print<INFO>("VR Settings:\n{}", g_settings.ToString());
        logSettings = false;
    }

    initCutsceneDefaultSettings(ppc_tableOfCutsceneEventSettings);
}

data_VRSettingsIn CemuHooks::GetSettings() {
    std::lock_guard lock(g_settingsMutex);
    return g_settings;
}


void CemuHooks::hook_OSReportToConsole(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t strPtr = hCPU->gpr[3];
    if (strPtr == 0) {
        return;
    }
    char* str = (char*)(s_memoryBaseAddress + strPtr);
    if (str == nullptr) {
        return;
    }
    if (str[0] != '\0') {
        Log::print<PPC>(str);
    }
}