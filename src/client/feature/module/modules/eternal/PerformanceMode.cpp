#include "pch.h"
#include "PerformanceMode.h"
#include "client/Latite.h"
#include "client/feature/module/ModuleManager.h"
#include "client/misc/ClientMessageQueue.h"
#include "util/Util.h"

namespace {
    constexpr const char* kSect = "\xC2\xA7";

    // Modules that do work every frame or tick scaling with what's around you.
    constexpr std::array kHeavyModules = {
        "ChestOutlines", // scans nearby blocks
        "Hitboxes",      // draws a box for every entity, a lot at spawn
        "ChunkBorders",  // extra world geometry
        "WAILA",         // block/entity lookups every frame
        "ShulkerPreview",
    };

    std::filesystem::path restoreFile() {
        return util::GetLatitePath() / "performance_mode_restore.txt";
    }
}

PerformanceMode::PerformanceMode()
    : Module("PerformanceMode", L"Performance Mode",
             L"One switch for crowded places like spawn: turns off the Enta modules that cost the most FPS, then "
             L"turns them back on when you switch it off. Give it a keybind to flip it quickly.",
             GAME) {
    addSetting("pauseHeavy", L"Turn Off Heavy Modules",
               L"Chest Outlines, Hitboxes, Chunk Borders, WAILA and Shulker Preview.", pauseHeavy);
    addSetting("pauseHud", L"Turn Off HUD Modules",
               L"Also hide your HUD modules (except Lag Check) for the most FPS.", pauseHud);

    // also runs while off, so leftovers from a crash can be restored
    listen<UpdateEvent>(static_cast<EventListenerFunc>(&PerformanceMode::onUpdate), true);
}

std::vector<std::string> PerformanceMode::modulesToPause() {
    std::vector<std::string> out;
    const bool heavy = std::get<BoolValue>(pauseHeavy);
    const bool hud = std::get<BoolValue>(pauseHud);

    Latite::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (!mod || mod.get() == this || !mod->isEnabled()) return;
        const std::string name = mod->name();
        if (name == "LagCheck") return;

        bool isHeavy = std::find(kHeavyModules.begin(), kHeavyModules.end(), name) != kHeavyModules.end();
        if ((heavy && isHeavy) || (hud && mod->isHud())) out.push_back(name);
    });
    return out;
}

void PerformanceMode::saveRestoreList() {
    std::error_code ec;
    if (paused.empty()) {
        std::filesystem::remove(restoreFile(), ec);
        return;
    }
    std::filesystem::create_directories(restoreFile().parent_path(), ec);
    std::ofstream f(restoreFile(), std::ios::trunc);
    for (auto& name : paused) f << name << "\n";
}

void PerformanceMode::onEnable() {
    restoreLeftover();
    paused = modulesToPause();
    for (auto& name : paused) {
        if (auto mod = Latite::getModuleManager().find(name)) mod->setEnabled(false);
    }
    // written to disk so a crash or closing the game doesn't leave these off forever
    saveRestoreList();

    Latite::getClientMessageQueue().push(std::format("{}d[Enta]{}r Performance Mode {}aON{}r, paused {} module{}.", kSect,
                                                     kSect, kSect, kSect, paused.size(),
                                                     paused.size() == 1 ? "" : "s"));
}

void PerformanceMode::restore() {
    for (auto& name : paused) {
        if (auto mod = Latite::getModuleManager().find(name)) mod->setEnabled(true);
    }
    paused.clear();
    saveRestoreList();
}

void PerformanceMode::onDisable() {
    restore();
    Latite::getClientMessageQueue().push(
        std::format("{}d[Enta]{}r Performance Mode {}cOFF{}r, your modules are back.", kSect, kSect, kSect, kSect));
}

void PerformanceMode::onUpdate(Event&) {
    restoreLeftover();
}

// If the game closed (or crashed) while Performance Mode was on, turn those modules back on.
// Runs once, on the first frame, after every module has loaded its config.
void PerformanceMode::restoreLeftover() {
    if (checkedLeftover) return;
    checkedLeftover = true;

    std::ifstream f(restoreFile());
    if (!f) return;

    std::vector<std::string> leftover;
    for (std::string line; std::getline(f, line);) {
        if (!line.empty()) leftover.push_back(line);
    }
    f.close();

    paused = leftover;
    restore();
}
