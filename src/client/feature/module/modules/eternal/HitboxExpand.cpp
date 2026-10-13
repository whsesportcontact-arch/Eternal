#include "pch.h"
#include "HitboxExpand.h"
#include "client/event/events/TickEvent.h"
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

    bool sameBox(AABB const& a, AABB const& b) {
        constexpr float eps = 1e-4f;
        return std::fabs(a.lower.x - b.lower.x) < eps && std::fabs(a.lower.y - b.lower.y) < eps &&
               std::fabs(a.lower.z - b.lower.z) < eps && std::fabs(a.higher.x - b.higher.x) < eps &&
               std::fabs(a.higher.y - b.higher.y) < eps && std::fabs(a.higher.z - b.higher.z) < eps;
    }

    void pushChat(std::string const& text) {
        Latite::getClientMessageQueue().push(std::string("\xC2\xA7" "d[Enta]\xC2\xA7" "r ") + text);
    }
}

HitboxExpand::HitboxExpand()
    : Module("HitboxExpand", L"Hitbox Expand",
             L"Makes mobs easier to hit by growing their hitboxes. Singleplayer worlds only, never players.", GAME) {
    addSliderSetting("horizontal", L"Width", L"Blocks added to each side of the hitbox.", horizontal,
                     FloatValue(0.f), FloatValue(2.f), FloatValue(0.05f));
    addSliderSetting("vertical", L"Height", L"Blocks added to the top of the hitbox.", vertical, FloatValue(0.f),
                     FloatValue(2.f), FloatValue(0.05f));

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

    addSetting("unlisted", L"Other Entities", L"Add-on mobs and anything new that isn't in the list above.",
               unlisted);
    addSetting("include", L"Extra Entity IDs",
               L"Comma-separated IDs to always expand, for add-on mobs, e.g. mymod:frost_zombie", include);

    // keep listening while disabled so boxes get put back on the game thread
    listen<TickEvent>(static_cast<EventListenerFunc>(&HitboxExpand::onTick), true);
}

void HitboxExpand::onEnable() {
    reported = false;
}

bool HitboxExpand::isLocalWorld() {
    std::string host;
    if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) host = info->hostIpAddress;
    const bool local = host.empty() || host == "127.0.0.1" || host == "localhost" || host == "::1" ||
                       host == "0.0.0.0";

    if (!reported || host != reportedHost) {
        reported = true;
        reportedHost = host;
        pushChat(local ? "Hitbox Expand is \xC2\xA7" "aON\xC2\xA7" "r (singleplayer world)."
                       : "Hitbox Expand only works in your own singleplayer worlds, so it's \xC2\xA7" "cOFF\xC2\xA7"
                         "r here.");
    }
    return local;
}

void HitboxExpand::parseIncludeList() {
    auto& inc = std::get<TextValue>(include).str;
    if (inc == lastInclude) return;
    lastInclude = inc;
    includeSet.clear();
    for (auto& id : splitIds(util::WStrToStr(inc))) includeSet.insert(std::move(id));
}

HitboxExpand::Lookup HitboxExpand::classify(std::string const& id) {
    if (auto it = idLookup.find(id); it != idLookup.end()) return it->second;
    return Lookup { Unlisted, -1 };
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

void HitboxExpand::restoreAll() {
    if (tracked.empty()) return;

    // Only touch actors that are still alive in the level; stale pointers are just forgotten.
    if (auto ci = SDK::ClientInstance::get(); ci && ci->minecraft) {
        if (auto level = ci->minecraft->getLevel()) {
            for (auto* entt : level->getRuntimeActorList()) {
                auto it = tracked.find(entt);
                if (it == tracked.end() || !entt->aabbShape) continue;
                auto& bb = entt->getBoundingBox();
                // if the game already recomputed the box, it's vanilla again and we leave it alone
                if (sameBox(bb, it->second.written)) bb = it->second.original;
            }
        }
    }
    tracked.clear();
}

void HitboxExpand::onTick(Event& evG) {
    auto& ev = static_cast<TickEvent&>(evG);
    ++tickCounter;

    auto level = ev.getLevel();
    if (!level) {
        tracked.clear(); // world unloaded, every pointer is gone
        reported = false;
        return;
    }

    if (!isEnabled()) {
        restoreAll();
        return;
    }

    if (tickCounter % 20 == 0 || !reported) allowed = isLocalWorld();
    if (!allowed) {
        restoreAll();
        return;
    }

    parseIncludeList();

    const float h = (std::max)(0.f, static_cast<float>(std::get<FloatValue>(horizontal)));
    const float v = (std::max)(0.f, static_cast<float>(std::get<FloatValue>(vertical)));

    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    std::unordered_map<SDK::Actor*, Tracked> next;
    next.reserve(tracked.size());

    for (auto* entt : level->getRuntimeActorList()) {
        if (!entt || entt == lp || !entt->aabbShape) continue;
        if (entt->isPlayer()) continue;

        auto& bb = entt->getBoundingBox();
        auto prev = tracked.find(entt);

        bool want = false;
        if (const std::string id = normalizeId(entt->getEntityTypeName()); !id.empty()) {
            const Lookup l = classify(id);
            if (l.group != Never) {
                if (includeSet.contains(id)) {
                    want = true;
                } else if (groupEnabled(l.group)) {
                    want = l.toggle < 0 || static_cast<bool>(std::get<BoolValue>(entityToggles[l.toggle]));
                }
            }
        }

        // Work out the vanilla box: if what's there is still our write, the game hasn't touched it.
        AABB original = bb;
        if (prev != tracked.end() && sameBox(bb, prev->second.written)) original = prev->second.original;

        if (!want || (h == 0.f && v == 0.f)) {
            bb = original; // turned off for this entity: put it back
            continue;
        }

        // Expanded box already in place with the same settings
        if (prev != tracked.end() && sameBox(bb, prev->second.written) && prev->second.h == h &&
            prev->second.v == v) {
            next.emplace(entt, prev->second);
            continue;
        }

        AABB expanded = original;
        expanded.lower.x -= h;
        expanded.lower.z -= h;
        expanded.higher.x += h;
        expanded.higher.z += h;
        expanded.higher.y += v; // only grow upward so the box never sinks into the ground

        bb = expanded;
        next.emplace(entt, Tracked { original, expanded, h, v });
    }

    tracked = std::move(next);
}
