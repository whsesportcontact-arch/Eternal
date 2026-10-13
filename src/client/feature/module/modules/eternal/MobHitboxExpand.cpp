#include "pch.h"
#include "MobHitboxExpand.h"
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"
#include "mc/common/network/RemoteConnectorComposite.h"

namespace {
    constexpr const char* kSect = "\xC2\xA7";

    void say(std::string const& msg) {
        Latite::getClientMessageQueue().push(std::string(kSect) + "d[Enta]" + kSect + "r " + msg);
    }

    bool isLocalAddress(std::string const& s) {
        return s.empty() || s == "127.0.0.1" || s == "localhost" || s == "::1" || s == "0.0.0.0";
    }
}

MobHitboxExpand::MobHitboxExpand()
    : Module("MobHitboxExpand", L"Mob Hitbox Expand (Singleplayer)",
             L"Experimental. Makes the mobs you pick easier to hit over blocks, for farms in your own singleplayer "
             L"world. Turns itself off on Realms, servers, and worlds with other players.",
             GAME) {
    addSliderSetting("extraHeight", L"Extra Height", L"Blocks added to the top of the hitbox.", extraHeight,
                     FloatValue(0.f), FloatValue(3.f), FloatValue(0.25f));
    addSliderSetting("extraWidth", L"Extra Width", L"Blocks added to each side of the hitbox.", extraWidth,
                     FloatValue(0.f), FloatValue(1.f), FloatValue(0.1f));

    // ids are the low byte of the game's actor type (the old numeric entity ids)
    mobs = {
        { 48, "witherSkeleton", L"Wither Skeleton", BoolValue(true) },
        { 43, "blaze", L"Blaze", BoolValue(false) },
        { 36, "zombifiedPiglin", L"Zombified Piglin", BoolValue(false) },
        { 123, "piglin", L"Piglin", BoolValue(false) },
        { 42, "magmaCube", L"Magma Cube", BoolValue(false) },
        { 34, "skeleton", L"Skeleton", BoolValue(false) },
        { 32, "zombie", L"Zombie", BoolValue(false) },
        { 33, "creeper", L"Creeper", BoolValue(false) },
        { 35, "spider", L"Spider", BoolValue(false) },
        { 38, "enderman", L"Enderman", BoolValue(false) },
        { 45, "witch", L"Witch", BoolValue(false) },
    };
    // settings keep references into `mobs`, so it must never grow after this point
    for (auto& mob : mobs) {
        addSetting(mob.key, mob.name, L"", mob.enabled);
    }

    listen<TickEvent>(static_cast<EventListenerFunc>(&MobHitboxExpand::onTick));
    listen<LeaveGameEvent>(static_cast<EventListenerFunc>(&MobHitboxExpand::onLeaveGame));
}

std::string MobHitboxExpand::notSoloReason(SDK::Level* level) {
    if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) {
        std::string host = info->hostIpAddress;
        std::string url = info->unresolvedUrl;
        if (!isLocalAddress(host)) return "you're on a server/Realm (" + host + ")";
        if (!isLocalAddress(url)) return "you're on a server/Realm (" + url + ")";
    }
    if (!level) return "no world loaded";
    if (auto* players = level->getPlayerList(); players && players->size() > 1)
        return "there are other players in this world";
    return {};
}

bool MobHitboxExpand::isSelected(uint32_t entityTypeId) {
    const uint32_t legacy = entityTypeId & 0xFF;
    for (auto& mob : mobs) {
        if (mob.legacyId == legacy) return std::get<BoolValue>(mob.enabled);
    }
    return false;
}

// expand = true: grow selected mobs. expand = false: put every mob we touched back to normal.
//
// We change the mob's stored size (the AABB shape's width/height), not just its box: the game rebuilds the box
// from that size every time the mob moves, so the bigger box stays put instead of flickering back each frame.
void MobHitboxExpand::apply(bool expand) {
    auto* ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;
    auto* level = ci->minecraft->getLevel();
    if (!level) return;

    std::lock_guard guard(lock);
    const float ew = expand ? std::get<FloatValue>(extraWidth).value : 0.f;
    const float eh = expand ? std::get<FloatValue>(extraHeight).value : 0.f;

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

    std::unordered_map<uint64_t, Tracked> next;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || actor->isPlayer() || !actor->aabbShape) continue;

        const uint64_t id = actor->getRuntimeID();
        auto it = expanded.find(id);
        const bool touched = it != expanded.end();
        const bool wanted = expand && isSelected(actor->getEntityTypeID());
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

void MobHitboxExpand::onTick(Event& evG) {
    auto& ev = reinterpret_cast<TickEvent&>(evG);

    // re-check where we are about once a second, and right away when entering a world
    if (!checkedOnce || ++tickCounter % 20 == 0) {
        checkedOnce = true;
        std::string reason = notSoloReason(ev.getLevel());
        const bool nowAllowed = reason.empty();

        if (nowAllowed != allowed || reason != lastReason) {
            if (nowAllowed)
                say(std::string("Mob Hitbox Expand is ") + kSect + "aON" + kSect + "r (your singleplayer world).");
            else
                say(std::string("Mob Hitbox Expand is ") + kSect + "cOFF" + kSect + "r here: " + reason + ".");
        }
        if (allowed && !nowAllowed) apply(false); // failsafe: undo immediately
        allowed = nowAllowed;
        lastReason = reason;
    }

    if (allowed) apply(true);
}

void MobHitboxExpand::onLeaveGame(Event&) {
    allowed = false;
    checkedOnce = false;
    lastReason.clear();
    std::lock_guard guard(lock);
    expanded.clear(); // the world's mobs are gone, nothing to restore
}

void MobHitboxExpand::onDisable() {
    if (allowed) apply(false);
    allowed = false;
    checkedOnce = false;
    lastReason.clear();
    std::lock_guard guard(lock);
    expanded.clear();
}
