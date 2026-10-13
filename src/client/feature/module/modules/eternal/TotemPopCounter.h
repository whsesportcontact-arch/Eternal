#pragma once
#include "../../Module.h"
#include "util/DxUtil.h"
#include <unordered_map>
#include <mutex>

// Enta: counts how many totems each player has popped and shows it next to their nametag.
// Uses the totem pop event the server already sends to everyone (the same one that plays
// the totem animation and particles), so it only shows what you could already see.
class TotemPopCounter : public Module {
public:
    TotemPopCounter();

    void onPacketReceive(Event& ev);
    void onRenderNameTag(Event& ev);
    void onTick(Event& ev);
    void onLeaveGame(Event& ev);
    void onRenderLayer(Event& ev);
    void onDisable() override;

private:
    struct Entry {
        std::string playerName; // raw player name, used as a fallback when the nametag changes
        std::string nameTag;    // nametag exactly as the game renders it
        int pops = 0;
    };

    static std::string stripFormatting(std::string const& s);
    static bool containsName(std::string const& haystack, std::string const& name);
    std::string formatCount(int pops) const;
    std::string colorCode() const;
    d2d::Color drawColor() const;
    void refreshNames(SDK::Level* level);

    EnumData style;
    EnumData color;
    ValueType aboveHead = BoolValue(true);
    ValueType onNametag = BoolValue(false);
    ValueType chatAlerts = BoolValue(false);
    ValueType countSelf = BoolValue(true);
    ValueType resetOnDeath = BoolValue(true);

    std::mutex lock;
    std::unordered_map<uint64_t, Entry> entries;
    int tickCounter = 0;
};
