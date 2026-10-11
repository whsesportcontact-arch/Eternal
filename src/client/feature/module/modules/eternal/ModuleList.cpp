#include "pch.h"
#include "ModuleList.h"
#include "client/Latite.h"
#include "client/feature/module/ModuleManager.h"

ModuleList::ModuleList()
    : TextModule("ModuleList", L"Module List", L"Lists all of your enabled modules on the HUD.", HUD, 600.f) {
    addSetting("hideHudModules", L"Hide HUD Modules",
               L"Don't list modules that already draw something on your HUD.", hideHudModules);
    addSetting("sortByLength", L"Sort By Length", L"Longest names at the top, like an ArrayList.", sortByLength);
    this->fillBg = BoolValue(false);
}

std::wstringstream ModuleList::text(bool isDefault, bool inEditor) {
    std::vector<std::wstring> names;
    const bool hideHud = std::get<BoolValue>(hideHudModules);

    Latite::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (!mod || !mod->isEnabled() || !mod->isVisible()) return;
        if (mod.get() == this) return;
        if (hideHud && mod->isHud()) return;
        names.push_back(mod->getDisplayName());
    });

    if (std::get<BoolValue>(sortByLength)) {
        std::sort(names.begin(), names.end(), [](std::wstring const& a, std::wstring const& b) {
            return a.size() != b.size() ? a.size() > b.size() : a < b;
        });
    } else {
        std::sort(names.begin(), names.end());
    }

    // placeholder so the module can still be positioned in the HUD editor
    if (names.empty() && (isDefault || inEditor)) names = { L"Zoom", L"Freelook", L"Toggle Sprint" };

    std::wstringstream wss;
    for (size_t i = 0; i < names.size(); i++) {
        wss << names[i];
        if (i + 1 < names.size()) wss << L"\n";
    }
    return wss;
}
