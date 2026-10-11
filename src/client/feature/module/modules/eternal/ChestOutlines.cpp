#include "pch.h"
#include "ChestOutlines.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/network/RemoteConnectorComposite.h"
#include "mc/common/client/renderer/MaterialPtr.h"
#include <util/DrawUtil3D.h>

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
    if (!std::get<BoolValue>(throughWalls)) return false;
    // Connected to a server or Realm -> never draw through walls.
    auto* info = SDK::RemoteConnectorComposite::getConnectionInfo();
    if (info && !info->hostIpAddress.empty()) return false;
    return true;
}

void ChestOutlines::onTick(Event&) {
    // rescan twice a second; storage doesn't move
    if (++tickCounter % 10 != 0) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    auto region = ci ? ci->getRegion() : nullptr;
    if (!lp || !region) {
        std::scoped_lock lk(foundLock);
        found.clear();
        return;
    }

    const bool wantKind[] = { false,
                              std::get<BoolValue>(showChests),
                              std::get<BoolValue>(showBarrels),
                              std::get<BoolValue>(showShulkers),
                              std::get<BoolValue>(showEnderChests),
                              std::get<BoolValue>(showHoppers),
                              std::get<BoolValue>(showFurnaces) };

    Vec3 p = lp->getPos();
    int px = static_cast<int>(std::floor(p.x)), py = static_cast<int>(std::floor(p.y)),
        pz = static_cast<int>(std::floor(p.z));
    int r = static_cast<int>(std::get<FloatValue>(range));
    int yr = (std::min)(r, 24);

    std::vector<Found> next;
    for (int x = px - r; x <= px + r; x++) {
        for (int z = pz - r; z <= pz + r; z++) {
            for (int y = py - yr; y <= py + yr; y++) {
                auto block = region->getBlock(x, y, z);
                if (!block) continue;
                Kind k = classify(block->legacyBlock);
                if (k != None && wantKind[k]) next.push_back({ BlockPos { x, y, z }, k });
            }
        }
    }

    std::scoped_lock lk(foundLock);
    found = std::move(next);
}

void ChestOutlines::onRender(Event& evG) {
    auto& ev = reinterpret_cast<RenderLevelEvent&>(evG);
    if (!SDK::ClientInstance::get()->getLocalPlayer()) return;

    // Selection-box material is depth tested (hidden behind blocks); UI color draws on top of everything.
    MCDrawUtil3D dc { SDK::ClientInstance::get()->levelRenderer, ev.getScreenContext(),
                      canSeeThroughWalls() ? SDK::MaterialPtr::getUIColor()
                                           : SDK::MaterialPtr::getSelectionBoxMaterial() };

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

    int layers = (std::max)(1, static_cast<int>(std::get<FloatValue>(thickness)));
    const bool doFill = std::get<BoolValue>(fill);

    std::scoped_lock lk(foundLock);
    for (auto& f : found) {
        // chests are slightly smaller than a full block
        float in = (f.kind == Chest || f.kind == EnderChest) ? 0.0625f : 0.f;
        Vec3 lo { f.pos.x + in, static_cast<float>(f.pos.y), f.pos.z + in };
        Vec3 hi { f.pos.x + 1.f - in, f.pos.y + (in > 0.f ? 0.875f : 1.f), f.pos.z + 1.f - in };
        d2d::Color col = colorFor(f.kind);

        for (int i = 0; i < layers; i++) {
            float e = 0.003f + 0.004f * static_cast<float>(i);
            dc.drawBox(AABB { Vec3 { lo.x - e, lo.y - e, lo.z - e }, Vec3 { hi.x + e, hi.y + e, hi.z + e } }, col);
        }

        if (doFill) {
            d2d::Color fc = col.asAlpha(0.18f);
            float e = 0.002f;
            Vec3 a { lo.x - e, lo.y - e, lo.z - e }, b { hi.x + e, hi.y + e, hi.z + e };
            dc.fillQuad({ a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z }, fc); // top
            dc.fillQuad({ a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, a.y, b.z }, { a.x, a.y, b.z }, fc); // bottom
            dc.fillQuad({ a.x, a.y, a.z }, { a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, a.y, a.z }, fc); // north
            dc.fillQuad({ a.x, a.y, b.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z }, fc); // south
            dc.fillQuad({ a.x, a.y, a.z }, { a.x, b.y, a.z }, { a.x, b.y, b.z }, { a.x, a.y, b.z }, fc); // west
            dc.fillQuad({ b.x, a.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { b.x, a.y, b.z }, fc); // east
        }
    }
    dc.flush();
}
