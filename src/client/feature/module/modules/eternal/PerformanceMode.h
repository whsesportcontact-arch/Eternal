#pragma once
#include "../../Module.h"

// Enta: one switch for crowded places like spawn. Turns off the Enta modules that cost the most
// (and optionally your whole HUD), then turns exactly those back on when you switch it off.
class PerformanceMode : public Module {
public:
    PerformanceMode();

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& ev);

    // always starts off; a crash or quit while on is fixed by the restore file instead
    bool shouldPersistEnabled() override { return false; }

private:
    std::vector<std::string> modulesToPause();
    void saveRestoreList();
    void restore();
    void restoreLeftover();

    ValueType pauseHeavy = BoolValue(true);
    ValueType pauseHud = BoolValue(false);

    std::vector<std::string> paused; // internal names we turned off
    bool checkedLeftover = false;
};
