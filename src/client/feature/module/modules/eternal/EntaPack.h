#pragma once
#include "../../Module.h"
#include <future>
#include <atomic>

// Enta: builds an "Enta Pack" resource pack from the game's own vanilla files, using your settings:
// low fire, low shield, smaller totem pop and fewer particles for more FPS.
// The pack is written to com.mojang/development_resource_packs, so you only have to turn it on
// once in Settings > Global Resources. After that, changing a setting here rebuilds it.
class EntaPack : public Module {
public:
    EntaPack();
    ~EntaPack() override;

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& ev);

    // keep getting updates while a build is still queued, even after being turned off
    bool shouldListen() override { return isEnabled() || queued; }

    struct Options {
        bool active = true;
        bool lowFire = true;
        float screenFireHeight = 30.f;
        float groundFireHeight = 50.f;
        bool lowShield = true;
        float shieldOffset = 3.f;
        bool smallTotem = true;
        float totemSize = 60.f;
        float combatParticles = 40.f;
        float ambientParticles = 50.f;

        [[nodiscard]] std::string signature() const;
    };

private:
    Options snapshot(bool active);
    void markDirty();
    void startBuild(bool active, bool force);
    static void build(Options opts, bool force);
    static void openPackFolder();

    ValueType lowFire = BoolValue(true);
    ValueType screenFireHeight = FloatValue(30.f);
    ValueType groundFireHeight = FloatValue(50.f);
    ValueType lowShield = BoolValue(true);
    ValueType shieldOffset = FloatValue(3.f);
    ValueType smallTotem = BoolValue(true);
    ValueType totemSize = FloatValue(60.f);
    ValueType combatParticles = FloatValue(40.f);
    ValueType ambientParticles = FloatValue(50.f);

    std::future<void> job;
    std::atomic_bool dirty = false;
    std::atomic_bool queuedForce = false;
    std::chrono::steady_clock::time_point lastChange {};
    bool queued = false;
    bool queuedActive = true;
};
