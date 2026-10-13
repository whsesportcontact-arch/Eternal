#include "pch.h"
#include "LagCheck.h"
#include "client/Latite.h"
#include "client/feature/module/ModuleManager.h"
#include "client/misc/ModuleProfiler.h"
#include "client/event/events/AveragePingEvent.h"
#include "mc/common/network/RemoteConnectorComposite.h"

namespace {
    int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
}

LagCheck::LagCheck()
    : TextModule("LagCheck", L"Lag Check",
                 L"Shows if lag is coming from your PC, your connection, or the server/Realm itself.", HUD, 500.f) {
    addSetting("showCause", L"Show Lag Cause", L"Adds a line saying where the lag is coming from.", showCause);
    addSliderSetting("lowFps", L"Low FPS Below", L"Below this FPS, Lag Check says your PC is the problem.", lowFps,
                     FloatValue(20.f), FloatValue(120.f), FloatValue(5.f), "showCause"_istrue);
    addSetting("showCosts", L"Show Module Costs",
               L"Shows which Enta modules use the most time per frame. Measuring this costs a tiny bit of FPS itself, "
               L"so turn it off when you're done.",
               showCosts);

    listen<PacketReceiveEvent>(static_cast<EventListenerFunc>(&LagCheck::onPacketReceive));
    listen<AveragePingEvent>(static_cast<EventListenerFunc>(&LagCheck::onPing));
}

void LagCheck::onEnable() {
    lastPacketNs = 0;
    packetsThisSecond = 0;
    std::lock_guard guard(rateLock);
    packetRates.clear();
    rateWindow = Clock::now();
}

void LagCheck::onDisable() {
    ModuleProfiler::enabled = false;
    ModuleProfiler::reset();
}

void LagCheck::onPacketReceive(Event&) {
    lastPacketNs = nowNs();
    packetsThisSecond.fetch_add(1, std::memory_order_relaxed);
}

void LagCheck::onPing(Event& evG) {
    auto& ev = static_cast<AveragePingEvent&>(evG);
    lastPing = ev.getPing();
    lastPingNs = nowNs();
}

