#include "pch.h"
#include "Watermark.h"
#include "client/Latite.h"

Watermark::Watermark()
    : TextModule("Watermark", L"Watermark", L"Shows the Eternal Client name on your HUD.", HUD) {
    addSetting("customText", L"Text", L"What the watermark says.", customText);
    addSetting("showVersion", L"Show Version", L"Show the client version next to the name.", showVersion);
    // purple by default to match the theme
    this->textColor = ColorValue(0.72f, 0.55f, 1.f, 1.f);
}

std::wstringstream Watermark::text(bool isDefault, bool inEditor) {
    std::wstringstream wss;
    wss << std::get<TextValue>(customText).str;
    if (std::get<BoolValue>(showVersion)) {
        wss << L" " << util::StrToWStr(std::string(Latite::version));
    }
    return wss;
}
