#include "pch.h"
#include "ChestOutlines.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/network/RemoteConnectorComposite.h"
#include "mc/common/client/renderer/MaterialPtr.h"
#include <util/DrawUtil3D.h>
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"

ChestOutlines::ChestOutlines()
    : Module("ChestOutlines", L"Chest Outlines",
             L"Outlines chests, barrels, shulker boxes and other storage so your farms and sorters are easy to see.",
             GAME) {
    addSliderSetting("range", L"Range", L"How many blocks around you to look for storage.", range, FloatValue(4.f),
                     FloatValue(32.f), FloatValue(1.f));
    addSliderSetting("thickness", L"Thickness", L"Outline thickness.", thickness, FloatValue(1.f), FloatValue(5.f),
                     FloatValue(1.f));
    addSetting("fill", L"Fill", L"Also tint the block faces.", fill);
    addSetting("throughWalls", L"See Through Walls (Singleplayer)",
               L"Show outlines through blocks. Only works in your own singleplayer worlds, never on servers or Realms.",
               throughWalls);

    addSetting("showChests", L"Chests", L"Chests and trapped chests.", showChests);
    addSetting("chestColor", L"Chest Color", L"", chestColor, "showChests"_istrue);
    addSetting("showBarrels", L"Barrels", L"", showBarrels);
    addSetting("barrelColor", L"Barrel Color", L"", barrelColor, "showBarrels"_istrue);
    addSetting("showShulkers", L"Shulker Boxes", L"Every color of shulker box.", showShulkers);
    addSetting("shulkerColor", L"Shulker Color", L"", shulkerColor, "showShulkers"_istrue);
    addSetting("showEnderChests", L"Ender Chests", L"", showEnderChests);
    addSetting("enderChestColor", L"Ender Chest Color", L"", enderChestColor, "showEnderChests"_istrue);
    addSetting("showHoppers", L"Hoppers", L"Useful for checking sorter lines.", showHoppers);
    addSetting("hopperColor", L"Hopper Color", L"", hopperColor, "showHoppers"_istrue);
    addSetting("showFurnaces", L"Furnaces", L"Furnaces, blast furnaces and smokers.", showFurnaces);
    addSetting("furnaceColor", L"Furnace Color", L"", furnaceColor, "showFurnaces"_istrue);

    listen<TickEvent>(static_cast<EventListenerFunc>(&ChestOutlines::onTick));
    listen<RenderLevelEvent>(static_cast<EventListenerFunc>(&ChestOutlines::onRender));
}

ChestOutlines::Kind ChestOutlines::classify(SDK::BlockLegacy* legacy) {
    if (!legacy) return None;
    if (auto it = kindCache.find(legacy); it != kindCache.end()) return it->second;

    std::string n = legacy->name.getString();
    Kind k = None;
    if (n == "chest" || n == "trapped_chest")
        k = Chest;
    else if (n == "barrel")
        k = Barrel;
    else if (n.ends_with("shulker_box"))
        k = Shulker;
    else if (n == "ender_chest")
        k = EnderChest;
    else if (n == "hopper")
        k = Hopper;
    else if (n == "furnace" || n == "lit_furnace" || n == "blast_furnace" || n == "lit_blast_furnace" ||
             n == "smoker" || n == "lit_smoker")
        k = Furnace;

    kindCache[legacy] = k;
    return k;
}

bool ChestOutlines::canSeeThroughWalls() {
    return std::get<BoolValue>(throughWalls) && wallsAllowed.load();
}

// Runs on the game thread. Decides whether this is your own singleplayer world, and tells you in chat.
void ChestOutlines::updateWallsMode() {
    std::string host;
    auto* info = SDK::RemoteConnectorComposite::getConnectionInfo();
    if (info) host = info->hostIpAddress;

    // singleplayer / your own hosted world reports no host, or a loopback address
    const bool local = host.empty() || host == "127.0.0.1" || host == "localhost" || host == "::1" ||
                       host == "0.0.0.0";
    wallsAllowed = local;

    const bool wanted = std::get<BoolValue>(throughWalls);
    if (!wanted) {
        reportedWalls = false;
        return;
    }
    if (reportedWalls && host == reportedHost) return;
    reportedWalls = true;
    reportedHost = host;

    if (local)
        Latite::getClientMessageQueue().push(std::string(
            "\xC2\xA7" "d[Eternal]\xC2\xA7" "r Chest Outlines: See Through Walls is \xC2\xA7" "aON\xC2\xA7" "r (singleplayer world)."));
    else
        Latite::getClientMessageQueue().push(std::string(
            "\xC2\xA7" "d[Eternal]\xC2\xA7" "r Chest Outlines: See Through Walls is \xC2\xA7" "cOFF\xC2\xA7" "r here, you're on a server/Realm (") +
            host + ").");
}

