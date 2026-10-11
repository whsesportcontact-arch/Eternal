#include "pch.h"
#include "SessionTimer.h"

SessionTimer::SessionTimer()
    : TextModule("SessionTimer", L"Session Timer", L"Shows how long you've been playing this session.", HUD) {
    addSetting("resetOnJoin", L"Reset On Join",
               L"Restart the timer every time you join a world or server. Off = time since the client loaded.",
               resetOnJoin);
    this->prefix = TextValue(L"Session: ");
}

std::wstringstream SessionTimer::text(bool isDefault, bool inEditor) {
    auto ci = SDK::ClientInstance::get();
    const bool inWorld = ci && ci->getLocalPlayer() != nullptr;
    if (inWorld && !wasInWorld) joinTime = clock::now();
    wasInWorld = inWorld;

    auto start = std::get<BoolValue>(resetOnJoin) ? joinTime : injectTime;
    if (std::get<BoolValue>(resetOnJoin) && !inWorld) start = clock::now();

    auto total = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start).count();
    long long h = total / 3600, m = (total / 60) % 60, s = total % 60;

    std::wstringstream wss;
    if (h > 0)
        wss << std::format(L"{}:{:02}:{:02}", h, m, s);
    else
        wss << std::format(L"{:02}:{:02}", m, s);
    return wss;
}
