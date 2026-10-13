#include "pch.h"
#include "HitboxExpand.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "mc/common/client/renderer/MaterialPtr.h"
#include <util/DrawUtil3D.h>
#include "mc/common/network/RemoteConnectorComposite.h"
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
    using G = HitboxExpand::Group;

    // Every vanilla Bedrock entity. `ids` lists the game identifiers (without "minecraft:") that the toggle covers,
    // separated by commas, because a few mobs exist under an old and a new identifier.
    // Anything not listed here (add-on mobs, entities from future updates) falls under "Other Entities".
    struct Entry {
        const wchar_t* name;
        G group;
        const char* ids;
    };

    constexpr Entry kEntities[] = {
        // Hostile
        { L"Blaze", G::Hostile, "blaze" },
        { L"Bogged", G::Hostile, "bogged" },
        { L"Breeze", G::Hostile, "breeze" },
        { L"Camel Husk", G::Hostile, "camel_husk" },
        { L"Cave Spider", G::Hostile, "cave_spider" },
        { L"Creaking", G::Hostile, "creaking" },
        { L"Creeper", G::Hostile, "creeper" },
        { L"Drowned", G::Hostile, "drowned" },
        { L"Endermite", G::Hostile, "endermite" },
        { L"Evoker", G::Hostile, "evocation_illager" },
        { L"Ghast", G::Hostile, "ghast" },
        { L"Guardian", G::Hostile, "guardian" },
        { L"Hoglin", G::Hostile, "hoglin" },
        { L"Husk", G::Hostile, "husk" },
        { L"Magma Cube", G::Hostile, "magma_cube" },
        { L"Parched", G::Hostile, "parched" },
        { L"Phantom", G::Hostile, "phantom" },
        { L"Piglin Brute", G::Hostile, "piglin_brute" },
        { L"Pillager", G::Hostile, "pillager" },
        { L"Ravager", G::Hostile, "ravager" },
        { L"Shulker", G::Hostile, "shulker" },
        { L"Silverfish", G::Hostile, "silverfish" },
        { L"Skeleton", G::Hostile, "skeleton" },
        { L"Slime", G::Hostile, "slime" },
        { L"Spider", G::Hostile, "spider" },
        { L"Stray", G::Hostile, "stray" },
        { L"Vex", G::Hostile, "vex" },
        { L"Vindicator", G::Hostile, "vindicator" },
        { L"Witch", G::Hostile, "witch" },
        { L"Wither Skeleton", G::Hostile, "wither_skeleton" },
        { L"Zoglin", G::Hostile, "zoglin" },
        { L"Zombie", G::Hostile, "zombie" },
        { L"Zombie Nautilus", G::Hostile, "zombie_nautilus" },
        { L"Zombie Villager", G::Hostile, "zombie_villager_v2,zombie_villager" },

        // Neutral (attack only when provoked)
        { L"Bee", G::Neutral, "bee" },
        { L"Dolphin", G::Neutral, "dolphin" },
        { L"Enderman", G::Neutral, "enderman" },
        { L"Goat", G::Neutral, "goat" },
        { L"Iron Golem", G::Neutral, "iron_golem" },
        { L"Llama", G::Neutral, "llama" },
        { L"Panda", G::Neutral, "panda" },
        { L"Piglin", G::Neutral, "piglin" },
        { L"Polar Bear", G::Neutral, "polar_bear" },
        { L"Trader Llama", G::Neutral, "trader_llama" },
        { L"Wolf", G::Neutral, "wolf" },
        { L"Zombified Piglin", G::Neutral, "zombie_pigman" },

        // Passive
        { L"Allay", G::Passive, "allay" },
        { L"Armadillo", G::Passive, "armadillo" },
        { L"Axolotl", G::Passive, "axolotl" },
        { L"Bat", G::Passive, "bat" },
        { L"Camel", G::Passive, "camel" },
        { L"Cat", G::Passive, "cat" },
        { L"Chicken", G::Passive, "chicken" },
        { L"Cod", G::Passive, "cod" },
        { L"Copper Golem", G::Passive, "copper_golem" },
        { L"Cow", G::Passive, "cow" },
        { L"Donkey", G::Passive, "donkey" },
        { L"Fox", G::Passive, "fox" },
        { L"Frog", G::Passive, "frog" },
        { L"Glow Squid", G::Passive, "glow_squid" },
        { L"Happy Ghast", G::Passive, "happy_ghast" },
        { L"Horse", G::Passive, "horse" },
        { L"Mooshroom", G::Passive, "mooshroom" },
        { L"Mule", G::Passive, "mule" },
        { L"Nautilus", G::Passive, "nautilus" },
        { L"Ocelot", G::Passive, "ocelot" },
        { L"Parrot", G::Passive, "parrot" },
        { L"Pig", G::Passive, "pig" },
        { L"Pufferfish", G::Passive, "pufferfish" },
        { L"Rabbit", G::Passive, "rabbit" },
        { L"Salmon", G::Passive, "salmon" },
        { L"Sheep", G::Passive, "sheep" },
        { L"Skeleton Horse", G::Passive, "skeleton_horse" },
        { L"Sniffer", G::Passive, "sniffer" },
        { L"Snow Golem", G::Passive, "snow_golem" },
        { L"Squid", G::Passive, "squid" },
        { L"Strider", G::Passive, "strider" },
        { L"Tadpole", G::Passive, "tadpole" },
        { L"Tropical Fish", G::Passive, "tropicalfish" },
        { L"Turtle", G::Passive, "turtle" },
        { L"Villager", G::Passive, "villager_v2,villager" },
        { L"Wandering Trader", G::Passive, "wandering_trader" },
        { L"Zombie Horse", G::Passive, "zombie_horse" },

        // Bosses
        { L"Elder Guardian", G::Boss, "elder_guardian" },
        { L"Ender Dragon", G::Boss, "ender_dragon" },
        { L"Warden", G::Boss, "warden" },
        { L"Wither", G::Boss, "wither" },

        // Projectiles (handy for knocking ghast fireballs back)
        { L"Arrow", G::Projectile, "arrow" },
        { L"Breeze Wind Charge", G::Projectile, "breeze_wind_charge_projectile" },
        { L"Dragon Fireball", G::Projectile, "dragon_fireball" },
        { L"Egg", G::Projectile, "egg" },
        { L"Ender Pearl", G::Projectile, "ender_pearl" },
        { L"Ghast Fireball", G::Projectile, "fireball" },
        { L"Firework Rocket", G::Projectile, "fireworks_rocket" },
        { L"Fishing Bobber", G::Projectile, "fishing_hook" },
        { L"Llama Spit", G::Projectile, "llama_spit" },
        { L"Potion", G::Projectile, "splash_potion,lingering_potion" },
        { L"Shulker Bullet", G::Projectile, "shulker_bullet" },
        { L"Small Fireball", G::Projectile, "small_fireball" },
        { L"Snowball", G::Projectile, "snowball" },
        { L"Trident", G::Projectile, "thrown_trident" },
        { L"Wind Charge", G::Projectile, "wind_charge_projectile" },
        { L"Wither Skull", G::Projectile, "wither_skull,wither_skull_dangerous" },
        { L"Bottle o' Enchanting", G::Projectile, "xp_bottle" },

        // Vehicles and other objects
        { L"Armor Stand", G::Vehicle, "armor_stand" },
        { L"Boat", G::Vehicle, "boat" },
        { L"Boat with Chest", G::Vehicle, "chest_boat" },
        { L"End Crystal", G::Vehicle, "ender_crystal" },
        { L"Minecart", G::Vehicle, "minecart" },
        { L"Minecart with Chest", G::Vehicle, "chest_minecart" },
        { L"Minecart with Command Block", G::Vehicle, "command_block_minecart" },
        { L"Minecart with Hopper", G::Vehicle, "hopper_minecart" },
        { L"Minecart with TNT", G::Vehicle, "tnt_minecart" },

        // Dropped items and XP
        { L"Dropped Item", G::Drop, "item" },
        { L"Experience Orb", G::Drop, "xp_orb" },
    };

    // Never expanded: not things you hit, and growing them can break blocks or physics.
    constexpr const char* kNever[] = {
        "agent", "area_effect_cloud", "evocation_fang", "eye_of_ender_signal", "falling_block", "leash_knot",
        "lightning_bolt", "npc", "ominous_item_spawner", "painting", "player", "tnt",
    };

    std::string normalizeId(std::string s) {
        if (auto lt = s.find('<'); lt != std::string::npos) s.resize(lt);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
        s.erase(0, start);
        if (s.starts_with("minecraft:")) s.erase(0, 10);
        return s;
    }

    std::vector<std::string> splitIds(std::string const& s) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= s.size()) {
            size_t comma = s.find(',', start);
            if (comma == std::string::npos) comma = s.size();
            auto id = normalizeId(s.substr(start, comma - start));
            if (!id.empty()) out.push_back(std::move(id));
            start = comma + 1;
        }
        return out;
    }

    constexpr const char* kSect = "\xC2\xA7";

    void say(std::string const& msg) {
        Latite::getClientMessageQueue().push(std::string(kSect) + "d[Enta]" + kSect + "r " + msg);
    }

    bool isLocalAddress(std::string const& s) {
        return s.empty() || s == "127.0.0.1" || s == "localhost" || s == "::1" || s == "0.0.0.0";
    }
}

