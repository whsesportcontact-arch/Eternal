#pragma once
#include "../../TextModule.h"
#include <map>

// Eternal: hold a shulker box and press Shift to see what's inside.
class ShulkerPreview : public TextModule {
public:
    ShulkerPreview();

protected:
    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType requireShift = BoolValue(true);
    ValueType maxLines = FloatValue(12.f);
    ValueType groupItems = BoolValue(true);

    // cache so we don't re-read the item every frame
    void* lastStack = nullptr;
    void* lastTag = nullptr;
    int framesSinceRead = 0;
    std::wstring cachedText;

    std::wstring buildText(SDK::ItemStack* stack);
};
