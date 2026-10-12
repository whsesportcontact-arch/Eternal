#pragma once
#include "../../TextModule.h"

// Enta: how long you've been in the current world/server.
class SessionTimer : public TextModule {
public:
    SessionTimer();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType resetOnJoin = BoolValue(true);

    using clock = std::chrono::steady_clock;
    clock::time_point injectTime = clock::now();
    clock::time_point joinTime = clock::now();
    bool wasInWorld = false;
};
