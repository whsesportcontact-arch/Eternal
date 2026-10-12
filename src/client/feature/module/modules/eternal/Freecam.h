#pragma once
#include "../../Module.h"

// Enta: singleplayer freecam. Switches you to spectator mode to fly through blocks,
// then puts you back exactly where you were. Needs cheats enabled in the world.
class Freecam : public Module {
public:
    Freecam();

    void onEnable() override;
    void onDisable() override;
    void onTick(Event& ev);

private:
    static bool inSingleplayer();
    static void sendCommand(std::string const& cmd);

    EnumData returnMode;
    ValueType eyeOffset = FloatValue(1.62f);

    bool active = false;
    bool pendingDisable = false;
    Vec3 savedPos {};
};
