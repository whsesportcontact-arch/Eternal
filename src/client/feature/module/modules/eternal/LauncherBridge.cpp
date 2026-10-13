#include "pch.h"
#include "LauncherBridge.h"
#include "client/Latite.h"
#include "client/config/ConfigManager.h"
#include "client/feature/module/ModuleManager.h"
#include "util/Util.h"
#include "client/misc/Notifications.h"

#include <wincodec.h>
#include <wrl/client.h>

namespace fs = std::filesystem;

namespace {
    // keep tabs/newlines out of the tab-separated files
    std::string clean(std::string s) {
        for (auto& c : s) {
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        }
        return s;
    }

    bool writeAtomically(fs::path const& path, std::string const& data) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!f) return false;
        }
        fs::rename(tmp, path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

    bool writeSkinPng(fs::path const& path, uint32_t width, uint32_t height, unsigned char const* rgba) {
        IWICImagingFactory2* factory = Latite::getRenderer().getImagingFactory();
        if (!factory) return false;

        std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < bgra.size(); i += 4) {
            bgra[i] = rgba[i + 2];
            bgra[i + 1] = rgba[i + 1];
            bgra[i + 2] = rgba[i];
            bgra[i + 3] = rgba[i + 3];
        }

        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path;
        tmp += ".tmp";
        fs::remove(tmp, ec);

        bool ok = [&]() -> bool {
            using Microsoft::WRL::ComPtr;
            ComPtr<IWICStream> stream;
            if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
            if (FAILED(stream->InitializeFromFilename(tmp.wstring().c_str(), GENERIC_WRITE))) return false;
            ComPtr<IWICBitmapEncoder> encoder;
            if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
            if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> props;
            if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()))) return false;
            if (FAILED(frame->Initialize(props.Get()))) return false;
            if (FAILED(frame->SetSize(width, height))) return false;
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            if (FAILED(frame->SetPixelFormat(&fmt)) || !IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) return false;
            if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data()))) return false;
            if (FAILED(frame->Commit())) return false;
            return SUCCEEDED(encoder->Commit());
        }();

        if (!ok) {
            fs::remove(tmp, ec);
            return false;
        }
        fs::rename(tmp, path, ec);
        if (ec) fs::remove(tmp, ec);
        return !ec;
    }
}

LauncherBridge::LauncherBridge()
    : Module("LauncherBridge", L"Launcher Bridge", L"Shares your skin and module list with the Enta Launcher.", GAME,
             nokeybind, false, /*visible*/ false) {
    // runs whether or not the module is "enabled"; it's an internal helper
    listen<TickEvent>(static_cast<EventListenerFunc>(&LauncherBridge::onTick), true);
    listen<UpdateEvent>(static_cast<EventListenerFunc>(&LauncherBridge::onUpdate), true);
}

fs::path LauncherBridge::folder() {
    return util::GetLatitePath() / "Launcher";
}

// Modules you switched on/off in the launcher before starting the game.
bool LauncherBridge::applyOverrides() {
    const fs::path path = folder() / "module_overrides.tsv";
    std::ifstream f(path);
    if (!f) return false;

    bool changed = false;
    for (std::string line; std::getline(f, line);) {
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string name = line.substr(0, tab);
        const bool on = line.compare(tab + 1, 1, "1") == 0;

        auto mod = Latite::getModuleManager().find(name);
        if (!mod || !mod->isVisible() || !mod->isToggleable()) continue;
        if (mod->isEnabled() != on) {
            mod->setEnabled(on);
            changed = true;
        }
    }
    f.close();

    std::error_code ec;
    fs::remove(path, ec);
    return changed;
}

void LauncherBridge::writeManifest() {
    std::string out = "# enta-modules 1\n# name\tdisplay name\tcategory\tenabled\n";
    Latite::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        // skip internal helpers and modules that always start off anyway
        if (!mod || !mod->isVisible() || !mod->isToggleable() || !mod->shouldPersistEnabled()) return;
        out += clean(mod->name()) + "\t" + clean(util::WStrToStr(mod->getDisplayName())) + "\t" +
               std::to_string(static_cast<int>(mod->getCategory())) + "\t" + (mod->isEnabled() ? "1" : "0") + "\n";
    });
    writeAtomically(folder() / "modules.tsv", out);
}

void LauncherBridge::onConfigApplied() {
    if (applyOverrides()) Latite::getConfigManager().saveCurrentConfig(); // also rewrites the manifest
    writeManifest();
}

void LauncherBridge::exportSkin(SDK::Level* level) {
    auto* ci = SDK::ClientInstance::get();
    auto* local = ci ? ci->getLocalPlayer() : nullptr;
    if (!level || !local) return;
    auto* players = level->getPlayerList();
    if (!players) return;

    const std::string myName = local->playerName;
    for (auto& [uuid, entry] : *players) {
        if (entry.name != myName) continue;

        SDK::SkinImage const* img = entry.skin.getSkinImage();
        if (!img || !img->hasRgbaBytes()) return;

        // the launcher can preview normal skins (64x64, old 64x32, HD 128x128); persona skins use other layouts
        const bool previewable = (img->width == 64 && (img->height == 64 || img->height == 32)) ||
                                 (img->width == 128 && img->height == 128);
        if (!previewable) return;

        // only rewrite the file when the skin actually changed (FNV-1a hash)
        const size_t size = static_cast<size_t>(img->width) * img->height * 4;
        uint64_t hash = 1469598103934665603ULL ^ (static_cast<uint64_t>(img->width) << 32 | img->height);
        for (size_t i = 0; i < size; i++) {
            hash ^= img->bytes.data()[i];
            hash *= 1099511628211ULL;
        }
        if (hash == lastSkinHash) return;
        if (writeSkinPng(folder() / "skin.png", img->width, img->height, img->bytes.data())) lastSkinHash = hash;
        return;
    }
}

namespace {
    std::optional<std::string> readPendingSkin() {
        std::ifstream f(util::GetLatitePath() / "Launcher" / "pending_skin.txt");
        if (!f) return std::nullopt;
        std::string path;
        std::getline(f, path);
        while (!path.empty() && (path.back() == '\r' || path.back() == ' ')) path.pop_back();
        std::error_code ec;
        if (path.empty() || !fs::is_regular_file(fs::path(util::StrToWStr(path)), ec)) return std::nullopt;
        return path;
    }
}

std::optional<std::string> LauncherBridge::takePendingSkin() {
    auto path = readPendingSkin();
    std::error_code ec;
    fs::remove(folder() / "pending_skin.txt", ec);
    if (path) writeAtomically(folder() / "skin_applied.txt", *path + "\n"); // lets the launcher show it worked
    return path;
}

void LauncherBridge::onTick(Event& evG) {
    auto& ev = reinterpret_cast<TickEvent&>(evG);
    // about every 5 seconds; cheap when nothing changed
    if (++tickCounter % 100 != 1) return;
    exportSkin(ev.getLevel());
}

// runs every frame, also in menus (where the Dressing Room is)
void LauncherBridge::onUpdate(Event&) {
    if (toldAboutSkin || ++frameCounter % 300 != 1) return;

    // once per session, point the way to applying the launcher's skin
    if (readPendingSkin()) {
        toldAboutSkin = true;
        Latite::getNotifications().push(L"Your launcher skin is ready: Dressing Room > Classic Skins > Choose New Skin");
    }
}
