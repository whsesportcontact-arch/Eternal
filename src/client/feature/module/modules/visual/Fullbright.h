#pragma once
#include "../../Module.h"
#include "client/event/events/GammaEvent.h"
#include <atomic>

class Fullbright : public Module {
public:
    Fullbright();

    void onGamma(Event& ev);
    void onUpdate(Event& ev);
    void onEnable() override;

private:
    ValueType gamma = FloatValue(25.f);

    // Enta: notice when the game never asks for brightness (e.g. Vibrant Visuals), and say so
    std::atomic_bool gammaSeen = false;
    std::chrono::steady_clock::time_point enabledAt {};
    bool told = false;
};
