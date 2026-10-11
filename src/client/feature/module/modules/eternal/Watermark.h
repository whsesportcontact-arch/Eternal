#pragma once
#include "../../TextModule.h"

// Eternal: shows the client name (and optionally version) on the HUD.
class Watermark : public TextModule {
public:
    Watermark();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType showVersion = BoolValue(true);
    ValueType customText = TextValue(L"Eternal Client");
};
