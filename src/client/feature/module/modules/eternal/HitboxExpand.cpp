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
    // Every vanilla Bedrock entity identifier (without the "minecraft:" prefix), sorted into groups.
    // Anything not listed here (add-on mobs, entities from future updates) falls into "Unlisted".
    struct Entry {
        const char* id;
        HitboxExpand::Group group;
    };

    using G = HitboxExpand::Group;

    constexpr Entry kEntities[] = {
        // Hostile
        { "blaze", G::Hostile },           { "bogged", G::Hostile },
        { "breeze", G::Hostile },          { "camel_husk", G::Hostile },
        { "cave_spider", G::Hostile },     { "creaking", G::Hostile },
        { "creeper", G::Hostile },         { "drowned", G::Hostile },
        { "endermite", G::Hostile },       { "evocation_illager", G::Hostile },
        { "ghast", G::Hostile },           { "guardian", G::Hostile },
        { "hoglin", G::Hostile },          { "husk", G::Hostile },
        { "magma_cube", G::Hostile },      { "parched", G::Hostile },
        { "phantom", G::Hostile },         { "piglin_brute", G::Hostile },
        { "pillager", G::Hostile },        { "ravager", G::Hostile },
        { "shulker", G::Hostile },         { "silverfish", G::Hostile },
        { "skeleton", G::Hostile },        { "slime", G::Hostile },
        { "spider", G::Hostile },          { "stray", G::Hostile },
        { "vex", G::Hostile },             { "vindicator", G::Hostile },
        { "witch", G::Hostile },           { "wither_skeleton", G::Hostile },
        { "zoglin", G::Hostile },          { "zombie", G::Hostile },
        { "zombie_nautilus", G::Hostile }, { "zombie_villager", G::Hostile },
        { "zombie_villager_v2", G::Hostile },

        // Neutral (attack only when provoked)
        { "bee", G::Neutral },             { "dolphin", G::Neutral },
        { "enderman", G::Neutral },        { "goat", G::Neutral },
        { "iron_golem", G::Neutral },      { "llama", G::Neutral },
        { "panda", G::Neutral },           { "piglin", G::Neutral },
        { "polar_bear", G::Neutral },      { "trader_llama", G::Neutral },
        { "wolf", G::Neutral },            { "zombie_pigman", G::Neutral }, // zombified piglin

        // Passive
        { "allay", G::Passive },           { "armadillo", G::Passive },
        { "axolotl", G::Passive },         { "bat", G::Passive },
        { "camel", G::Passive },           { "cat", G::Passive },
        { "chicken", G::Passive },         { "cod", G::Passive },
        { "copper_golem", G::Passive },    { "cow", G::Passive },
        { "donkey", G::Passive },          { "fox", G::Passive },
        { "frog", G::Passive },            { "glow_squid", G::Passive },
        { "happy_ghast", G::Passive },     { "horse", G::Passive },
        { "mooshroom", G::Passive },       { "mule", G::Passive },
        { "nautilus", G::Passive },        { "ocelot", G::Passive },
        { "parrot", G::Passive },          { "pig", G::Passive },
        { "pufferfish", G::Passive },      { "rabbit", G::Passive },
        { "salmon", G::Passive },          { "sheep", G::Passive },
        { "skeleton_horse", G::Passive },  { "sniffer", G::Passive },
        { "snow_golem", G::Passive },      { "squid", G::Passive },
        { "strider", G::Passive },         { "tadpole", G::Passive },
        { "tropicalfish", G::Passive },    { "turtle", G::Passive },
        { "villager", G::Passive },        { "villager_v2", G::Passive },
        { "wandering_trader", G::Passive },{ "zombie_horse", G::Passive },

        // Bosses
        { "elder_guardian", G::Boss },     { "ender_dragon", G::Boss },
        { "warden", G::Boss },             { "wither", G::Boss },

        // Projectiles (handy for knocking ghast fireballs back)
        { "arrow", G::Projectile },               { "breeze_wind_charge_projectile", G::Projectile },
        { "dragon_fireball", G::Projectile },     { "egg", G::Projectile },
        { "ender_pearl", G::Projectile },         { "fireball", G::Projectile },
        { "fireworks_rocket", G::Projectile },    { "fishing_hook", G::Projectile },
        { "lingering_potion", G::Projectile },    { "llama_spit", G::Projectile },
        { "shulker_bullet", G::Projectile },      { "small_fireball", G::Projectile },
        { "snowball", G::Projectile },            { "splash_potion", G::Projectile },
        { "thrown_trident", G::Projectile },      { "wind_charge_projectile", G::Projectile },
        { "wither_skull", G::Projectile },        { "wither_skull_dangerous", G::Projectile },
        { "xp_bottle", G::Projectile },

        // Vehicles and other objects
        { "armor_stand", G::Vehicle },            { "boat", G::Vehicle },
        { "chest_boat", G::Vehicle },             { "chest_minecart", G::Vehicle },
        { "command_block_minecart", G::Vehicle }, { "ender_crystal", G::Vehicle },
        { "hopper_minecart", G::Vehicle },        { "minecart", G::Vehicle },
        { "tnt_minecart", G::Vehicle },

        // Dropped items and XP
        { "item", G::Drop },                      { "xp_orb", G::Drop },

        // Never expanded: these aren't things you hit, and growing them can break blocks/physics
        { "agent", G::Never },                    { "area_effect_cloud", G::Never },
        { "evocation_fang", G::Never },           { "eye_of_ender_signal", G::Never },
        { "falling_block", G::Never },            { "leash_knot", G::Never },
        { "lightning_bolt", G::Never },           { "npc", G::Never },
        { "ominous_item_spawner", G::Never },     { "painting", G::Never },
        { "player", G::Never },                   { "tnt", G::Never },
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
    addSliderSetting("vertical", L"Height", L"Blocks added to the top of the hitbox.", vertical,
                     FloatValue(0.f), FloatValue(2.f), FloatValue(0.05f));

    addSetting("hostile", L"Hostile Mobs", L"Zombies, skeletons, wither skeletons, creepers, blazes and the rest.",
               hostile);
    addSetting("neutral", L"Neutral Mobs", L"Endermen, piglins, zombified piglins, wolves, bees, golems...", neutral);
    addSetting("passive", L"Passive Mobs", L"Animals, villagers, fish and other friendly mobs.", passive);
    addSetting("bosses", L"Bosses", L"Wither, Ender Dragon, Warden and Elder Guardian.", bosses);
    addSetting("projectiles", L"Projectiles", L"Ghast fireballs, wither skulls, arrows and other projectiles.",
               projectiles);
    addSetting("vehicles", L"Vehicles & Objects", L"Boats, minecarts, armor stands and end crystals.", vehicles);
    addSetting("drops", L"Items & XP", L"Dropped items and experience orbs.", drops);
    addSetting("unlisted", L"Other Entities", L"Add-on mobs and anything new that isn't in the built-in list.",
               unlisted);
    addSetting("include", L"Always Include", L"Comma-separated entity IDs to always expand, e.g. wither_skeleton",
               include);
    addSetting("exclude", L"Always Exclude", L"Comma-separated entity IDs to never expand, e.g. villager_v2,cat",
               exclude);

    // keep listening while disabled so boxes get put back on the game thread
    listen<TickEvent>(static_cast<EventListenerFunc>(&HitboxExpand::onTick), true);
}