std::wstringstream LagCheck::text(bool isDefault, bool inEditor) {
    std::wstringstream wss;

    auto* ci = SDK::ClientInstance::get();
    const bool inWorld = ci && ci->getLocalPlayer();

    const bool wantCosts = std::get<BoolValue>(showCosts) && isEnabled();
    if (ModuleProfiler::enabled != wantCosts) {
        ModuleProfiler::enabled = wantCosts;
        ModuleProfiler::reset();
    }

    if (!inWorld) {
        if (isDefault || inEditor) {
            wss << L"FPS: 70 (14.3 ms)\nPing: 85 ms\nServer: OK";
            if (std::get<BoolValue>(showCause)) wss << L"\nLag: none";
        }
        return wss;
    }

    // ---- FPS
    auto& timings = Latite::get().getTimings();
    const int fps = timings.getFPS();
    wss << std::format(L"FPS: {} ({:.1f} ms)", fps, timings.getFrameTime());

    // ---- packet flow (roll the 1 second window)
    double avgRate = 0.0;
    {
        std::lock_guard guard(rateLock);
        auto now = Clock::now();
        if (now - rateWindow >= 1s) {
            packetRates.push_back(packetsThisSecond.exchange(0));
            while (packetRates.size() > 10) packetRates.pop_front();
            rateWindow = now;
        }
        for (int r : packetRates) avgRate += r;
        if (!packetRates.empty()) avgRate /= static_cast<double>(packetRates.size());
    }

    const int64_t last = lastPacketNs.load();
    const double gapSec = last == 0 ? 0.0 : static_cast<double>(nowNs() - last) / 1e9;
    // only judge silence if the server normally sends a steady stream
    const bool steady = avgRate >= 5.0;
    const bool stalled = steady && gapSec >= 2.0;
    const bool slow = steady && gapSec >= 1.0;

    // ---- ping
    const int ping = lastPing.load();
    const bool pingFresh = ping >= 0 && nowNs() - lastPingNs.load() < 5'000'000'000LL;
    bool pingSpike = false;
    if (pingFresh) {
        // update the running average once per new ping sample, not once per frame
        const int64_t sampleNs = lastPingNs.load();
        if (sampleNs != lastAvgSampleNs) {
            lastAvgSampleNs = sampleNs;
            avgPing = avgPing <= 0.f ? static_cast<float>(ping) : avgPing * 0.9f + static_cast<float>(ping) * 0.1f;
        }
        pingSpike = ping > avgPing * 1.5f + 30.f;
        wss << std::format(L"\nPing: {} ms{}", ping, pingSpike ? L" (spike)" : L"");
    } else {
        std::string host;
        if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) host = info->hostIpAddress;
        wss << (host.empty() || host == "127.0.0.1" ? L"\nPing: local world" : L"\nPing: not reported");
    }

    // ---- server
    if (stalled)
        wss << std::format(L"\nServer: not responding ({:.1f}s)", gapSec);
    else if (slow)
        wss << L"\nServer: slow";
    else
        wss << L"\nServer: OK";

    // ---- verdict
    if (std::get<BoolValue>(showCause)) {
        const float low = std::get<FloatValue>(lowFps);
        if (stalled || slow)
            wss << L"\nLag: the server/Realm";
        else if (pingSpike || (pingFresh && ping > 250))
            wss << L"\nLag: your connection";
        else if (fps > 0 && fps < low)
            wss << L"\nLag: your PC (rendering)";
        else
            wss << L"\nLag: none";
    }

    // ---- module costs
    if (wantCosts) {
        auto samples = ModuleProfiler::results();
        const double perFrame = 1.0 / std::max(fps, 1);

        // match timings to modules; anything else is internal client code and isn't counted
        std::unordered_map<const void*, std::wstring> names;
        Latite::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (mod) names[static_cast<Listener*>(mod.get())] = mod->getDisplayName();
        });

        double total = 0.0;
        std::vector<std::pair<std::wstring, double>> top;
        for (auto& s : samples) {
            auto it = names.find(s.owner);
            if (it == names.end()) continue;
            const double ms = s.msPerSecond * perFrame;
            total += ms;
            if (top.size() < 3) top.emplace_back(it->second, ms);
        }

        wss << std::format(L"\nEnta modules: {:.2f} ms/frame", total);
        for (auto& [name, ms] : top) wss << std::format(L"\n  {}: {:.2f} ms", name, ms);
    }

    return wss;
    }

    // ---- FPS
    auto& timings = Latite::get().getTimings();
    const int fps = timings.getFPS();
    wss << std::format(L"FPS: {} ({:.1f} ms)", fps, timings.getFrameTime());

    // ---- packet flow (roll the 1 second window)
    double avgRate = 0.0;
    {
        std::lock_guard guard(rateLock);
        auto now = Clock::now();
        if (now - rateWindow >= 1s) {
            packetRates.push_back(packetsThisSecond.exchange(0));
            while (packetRates.size() > 10) packetRates.pop_front();
            rateWindow = now;
        }
        for (int r : packetRates) avgRate += r;
        if (!packetRates.empty()) avgRate /= static_cast<double>(packetRates.size());
    }

    const int64_t last = lastPacketNs.load();
    const double gapSec = last == 0 ? 0.0 : static_cast<double>(nowNs() - last) / 1e9;
    // only judge silence if the server normally sends a steady stream
    const bool steady = avgRate >= 5.0;
    const bool stalled = steady && gapSec >= 2.0;
    const bool slow = steady && gapSec >= 1.0;

    // ---- ping
    const int ping = lastPing.load();
    const bool pingFresh = ping >= 0 && nowNs() - lastPingNs.load() < 5'000'000'000LL;
    bool pingSpike = false;
    if (pingFresh) {
        avgPing = avgPing <= 0.f ? static_cast<float>(ping) : avgPing * 0.95f + static_cast<float>(ping) * 0.05f;
        pingSpike = ping > avgPing * 1.5f + 30.f;
        wss << std::format(L"\nPing: {} ms{}", ping, pingSpike ? L" (spike)" : L"");
    } else {
        std::string host;
        if (auto* info = SDK::RemoteConnectorComposite::getConnectionInfo()) host = info->hostIpAddress;
        wss << (host.empty() || host == "127.0.0.1" ? L"\nPing: local world" : L"\nPing: not reported");
    }

    // ---- server
    if (stalled)
        wss << std::format(L"\nServer: not responding ({:.1f}s)", gapSec);
    else if (slow)
        wss << L"\nServer: slow";
    else
        wss << L"\nServer: OK";

    // ---- verdict
    if (std::get<BoolValue>(showCause)) {
        const float low = std::get<FloatValue>(lowFps);
        if (stalled || slow)
            wss << L"\nLag: the server/Realm";
        else if (pingSpike || (pingFresh && ping > 250))
            wss << L"\nLag: your connection";
        else if (fps > 0 && fps < low)
            wss << L"\nLag: your PC (rendering)";
        else
            wss << L"\nLag: none";
    }

    // ---- module costs
    if (wantCosts) {
        auto samples = ModuleProfiler::results();
        const double perFrame = 1.0 / std::max(fps, 1);
        double total = 0.0;
        for (auto& s : samples) total += s.msPerSecond * perFrame;
        wss << std::format(L"\nEnta modules: {:.2f} ms/frame", total);

        int shown = 0;
        for (auto& s : samples) {
            if (shown >= 3) break;
            std::wstring name;
            Latite::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
                if (mod && static_cast<const void*>(static_cast<Listener*>(mod.get())) == s.owner)
                    name = mod->getDisplayName();
            });
            if (name.empty()) continue; // not a module (internal client code)
            wss << std::format(L"\n  {}: {:.2f} ms", name, s.msPerSecond * perFrame);
            shown++;
        }
    }

    return wss;
}