HitboxExpand::HitboxExpand()
    : Module("HitboxExpand", L"Hitbox Expand (Singleplayer)",
             L"Makes the mobs you pick easier to hit. Only works in your own solo singleplayer world: turns itself "
             L"off on Realms, servers and worlds with other players.",
             GAME) {
    addSliderSetting("horizontal", L"Extra Width", L"Blocks added to each side of the hitbox.", horizontal,
                     FloatValue(0.f), FloatValue(2.f), FloatValue(0.05f));
    addSliderSetting("vertical", L"Extra Height", L"Blocks added to the top of the hitbox.", vertical,
                     FloatValue(0.f), FloatValue(3.f), FloatValue(0.25f));

    addSetting("outline", L"Outlines", L"Draws an outline around every expanded hitbox.", outline);
    addSetting("outlineColor", L"Outline Color", L"", outlineColor, "outline"_istrue);
    addSliderSetting("outlineThickness", L"Outline Thickness", L"", outlineThickness, FloatValue(1.f),
                     FloatValue(5.f), FloatValue(1.f), "outline"_istrue);
    addSetting("outlineFill", L"Outline Fill", L"Also tint the sides of the box.", outlineFill, "outline"_istrue);
    addSetting("outlineThroughWalls", L"Outlines Through Walls",
               L"Show outlines through blocks. Like the rest of this module, only in your own solo world.",
               outlineThroughWalls, "outline"_istrue);

    // Never resized after this, because each setting keeps a pointer to its toggle.
    entityToggles.reserve(std::size(kEntities));

    auto addCategory = [this](G group, const char* setting, std::wstring const& name, std::wstring const& desc,
                              ValueType& value) {
        addSetting(setting, name, desc, value);

        // the entity toggles for this category, only shown while the category is on
        for (auto const& e : kEntities) {
            if (e.group != group) continue;
            auto ids = splitIds(e.ids);
            const int index = static_cast<int>(entityToggles.size());
            entityToggles.emplace_back(BoolValue(true));
            for (auto const& id : ids) idLookup[id] = Lookup { group, index };

            addSetting("entity_" + ids.front(), std::wstring(L"    ") + e.name, L"", entityToggles.back(),
                       Setting::Condition(setting));
        }
    };

    addCategory(G::Hostile, "hostile", L"Hostile Mobs", L"Zombies, skeletons, wither skeletons, creepers, blazes...",
                hostile);
    addCategory(G::Neutral, "neutral", L"Neutral Mobs", L"Endermen, piglins, zombified piglins, wolves, golems...",
                neutral);
    addCategory(G::Passive, "passive", L"Passive Mobs", L"Animals, villagers, fish and other friendly mobs.", passive);
    addCategory(G::Boss, "bosses", L"Bosses", L"Wither, Ender Dragon, Warden and Elder Guardian.", bosses);
    addCategory(G::Projectile, "projectiles", L"Projectiles",
                L"Ghast fireballs, wither skulls, arrows and other projectiles.", projectiles);
    addCategory(G::Vehicle, "vehicles", L"Vehicles & Objects", L"Boats, minecarts, armor stands and end crystals.",
                vehicles);
    addCategory(G::Drop, "drops", L"Items & XP", L"Dropped items and experience orbs.", drops);

    for (auto const* id : kNever) idLookup[id] = Lookup { G::Never, -1 };

    addSetting("unlisted", L"Add-on Mobs",
               L"Mobs from add-ons or behavior packs (for example a skygen world's custom mobs), and anything newer "
               L"than the list above.",
               unlisted);
    addSetting("playerNpcs", L"Player-Type NPCs",
               L"Add-on NPCs that are built like players. Real players are never affected: the module turns off "
               L"whenever another real player is in the world.",
               playerNpcs);
    addSetting("include", L"Extra Entity IDs",
               L"Comma-separated IDs to always expand, e.g. mymod:frost_zombie", include);

    listen<TickEvent>(static_cast<EventListenerFunc>(&HitboxExpand::onTick));
    listen<LeaveGameEvent>(static_cast<EventListenerFunc>(&HitboxExpand::onLeaveGame));
    listen<RenderLevelEvent>(static_cast<EventListenerFunc>(&HitboxExpand::onRender));
}

