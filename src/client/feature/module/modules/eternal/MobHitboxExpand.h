#pragma once
#include "../../Module.h"
#include <mutex>
#include <unordered_set>

// Enta (experimental): makes chosen mobs' hitboxes bigger on your screen so you can hit them over
// blocks, for farms in your OWN singleplayer world. It shuts itself off on Realms, servers, and any
// world with other players in it, and puts every hitbox back to normal when it does.
class MobHitboxExpand : public Module {
public:
    MobHitboxExpand();

    void onTick(Event& ev);
    void onUpdate(Event& ev);
    void onLeaveGame(Event& ev);
    void onDisable() override;

private:
    // returns an empty string when this is your own solo world, otherwise why it isn't
    std::string notSoloReason(SDK::Level* level);
    bool isSelected(uint32_t entityTypeId);
    void apply(bool expand);

    ValueType extraWidth = FloatValue(0.3f);
    ValueType extraHeight = FloatValue(1.f);

    struct MobChoice {
        uint32_t legacyId; // low byte of the actor type id
        const char* key;
        const wchar_t* name;
        ValueType enabled;
    };
    std::vector<MobChoice> mobs;

    bool allowed = false;
    bool checkedOnce = false;
    std::string lastReason;
    int tickCounter = 0;
    std::unordered_set<uint64_t> expanded; // runtime ids we changed
    std::mutex lock;
};
