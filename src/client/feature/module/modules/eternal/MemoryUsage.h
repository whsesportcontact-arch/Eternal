#pragma once
#include "../../TextModule.h"

// Eternal: how much RAM Minecraft is using.
class MemoryUsage : public TextModule {
public:
    MemoryUsage();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType showSystem = BoolValue(false);

    std::chrono::steady_clock::time_point lastPoll{};
    double processMB = 0.0;
    unsigned long systemLoad = 0;
};
