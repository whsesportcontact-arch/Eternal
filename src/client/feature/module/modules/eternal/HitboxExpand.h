#pragma once
#include "../../Module.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Enta: makes mobs easier to hit by growing their hitboxes on your screen.
// Every entity has its own on/off toggle, grouped under a category toggle.
// Only runs in your OWN solo singleplayer world: it turns itself off on Realms, servers and any world with
// other players in it, and puts every hitbox back to normal when it does. Real players are never touched.
class HitboxExpand : public Module {
public:
    HitboxExpand();

    void onTick(Event& ev);
    void onLeaveGame(Event& ev);
    void onRender(Event& ev);
    static void drawImpl(void* ctx);
    void onDisable() override;

    enum Group : uint8_t { Hostile, Neutral, Passive, Boss, Projectile, Vehicle, Drop, Unlisted, Never };

private:
    struct Lookup {
        Group group = Unlisted;
        int toggle = -1; // index into entityToggles, -1 = no per-entity toggle
    };

    struct Tracked {
        Vec2 original; // the entity's real size (width, height)
        Vec2 applied;  // the size we gave it
    };

    // returns an empty string when this is your own solo world, otherwise why it isn't
    std::string notSoloReason(SDK::Level* level);
    bool isSelected(SDK::Actor* actor);
    bool groupEnabled(Group g);
    void apply(bool expand);
    void parseIncludeList();

    ValueType horizontal = FloatValue(0.3f);
    ValueType vertical = FloatValue(1.f);

    ValueType hostile = BoolValue(true);
    ValueType neutral = BoolValue(true);
    ValueType passive = BoolValue(true);
    ValueType bosses = BoolValue(true);
    ValueType projectiles = BoolValue(false);
    ValueType vehicles = BoolValue(false);
    ValueType drops = BoolValue(false);
    ValueType unlisted = BoolValue(false);   // add-on mobs and anything not in the built-in list
    ValueType playerNpcs = BoolValue(false); // player-type entities that aren't real players (NPCs)

    ValueType include = TextValue(L"");

    // outlines around expanded hitboxes, drawn like Chest Outlines
    ValueType outline = BoolValue(true);
    ValueType outlineColor = ColorValue(1.f, 0.3f, 0.3f, 1.f);
    ValueType outlineThickness = FloatValue(2.f);
    ValueType outlineFill = BoolValue(false);
    ValueType outlineThroughWalls = BoolValue(false);
    bool drawFailed = false;

    // One toggle per entity. Reserved up front and never resized: settings keep pointers into it.
    std::vector<ValueType> entityToggles;
    std::unordered_map<std::string, Lookup> idLookup;

    std::unordered_set<std::string> includeSet;
    std::wstring lastInclude;

    bool allowed = false;
    bool checkedOnce = false;
    std::string lastReason;
    int tickCounter = 0;
    std::unordered_map<uint64_t, Tracked> expanded; // runtime ids we changed
    std::mutex lock;
};
