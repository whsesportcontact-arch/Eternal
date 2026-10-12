#pragma once
#include "../../Module.h"

class BlockOutline : public Module {
public:
    BlockOutline();

    void onOutlineSelection(Event& evG);

private:
    ValueType renderThrough = BoolValue(false);
    ValueType transparent = BoolValue(true);
    ValueType outlineVanilla = BoolValue(false);

    ValueType outline = BoolValue(true);
    ValueType outlineColor = ColorValue(0.72f, 0.45f, 1.f, 1.f); // Enta purple
    ValueType thickness = FloatValue(3.f);                        // Enta: nested boxes = thicker line

    ValueType overlay = BoolValue(false);
    ValueType faceOverlay = BoolValue(false);
    ValueType overlayColor = ColorValue(0.55f, 0.24f, 1.f, 0.18f);
};