namespace {
    // Runs fn(ctx) and catches crashes (access violations) instead of letting them close the game.
    bool guardedCall(void (*fn)(void*), void* ctx) {
        __try {
            fn(ctx);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}

struct ChestOutlinesScan {
    ChestOutlines* self;
    SDK::BlockSource* region;
    int px, py, pz, r, yr;
    bool want[7];
    std::vector<std::pair<BlockPos, int>>* out;
};

void ChestOutlines::scanImpl(void* ctxPtr) {
    auto& c = *static_cast<ChestOutlinesScan*>(ctxPtr);
    // stay inside the world's build limits
    int yMin = (std::max)(c.py - c.yr, -64), yMax = (std::min)(c.py + c.yr, 319);
    for (int x = c.px - c.r; x <= c.px + c.r; x++) {
        for (int z = c.pz - c.r; z <= c.pz + c.r; z++) {
            for (int y = yMin; y <= yMax; y++) {
                BlockPos bp { x, y, z };
                auto block = c.region->getBlock(bp); // same lookup WAILA uses
                if (!block) continue;
                auto legacy = block->legacyBlock;
                if (!legacy) continue;
                Kind k = c.self->classify(legacy);
                if (k != None && c.want[k]) c.out->push_back({ bp, static_cast<int>(k) });
            }
        }
    }
}

void ChestOutlines::onTick(Event&) {
    if (failed) return;
    // rescan twice a second; storage doesn't move
    if (++tickCounter % 10 != 0) return;
    updateWallsMode();

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    auto region = ci ? ci->getRegion() : nullptr;
    if (!lp || !region) {
        std::scoped_lock lk(foundLock);
        found.clear();
        return;
    }

    Vec3 p = lp->getPos();
    std::vector<std::pair<BlockPos, int>> raw;
    raw.reserve(256);
    ChestOutlinesScan ctx { this,
                            region,
                            static_cast<int>(std::floor(p.x)),
                            static_cast<int>(std::floor(p.y)),
                            static_cast<int>(std::floor(p.z)),
                            static_cast<int>(std::get<FloatValue>(range)),
                            (std::min)(static_cast<int>(std::get<FloatValue>(range)), 24),
                            { false, std::get<BoolValue>(showChests), std::get<BoolValue>(showBarrels),
                              std::get<BoolValue>(showShulkers), std::get<BoolValue>(showEnderChests),
                              std::get<BoolValue>(showHoppers), std::get<BoolValue>(showFurnaces) },
                            &raw };

    if (!guardedCall(&ChestOutlines::scanImpl, &ctx)) {
        failed = true;
        {
            std::scoped_lock lk(foundLock);
            found.clear();
        }
        Latite::getClientMessageQueue().push(
            std::string("\xC2\xA7" "d[Eternal]\xC2\xA7" "r Chest Outlines hit an error on this game version and paused "
                        "itself. Toggle it off and on to retry."));
        return;
    }

    std::vector<Found> next;
    next.reserve(raw.size());
    for (auto& [pos, k] : raw) next.push_back({ pos, static_cast<Kind>(k) });
    std::scoped_lock lk(foundLock);
    found = std::move(next);
}

void ChestOutlines::onEnable() {
    failed = false; // toggling the module retries after an error
    tickCounter = 9; // scan on the very next tick
    reportedWalls = false;
}

struct ChestOutlinesDraw {
    MCDrawUtil3D* dc;
    std::vector<std::pair<AABB, d2d::Color>>* boxes;
    int layers;
    bool fill;
};

void ChestOutlines::drawImpl(void* ctxPtr) {
    auto& c = *static_cast<ChestOutlinesDraw*>(ctxPtr);
    for (auto& [box, col] : *c.boxes) {
        Vec3 lo = box.lower, hi = box.higher;
        for (int i = 0; i < c.layers; i++) {
            float e = 0.003f + 0.004f * static_cast<float>(i);
            c.dc->drawBox(AABB { Vec3 { lo.x - e, lo.y - e, lo.z - e }, Vec3 { hi.x + e, hi.y + e, hi.z + e } }, col);
        }
        if (c.fill) {
            d2d::Color fc = col.asAlpha(0.18f);
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

void ChestOutlines::onRender(Event& evG) {
    if (failed) return;
    auto& ev = reinterpret_cast<RenderLevelEvent&>(evG);
    if (!SDK::ClientInstance::get()->getLocalPlayer()) return;

    auto colorFor = [&](Kind k) -> d2d::Color {
        switch (k) {
        case Chest: return std::get<ColorValue>(chestColor).getMainColor();
        case Barrel: return std::get<ColorValue>(barrelColor).getMainColor();
        case Shulker: return std::get<ColorValue>(shulkerColor).getMainColor();
        case EnderChest: return std::get<ColorValue>(enderChestColor).getMainColor();
        case Hopper: return std::get<ColorValue>(hopperColor).getMainColor();
        case Furnace: return std::get<ColorValue>(furnaceColor).getMainColor();
        default: return d2d::Colors::WHITE;
        }
    };

    std::vector<std::pair<AABB, d2d::Color>> boxes;
    {
        std::scoped_lock lk(foundLock);
        if (found.empty()) return;
        boxes.reserve(found.size());
        for (auto& f : found) {
            // chests are slightly smaller than a full block
            float in = (f.kind == Chest || f.kind == EnderChest) ? 0.0625f : 0.f;
            Vec3 lo { f.pos.x + in, static_cast<float>(f.pos.y), f.pos.z + in };
            Vec3 hi { f.pos.x + 1.f - in, f.pos.y + (in > 0.f ? 0.875f : 1.f), f.pos.z + 1.f - in };
            boxes.push_back({ AABB { lo, hi }, colorFor(f.kind) });
        }
    }

    // Selection-box material is depth tested (hidden behind blocks); UI color draws on top of everything.
    MCDrawUtil3D dc { SDK::ClientInstance::get()->levelRenderer, ev.getScreenContext(),
                      canSeeThroughWalls() ? SDK::MaterialPtr::getUIColor()
                                           : SDK::MaterialPtr::getSelectionBoxMaterial() };
    ChestOutlinesDraw ctx { &dc, &boxes, (std::max)(1, static_cast<int>(std::get<FloatValue>(thickness))),
                            std::get<BoolValue>(fill) };
    if (!guardedCall(&ChestOutlines::drawImpl, &ctx)) {
        failed = true;
        Latite::getClientMessageQueue().push(
            std::string("\xC2\xA7" "d[Eternal]\xC2\xA7" "r Chest Outlines couldn't draw on this game version and "
                        "paused itself. Toggle it off and on to retry."));
    }
}