void HitboxExpand::onEnable() {
    reported = false;
}

void HitboxExpand::onDisable() {
    // restoring happens on the next tick (game thread), see onTick
}

bool HitboxExpand::isLocalWorld() {
    std::string host;
    if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) host = info->hostIpAddress;
    const bool local = host.empty() || host == "127.0.0.1" || host == "localhost" || host == "::1" ||
                       host == "0.0.0.0";

    if (isEnabled() && (!reported || host != reportedHost)) {
        reported = true;
        reportedHost = host;
        pushChat(local ? "Hitbox Expand is \xC2\xA7" "aON\xC2\xA7" "r (singleplayer world)."
                       : "Hitbox Expand only works in your own singleplayer worlds, so it's \xC2\xA7" "cOFF\xC2\xA7"
                         "r here.");
    }
    return local;
}

void HitboxExpand::parseLists() {
    auto parse = [](std::wstring const& w, std::unordered_set<std::string>& out) {
        out.clear();
        std::string s = util::WStrToStr(w);
        size_t start = 0;
        while (start <= s.size()) {
            size_t comma = s.find(',', start);
            if (comma == std::string::npos) comma = s.size();
            auto id = normalizeId(s.substr(start, comma - start));
            if (!id.empty()) out.insert(id);
            start = comma + 1;
        }
    };

    auto& inc = std::get<TextValue>(include).str;
    auto& exc = std::get<TextValue>(exclude).str;
    if (inc != lastInclude) {
        lastInclude = inc;
        parse(inc, includeSet);
    }
    if (exc != lastExclude) {
        lastExclude = exc;
        parse(exc, excludeSet);
    }
}

HitboxExpand::Group HitboxExpand::classify(std::string const& id) {
    if (auto it = groupCache.find(id); it != groupCache.end()) return it->second;

    Group g = Unlisted;
    for (auto const& e : kEntities) {
        if (id == e.id) {
            g = e.group;
            break;
        }
    }
    groupCache.emplace(id, g);
    return g;
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
        groupCache.clear();
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

    parseLists();

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

        const std::string id = normalizeId(entt->getEntityTypeName());
        bool want = false;
        if (!id.empty() && !excludeSet.contains(id)) {
            const Group g = classify(id);
            want = g != Never && (includeSet.contains(id) || groupEnabled(g));
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
