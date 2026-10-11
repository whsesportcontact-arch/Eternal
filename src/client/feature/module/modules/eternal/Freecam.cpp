#include "pch.h"
#include "Freecam.h"
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"
#include "client/event/events/TickEvent.h"
#include "mc/common/network/RemoteConnectorComposite.h"
#include "mc/common/network/MinecraftPackets.h"
#include <mc/common/network/packet/CommandRequestPacket.h>

namespace {
    void eternalMsg(std::string const& s) {
        Latite::getClientMessageQueue().push(std::string("\xC2\xA7" "d[Eternal]\xC2\xA7" "r ") + s);
    }
}

Freecam::Freecam()
    : Module("Freecam", L"Freecam",
             L"Singleplayer only: fly through blocks in spectator mode, then snap back to where you were. "
             L"Needs cheats on in the world settings.",
             GAME) {
    returnMode.addEntry({ 0, L"Survival" });
    returnMode.addEntry({ 1, L"Creative" });
    returnMode.addEntry({ 2, L"Adventure" });
    addEnumSetting("returnMode", L"Return To", L"Which game mode to put you back in when freecam turns off.",
                   returnMode);
    addSliderSetting("eyeOffset", L"Height Fix",
                     L"Only change this if you end up slightly above/below where you started.", eyeOffset,
                     FloatValue(0.f), FloatValue(2.f), FloatValue(0.01f));

    listen<TickEvent>(static_cast<EventListenerFunc>(&Freecam::onTick));
}

bool Freecam::inSingleplayer() {
    std::string host;
    auto* info = SDK::RemoteConnectorComposite::getConnectionInfo();
    if (info) host = info->hostIpAddress;
    return host.empty() || host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "0.0.0.0";
}

void Freecam::sendCommand(std::string const& cmd) {
    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    if (!lp) return;
    auto pkt = SDK::MinecraftPackets::createPacket(SDK::PacketID::COMMAND_REQUEST);
    auto* req = reinterpret_cast<SDK::CommandRequestPacket*>(pkt.get());
    req->applyCommand(cmd);
    lp->packetSender->sendToServer(pkt.get());
}

void Freecam::onEnable() {
    active = false;
    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    if (!lp) {
        pendingDisable = true;
        return;
    }
    if (!inSingleplayer()) {
        eternalMsg("Freecam only works in your own singleplayer worlds.");
        pendingDisable = true;
        return;
    }

    savedPos = lp->getPos();
    savedPos.y -= std::get<FloatValue>(eyeOffset); // player position is at eye level; /tp wants feet
    active = true;
    sendCommand("/gamemode spectator");
    eternalMsg("Freecam on. Press the key again to snap back. (If nothing happened, turn on cheats in World Settings.)");
}

void Freecam::onDisable() {
    pendingDisable = false;
    if (!active) return;
    active = false;

    static const char* modes[] = { "survival", "creative", "adventure" };
    int idx = std::clamp(returnMode.getSelectedKey(), 0, 2);
    sendCommand(std::format("/tp @s {:.2f} {:.2f} {:.2f}", savedPos.x, savedPos.y, savedPos.z));
    sendCommand(std::string("/gamemode ") + modes[idx]);
}

void Freecam::onTick(Event&) {
    // turning ourselves off from inside onEnable isn't safe, so do it on the next tick
    if (pendingDisable && isEnabled()) setEnabled(false);
}
