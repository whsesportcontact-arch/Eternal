#pragma once
#include "../../TextModule.h"

// Eternal: an "ArrayList" that lists every module you currently have enabled.
class ModuleList : public TextModule {
public:
    ModuleList();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType hideHudModules = BoolValue(true);
    ValueType sortByLength = BoolValue(true);
};
