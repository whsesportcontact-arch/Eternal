#pragma once
#include "HUDModule.h"

class TextModule : public HUDModule {
protected:
    ValueType customSize = BoolValue(false);

    ValueType fillBg = BoolValue(true);

    ValueType bgX = FloatValue(150.f);
    ValueType bgY = FloatValue(45.f);

    ValueType padX = FloatValue(22.f);
    ValueType padY = FloatValue(4.f);

    ValueType textSizeS = FloatValue(30.f);

    ValueType textColor = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType bgColor = ColorValue(0.02f, 0.01f, 0.04f, 0.78f);
    ValueType starryBg = BoolValue(true); // Enta: animated stars behind the text

    ValueType prefix = TextValue(L"");
    ValueType suffix = TextValue(L"");

    ValueType showOutline = BoolValue(true);

    ValueType outlineThickness = FloatValue(1.5f);
    ValueType outlineColor = ColorValue(0.62f, 0.36f, 1.f, 0.85f);

    ValueType radius = FloatValue(4.f);

    static constexpr int alignment_center = 0;
    static constexpr int alignment_left = 1;
    static constexpr int alignment_right = 2;
    EnumData alignment;

public:
    TextModule(std::string const& name, std::wstring const& displayName, std::wstring const& description,
               Category category, float maxBgX = 400.f, int keybind = 0, bool cacheText = false)
        : HUDModule(name, displayName, description, category, keybind, true)
        , maxBGX(maxBgX)
        , cacheText(cacheText) {
        this->textual = true;
    }
    TextModule(std::string const& name, LocalizedString const& displayName, LocalizedString const& description,
               Category category, float maxBgX = 400.f, int keybind = 0, bool cacheText = false)
        : TextModule(name, displayName.value(), description.value(), category, maxBgX, keybind, cacheText) {
        setDisplayName(displayName);
        setDescription(description);
    }
    TextModule(std::string const& name, LocalizedString const& displayName, std::wstring const& description,
               Category category, float maxBgX = 400.f, int keybind = 0, bool cacheText = false)
        : TextModule(name, displayName.value(), description, category, maxBgX, keybind, cacheText) {
        setDisplayName(displayName);
    }
    TextModule(std::string const& name, std::wstring const& displayName, LocalizedString const& description,
               Category category, float maxBgX = 400.f, int keybind = 0, bool cacheText = false)
        : TextModule(name, displayName, description.value(), category, maxBgX, keybind, cacheText) {
        setDescription(description);
    }

    virtual ~TextModule() = default;

    void onInit() override;

    void render(DrawUtil& ctx, bool isDefault, bool inEditor) override;
    std::wstring getLastText() { return lastText; }

protected:
    virtual std::wstringstream text(bool isDefault, bool inEditor) = 0;
    bool cacheText;
    std::wstring lastText;
    FloatValue maxBGX = 0.f;

private:
    std::wstringstream processText(std::wstringstream& stream);
    void drawStarsIn(DrawUtil& dc, d2d::Rect const& rc, float rad);
};
