#pragma once
#include "../../Module.h"

// Enta: hidden helper that talks to EntaLauncher through files in %LOCALAPPDATA%\Enta\Launcher:
//   modules.tsv           written here: every module, its name and whether it's on (for the launcher's mod list)
//   module_overrides.tsv  written by the launcher: modules you switched on/off before launching; applied once
//                         at startup, then deleted
//   skin.png              written here: your current skin, for the launcher's 3D preview
class LauncherBridge : public Module {
public:
    LauncherBridge();

    bool shouldPersistEnabled() override { return false; }
    bool isToggleable() override { return false; }

    void onTick(Event& ev);

    // call right after the saved config has been applied to modules
    static void onConfigApplied();
    static void writeManifest();

private:
    static std::filesystem::path folder();
    static bool applyOverrides();
    void exportSkin(SDK::Level* level);

    int tickCounter = 0;
    uint64_t lastSkinHash = 0;
};
