#pragma once
#include "../../TextModule.h"

// Eternal: your exact health as a number.
class HealthDisplay : public TextModule {
public:
    HealthDisplay();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType asHearts = BoolValue(true);
};