std::string HitboxExpand::notSoloReason(SDK::Level* level) {
    if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) {
        std::string host = info->hostIpAddress;
        std::string url = info->unresolvedUrl;
        if (!isLocalAddress(host)) return "you're on a server/Realm (" + host + ")";
        if (!isLocalAddress(url)) return "you're on a server/Realm (" + url + ")";
    }
    if (!level) return "no world loaded";
    if (auto* players = level->getPlayerList(); players && players->size() > 1) {
        // name them, so it's clear whether these are real people or an add-on's NPCs
        std::string me;
        if (auto* lp = SDK::ClientInstance::get()->getLocalPlayer()) me = lp->playerName;
        std::string names;
        int shown = 0;
        for (auto& [uuid, entry] : *players) {
            std::string n = entry.name;
            if (n == me) continue;
            if (shown++ < 3) names += (names.empty() ? "" : ", ") + n;
        }
        if (shown > 3) names += ", ...";
        return "other players are in this world (" + names + ")";
    }
    return {};
}

void HitboxExpand::parseIncludeList() {
    auto& inc = std::get<TextValue>(include).str;
    if (inc == lastInclude) return;
    lastInclude = inc;
    includeSet.clear();
    for (auto& id : splitIds(util::WStrToStr(inc))) includeSet.insert(std::move(id));
}

