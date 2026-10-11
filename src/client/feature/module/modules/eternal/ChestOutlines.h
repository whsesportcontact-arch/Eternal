#pragma once
#include "../../Module.h"
#include <unordered_map>
#include <mutex>
#include <atomic>

// Eternal: outlines chests, barrels, shulker boxes and more around you.
// "See Through Walls" only works in your own singleplayer worlds; on servers/Realms
// outlines are hidden behind blocks like normal.
class ChestOutlines : public Module {
public:
    ChestOutlines();

    void onTick(Event& ev);
    void onRender(Event& ev);
    void onEnable() override;

    static void scanImpl(void* ctx);
    static void drawImpl(void* ctx);

private:
    enum Kind : uint8_t { None, Chest, Barrel, Shulker, EnderChest, Hopper, Furnace };

    Kind classify(SDK::BlockLegacy* legacy);
    bool canSeeThroughWalls();
    void updateWallsMode();

    ValueType range = FloatValue(16.f);
    ValueType thickness = FloatValue(2.f);
    ValueType throughWalls = BoolValue(false);
    ValueType fill = BoolValue(false);

    ValueType showChests = BoolValue(true);
    ValueType showBarrels = BoolValue(true);
    ValueType showShulkers = BoolValue(true);
    ValueType showEnderChests = BoolValue(true);
    ValueType showHoppers = BoolValue(false);
    ValueType showFurnaces = BoolValue(false);

    ValueType chestColor = ColorValue(1.f, 0.62f, 0.2f, 1.f);
    ValueType barrelColor = ColorValue(1.f, 0.85f, 0.35f, 1.f);
    ValueType shulkerColor = ColorValue(0.72f, 0.45f, 1.f, 1.f);
    ValueType enderChestColor = ColorValue(0.2f, 0.9f, 0.75f, 1.f);
    ValueType hopperColor = ColorValue(0.7f, 0.7f, 0.75f, 1.f);
    ValueType furnaceColor = ColorValue(1.f, 0.35f, 0.3f, 1.f);

    struct Found {
        BlockPos pos;
        Kind kind;
    };
    std::vector<Found> found;
    std::mutex foundLock;
    std::unordered_map<SDK::BlockLegacy*, Kind> kindCache;
    int tickCounter = 0;
    std::atomic_bool failed = false;
    std::atomic_bool wallsAllowed = false;
    bool reportedWalls = false;
    std::string reportedHost;

    friend struct ChestOutlinesScan;
};
