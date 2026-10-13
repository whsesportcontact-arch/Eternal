#pragma once
#include "../../Module.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Enta: makes mobs easier to hit by growing their client-side bounding boxes.
// Every entity has its own on/off toggle, grouped under a category toggle.
// Only runs in your own singleplayer worlds. On servers and Realms it restores every box and does nothing.
// Never touches players.
class HitboxExpand : public Module {
public:
    HitboxExpand();

    void onTick(Event& ev);
    void onEnable() override;

    enum Group : uint8_t { Hostile, Neutral, Passive, Boss, Projectile, Vehicle, Drop, Unlisted, Never };

private:
    struct Tracked {
        AABB original;  // the box the game gave the entity
        AABB written;   // the expanded box we wrote back
        float h = 0.f;  // expansion that produced `written`
        float v = 0.f;
    };

    struct Lookup {
        Group group = Unlisted;
        int toggle = -1; // index into entityToggles, -1 = no per-entity toggle
    };

    Lookup classify(std::string const& id);
    bool groupEnabled(Group g);
    bool isLocalWorld();
    void restoreAll();
    void parseIncludeList();

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

    // One toggle per entity. Reserved up front and never resized: settings keep pointers into it.
    std::vector<ValueType> entityToggles;
    std::unordered_map<std::string, Lookup> idLookup;

    std::unordered_map<SDK::Actor*, Tracked> tracked;
    std::unordered_set<std::string> includeSet;
    std::wstring lastInclude;

    int tickCounter = 0;
    bool allowed = false;
    bool reported = false;
    std::string reportedHost;
};