bool HitboxExpand::groupEnabled(Group g) {
    switch (g) {
    case Hostile: return std::get<BoolValue>(hostile);
    case Neutral: return std::get<BoolValue>(neutral);
    case Passive: return std::get<BoolValue>(passive);
    case Boss: return std::get<BoolValue>(bosses);
    case Projectile: return std::get<BoolValue>(projectiles);
    case Vehicle: return std::get<BoolValue>(vehicles);
    case Drop: return std::get<BoolValue>(drops);
    case Unlisted: return std::get<BoolValue>(unlisted);
    default: return false;
    }
}

bool HitboxExpand::isSelected(SDK::Actor* actor) {
    // player-type: only NPCs can get here, the solo-world lock turns everything off when real players are around
    if (actor->isPlayer()) return std::get<BoolValue>(playerNpcs);

    // match by the entity's identifier ("minecraft:wither_skeleton"), not the numeric type id
    const std::string id = normalizeId(actor->getEntityTypeName());
    if (id.empty()) return false;
    if (includeSet.contains(id)) return true;

    Lookup l { Unlisted, -1 };
    if (auto it = idLookup.find(id); it != idLookup.end()) l = it->second;
    if (l.group == Never || !groupEnabled(l.group)) return false;
    return l.toggle < 0 || static_cast<bool>(std::get<BoolValue>(entityToggles[l.toggle]));
}

