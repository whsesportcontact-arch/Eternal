#pragma once
#include "../../Module.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

// Enta: makes mobs easier to hit by growing their client-side bounding boxes.
// Only runs in your own singleplayer worlds. On servers and Realms it restores every box and does nothing.
// Never touches players.
class HitboxExpand : public Module {
public:
    HitboxExpand();

    void onTick(Event& ev);
    void onEnable() override;
    void onDisable() override;

    enum Group : uint8_t { Hostile, Neutral, Passive, Boss, Projectile, Vehicle, Drop, Unlisted, Never };

private:
    struct Tracked {
        AABB original;  // the box the game gave the entity
        AABB written;   // the expanded box we wrote back
        float h = 0.f;  // expansion that produced `written`
        float v = 0.f;
    };

    Group classify(std::string const& id);
    bool groupEnabled(Group g);
    bool isLocalWorld();
    void restoreAll();
    void parseLists();

    ValueType horizontal = FloatValue(0.5f);
    ValueType vertical = FloatValue(0.25f);

    ValueType hostile = BoolValue(true);
    ValueType neutral = BoolValue(true);
    ValueType passive = BoolValue(true);
    ValueType bosses = BoolValue(true);
    ValueType projectiles = BoolValue(false);
    ValueType vehicles = BoolValue(false);
    ValueType drops = BoolValue(false);
    ValueType unlisted = BoolValue(true);

    ValueType include = TextValue(L"");
    ValueType exclude = TextValue(L"");

    std::unordered_map<SDK::Actor*, Tracked> tracked;
    std::unordered_map<std::string, Group> groupCache;
    std::unordered_set<std::string> includeSet, excludeSet;
    std::wstring lastInclude, lastExclude;

    int tickCounter = 0;
    bool allowed = false;
    bool reported = false;
    std::string reportedHost;
};
