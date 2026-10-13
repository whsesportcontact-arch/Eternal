#pragma once
#include "../../TextModule.h"
#include <atomic>
#include <deque>
#include <mutex>

// Enta: tells you whether lag is your PC or the server/Realm.
// FPS + frame time, ping, whether the server has stopped sending updates,
// and optionally which Enta modules cost the most time.
class LagCheck : public TextModule {
public:
    LagCheck();

    void onPacketReceive(Event& ev);
    void onPing(Event& ev);
    void onEnable() override;
    void onDisable() override;

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    using Clock = std::chrono::steady_clock;

    ValueType showCause = BoolValue(true);
    ValueType showCosts = BoolValue(false);
    ValueType lowFps = FloatValue(45.f);

    std::atomic<int64_t> lastPacketNs = 0; // Clock time of the last packet from the server
    std::atomic<int> packetsThisSecond = 0;
    std::atomic<int> lastPing = -1;
    std::atomic<int64_t> lastPingNs = 0;

    std::mutex rateLock;
    std::deque<int> packetRates; // packets per second, last 10 seconds
    Clock::time_point rateWindow = Clock::now();
    float avgPing = 0.f;
    int64_t lastAvgSampleNs = 0;
};