// expand = true: grow selected entities. expand = false: put every entity we touched back to normal.
//
// We change the entity's stored size (the AABB shape's width/height), not just its box: the game rebuilds the box
// from that size every time the entity moves, so the bigger box stays put instead of flickering back each frame.
void HitboxExpand::apply(bool expand) {
    auto* ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;
    auto* level = ci->minecraft->getLevel();
    if (!level) return;

    std::lock_guard guard(lock);
    if (expand) parseIncludeList();
    const float ew = expand ? (std::max)(0.f, std::get<FloatValue>(horizontal).value) : 0.f;
    const float eh = expand ? (std::max)(0.f, std::get<FloatValue>(vertical).value) : 0.f;

    auto same = [](Vec2 a, Vec2 b) { return std::fabs(a.x - b.x) < 0.001f && std::fabs(a.y - b.y) < 0.001f; };

    // rebuild the box for a new size right away: same center, same bottom
    auto setSize = [](SDK::AABBShapeComponent* shape, Vec2 size) {
        AABB& box = shape->boundingBox;
        const float cx = (box.lower.x + box.higher.x) * 0.5f;
        const float cz = (box.lower.z + box.higher.z) * 0.5f;
        const float bottom = box.lower.y;
        shape->size = size;
        box = AABB(Vec3(cx - size.x * 0.5f, bottom, cz - size.x * 0.5f),
                   Vec3(cx + size.x * 0.5f, bottom + size.y, cz + size.x * 0.5f));
    };

    SDK::Actor* self = ci->getLocalPlayer();
    std::unordered_map<uint64_t, Tracked> next;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || actor == self || !actor->aabbShape) continue; // never you

        const uint64_t id = actor->getRuntimeID();
        auto it = expanded.find(id);
        const bool touched = it != expanded.end();
        const bool wanted = expand && (ew > 0.f || eh > 0.f) && isSelected(actor);
        if (!wanted && !touched) continue;

        auto* shape = actor->aabbShape;
        const Vec2 current = shape->size;
        if (current.x <= 0.f || current.y <= 0.f) continue;

        if (wanted) {
            // if the size isn't the one we set, the game changed it (spawned, grew up...): that's the new normal
            const Vec2 original = (touched && same(current, it->second.applied)) ? it->second.original : current;
            const Vec2 grown(original.x + ew * 2.f, original.y + eh);
            if (!same(current, grown)) setSize(shape, grown);
            next[id] = { original, grown };
        } else if (same(current, it->second.applied)) {
            setSize(shape, it->second.original); // put it back
        }
    }
    expanded = std::move(next);
}

void HitboxExpand::onTick(Event& evG) {
    auto& ev = reinterpret_cast<TickEvent&>(evG);

    // re-check where we are about once a second, and right away when entering a world
    if (!checkedOnce || ++tickCounter % 20 == 0) {
        checkedOnce = true;
        std::string reason = notSoloReason(ev.getLevel());
        const bool nowAllowed = reason.empty();

        if (nowAllowed != allowed || reason != lastReason) {
            if (nowAllowed)
                say(std::string("Hitbox Expand is ") + kSect + "aON" + kSect + "r (your singleplayer world).");
            else
                say(std::string("Hitbox Expand is ") + kSect + "cOFF" + kSect + "r here: " + reason + ".");
        }
        if (allowed && !nowAllowed) apply(false); // failsafe: undo immediately
        allowed = nowAllowed;
        lastReason = reason;
    }

    if (allowed) apply(true);
}

void HitboxExpand::onLeaveGame(Event&) {
    allowed = false;
    checkedOnce = false;
    lastReason.clear();
    std::lock_guard guard(lock);
    expanded.clear(); // the world's mobs are gone, nothing to restore
}

void HitboxExpand::onDisable() {
    drawFailed = false; // toggling the module retries the outlines after an error
    if (allowed) apply(false);
    allowed = false;
    checkedOnce = false;
    lastReason.clear();
    std::lock_guard guard(lock);
    expanded.clear();
}

