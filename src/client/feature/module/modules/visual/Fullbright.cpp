#include "pch.h"
#include "Fullbright.h"
#include "client/Latite.h"
#include "client/misc/Notifications.h"

Fullbright::Fullbright()
    : Module("Fullbright", LocalizeString::get("client.module.fullbright.name"),
             LocalizeString::get("client.module.fullbright.desc"), GAME) {
    addSliderSetting("gamma", LocalizeString::get("client.module.fullbright.gamma.name"),
                     LocalizeString::get("client.module.fullbright.gamma.desc"), this->gamma, FloatValue(0.f),
                     FloatValue(25.f), FloatValue(1.f));
    listen<GammaEvent>(&Fullbright::onGamma);
    listen<UpdateEvent>(&Fullbright::onUpdate);
}

void Fullbright::onEnable() {
    gammaSeen = false;
    told = false;
    enabledAt = std::chrono::steady_clock::now();
}

// Fullbright works by changing the game's brightness setting. If the game hasn't read that setting a few
// seconds into a world, it isn't using it (Vibrant Visuals has its own lighting), so tell the player why.
void Fullbright::onUpdate(Event&) {
    if (told || gammaSeen) return;
    auto* ci = SDK::ClientInstance::get();
    if (!ci || !ci->getLocalPlayer()) {
        enabledAt = std::chrono::steady_clock::now(); // start counting once you're in a world
        return;
    }
    if (std::chrono::steady_clock::now() - enabledAt < std::chrono::seconds(5)) return;
    told = true;
    Latite::getNotifications().push(
        L"Fullbright can't change brightness here. If Vibrant Visuals is on, set Video > Graphics Mode to Fancy.");
}

void Fullbright::onGamma(Event& genericEv) {
    auto& ev = reinterpret_cast<GammaEvent&>(genericEv);
    gammaSeen = true;
    ev.getGamma() = std::get<FloatValue>(this->gamma);
}
