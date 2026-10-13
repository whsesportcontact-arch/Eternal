#pragma once
#include "../../Module.h"

// Enta: hidden helper that talks to EntaLauncher through files in %LOCALAPPDATA%\Enta\Launcher:
//   modules.tsv           written here: every module, its name and whether it's on (for the launcher's mod list)
//   module_overrides.tsv  written by the launcher: modules you switched on/off before launching; applied once
//                         at startup, then deleted
//   skin.png              written here: your current skin, for the launcher's 3D preview
//   pending_skin.txt      written by the launcher: a skin file you chose there; the next time you press
//                         "Choose New Skin" in the Dressing Room, Enta picks this file for you
class LauncherBridge : public Module {
public:
    LauncherBridge();

    bool shouldPersistEnabled() override { return false; }
    bool isToggleable() override { return false; }

    void onTick(Event& ev);
    void onUpdate(Event& ev);

    // call right after the saved config has been applied to modules
    static void onConfigApplied();
    static void writeManifest();

    // The launcher's chosen skin file, if there is one; consumes it.
    static std::optional<std::string> takePendingSkin();

private:
    static std::filesystem::path folder();
    static bool applyOverrides();
    void exportSkin(SDK::Level* level);

    bool toldAboutSkin = false;
    int tickCounter = 0;
    int frameCounter = 0;
    uint64_t lastSkinHash = 0;
};