namespace {
    // Runs fn(ctx) and catches crashes (access violations) instead of letting them close the game.
    bool guardedDraw(void (*fn)(void*), void* ctx) {
        __try {
            fn(ctx);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}

struct HitboxExpandDraw {
    MCDrawUtil3D* dc;
    std::vector<AABB>* boxes;
    d2d::Color color;
    int layers;
    bool fill;
};

void HitboxExpand::drawImpl(void* ctxPtr) {
    auto& c = *static_cast<HitboxExpandDraw*>(ctxPtr);
    for (auto& box : *c.boxes) {
        Vec3 lo = box.lower, hi = box.higher;
        for (int i = 0; i < c.layers; i++) {
            float e = 0.003f + 0.004f * static_cast<float>(i);
            c.dc->drawBox(AABB { Vec3 { lo.x - e, lo.y - e, lo.z - e }, Vec3 { hi.x + e, hi.y + e, hi.z + e } },
                          c.color);
        }
        if (c.fill) {
            d2d::Color fc = c.color.asAlpha(0.18f);
            float e = 0.002f;
            Vec3 a { lo.x - e, lo.y - e, lo.z - e }, b { hi.x + e, hi.y + e, hi.z + e };
            c.dc->fillQuad({ a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z }, fc);
            c.dc->fillQuad({ a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, a.y, b.z }, { a.x, a.y, b.z }, fc);
            c.dc->fillQuad({ a.x, a.y, a.z }, { a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, a.y, a.z }, fc);
            c.dc->fillQuad({ a.x, a.y, b.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z }, fc);
            c.dc->fillQuad({ a.x, a.y, a.z }, { a.x, b.y, a.z }, { a.x, b.y, b.z }, { a.x, a.y, b.z }, fc);
            c.dc->fillQuad({ b.x, a.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z }, fc);
        }
    }
    c.dc->flush();
}

void HitboxExpand::onRender(Event& evG) {
    if (drawFailed || !allowed || !std::get<BoolValue>(outline)) return;
    auto& ev = reinterpret_cast<RenderLevelEvent&>(evG);

    auto* ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->getLocalPlayer()) return;
    auto* level = ci->minecraft->getLevel();
    if (!level) return;

    const float alpha = ci->minecraft->timer ? ci->minecraft->timer->alpha : 1.f;

    std::vector<AABB> boxes;
    {
        std::lock_guard guard(lock);
        if (expanded.empty()) return;
        boxes.reserve(expanded.size());
        for (auto* actor : level->getRuntimeActorList()) {
            if (!actor || !actor->aabbShape) continue;
            if (!expanded.contains(actor->getRuntimeID())) continue;

            // slide the box to where the mob is drawn this frame, so the outline doesn't lag behind it
            AABB box = actor->getBoundingBox();
            const Vec3 pos = actor->getPos(), old = actor->getPosOld();
            const Vec3 shift { (old.x - pos.x) * (1.f - alpha), (old.y - pos.y) * (1.f - alpha),
                               (old.z - pos.z) * (1.f - alpha) };
            box.lower = box.lower + shift;
            box.higher = box.higher + shift;
            boxes.push_back(box);
        }
    }
    if (boxes.empty()) return;

    // Selection-box material is depth tested (hidden behind blocks); UI color draws on top of everything.
    MCDrawUtil3D dc { ci->levelRenderer, ev.getScreenContext(),
                      std::get<BoolValue>(outlineThroughWalls) ? SDK::MaterialPtr::getUIColor()
                                                               : SDK::MaterialPtr::getSelectionBoxMaterial() };
    HitboxExpandDraw ctx { &dc, &boxes, std::get<ColorValue>(outlineColor).getMainColor(),
                           (std::max)(1, static_cast<int>(std::get<FloatValue>(outlineThickness))),
                           std::get<BoolValue>(outlineFill) };
    if (!guardedDraw(&HitboxExpand::drawImpl, &ctx)) {
        drawFailed = true;
        say("Hitbox Expand outlines couldn't draw on this game version and paused. Toggle the module off and on "
            "to retry.");
    }
}
