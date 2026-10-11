#include "pch.h"
#include "MemoryUsage.h"
#include <psapi.h>

MemoryUsage::MemoryUsage()
    : TextModule("MemoryUsage", L"Memory Usage", L"Shows how much RAM Minecraft is using.", HUD) {
    addSetting("showSystem", L"Show System Usage", L"Also show how full your PC's total RAM is.", showSystem);
    this->prefix = TextValue(L"RAM: ");
}

std::wstringstream MemoryUsage::text(bool isDefault, bool inEditor) {
    // polling every frame is wasteful; twice a second is plenty
    auto now = std::chrono::steady_clock::now();
    if (now - lastPoll > std::chrono::milliseconds(500)) {
        lastPoll = now;

        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                    sizeof(pmc))) {
            processMB = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        }

        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) systemLoad = ms.dwMemoryLoad;
    }

    std::wstringstream wss;
    wss << std::format(L"{:.0f} MB", processMB);
    if (std::get<BoolValue>(showSystem)) wss << std::format(L" ({}% system)", systemLoad);
    return wss;
}
