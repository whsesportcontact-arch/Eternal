#include "pch.h"
#include "HealthDisplay.h"

HealthDisplay::HealthDisplay()
    : TextModule("HealthDisplay", L"Health Display", L"Shows your exact health as a number.", HUD) {
    addSetting("asHearts", L"Show As Hearts", L"Show hearts (10 max) instead of health points (20 max).", asHearts);
    this->prefix = TextValue(L"HP: ");
}

std::wstringstream HealthDisplay::text(bool isDefault, bool inEditor) {
    float hp = 20.f;
    auto ci = SDK::ClientInstance::get();
    if (ci) {
        if (auto plr = ci->getLocalPlayer()) hp = plr->getHealth().value_or(20.f);
    }
    if (std::get<BoolValue>(asHearts)) hp /= 2.f;

    std::wstringstream wss;
    wss << std::format(L"{:.1f}", hp);
    return wss;
}
