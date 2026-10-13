#include "pch.h"
#include "EntaPack.h"
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"

#include <algorithm>
#include <cctype>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
    constexpr const char* kSect = "\xC2\xA7";
    constexpr const char* kPackFolder = "EntaPack";
    constexpr const char* kSignatureFile = "enta_pack_settings.txt";
    // bump this when the generator changes, so old packs get rebuilt
    constexpr const char* kGeneratorVersion = "1";

    constexpr const char* kHeaderUuid = "cb8b5819-6579-4acf-bb20-8c2509e36c52";
    constexpr const char* kModuleUuid = "ecf7a02c-0d5b-47f2-9f5a-5a1f2c157e52";

    // fire overlay + ground fire share these textures (32-frame flipbooks)
    constexpr std::array kFireTextures = {
        "textures/blocks/fire_0.png",
        "textures/blocks/fire_1.png",
        "textures/blocks/soul_fire_0.png",
        "textures/blocks/soul_fire_1.png",
    };

    // particles that spawn lots of quads during fights
    constexpr std::array kCombatParticles = {
        "totem",           "totem_manual",        "critical_hit",          "magic_critical_hit",
        "explosion_death", "explosion_manual",    "huge_explosion_level",  "splashpotionspell",
        "knockback_roar",  "wind_explosion",      "breeze_wind_explosion",
    };

    // block breaking, smoke and rain
    constexpr std::array kAmbientParticles = {
        "block_destruct", "breaking_item_icon", "campfire_smoke", "campfire_smoke_tall",
        "rain_splash",    "basic_smoke",        "white_smoke",
    };

    void say(std::string const& msg) {
        Latite::getClientMessageQueue().push(std::string(kSect) + "d[Enta]" + kSect + "r " + msg);
    }

    // ---------------------------------------------------------------- paths

    std::optional<fs::path> envPath(const wchar_t* name) {
        wchar_t buf[MAX_PATH] {};
        DWORD len = GetEnvironmentVariableW(name, buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return std::nullopt;
        return fs::path(std::wstring(buf, len));
    }

    // com.mojang/development_resource_packs/EntaPack, for the new launcher (GDK) or the old Store (UWP) version
    std::optional<fs::path> findPackDir() {
        std::error_code ec;
        std::vector<fs::path> roots;
        if (auto appdata = envPath(L"APPDATA"))
            roots.push_back(*appdata / "Minecraft Bedrock" / "Users" / "Shared" / "games" / "com.mojang");
        if (auto local = envPath(L"LOCALAPPDATA"))
            roots.push_back(*local / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "LocalState" / "games" /
                            "com.mojang");

        // If the running game is the old Store (UWP) version, check its folder first.
        wchar_t exe[MAX_PATH] {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (std::wstring(exe).find(L"MinecraftUWP") != std::wstring::npos) std::reverse(roots.begin(), roots.end());

        for (auto& root : roots) {
            if (fs::is_directory(root, ec)) return root / "development_resource_packs" / kPackFolder;
        }
        return std::nullopt;
    }

    std::vector<int> parseVersion(std::string const& s) {
        std::vector<int> out;
        std::stringstream ss(s);
        std::string part;
        while (std::getline(ss, part, '.')) {
            try {
                out.push_back(std::stoi(part));
            } catch (...) {
                out.push_back(0);
            }
        }
        return out;
    }

    // The game's built-in packs: <game folder>/data/resource_packs/vanilla, vanilla_1.20.50, ...
    // Newest folder first, because later folders override older ones.
    std::vector<fs::path> vanillaPackDirs() {
        std::vector<wchar_t> buf(32768);
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};

        fs::path packs = fs::path(std::wstring(buf.data(), len)).parent_path() / "data" / "resource_packs";
        std::error_code ec;
        if (!fs::is_directory(packs, ec)) return {};

        std::vector<std::pair<std::vector<int>, fs::path>> found;
        for (auto& entry : fs::directory_iterator(packs, ec)) {
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (name == "vanilla") {
                found.emplace_back(std::vector<int> {}, entry.path());
            } else if (name.starts_with("vanilla_") && name.size() > 8 && std::isdigit((unsigned char)name[8])) {
                found.emplace_back(parseVersion(name.substr(8)), entry.path());
            }
        }
        std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });

        std::vector<fs::path> out;
        for (auto& [ver, path] : found) out.push_back(path);
        return out;
    }

    std::optional<fs::path> findVanillaFile(std::vector<fs::path> const& packs, std::string const& rel) {
        std::error_code ec;
        for (auto& pack : packs) {
            fs::path p = pack / fs::path(rel);
            if (fs::is_regular_file(p, ec)) return p;
        }
        return std::nullopt;
    }

    // ---------------------------------------------------------------- files

    std::optional<std::string> readFile(fs::path const& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return std::nullopt;
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    bool writeFile(fs::path const& p, std::string const& data) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(f);
    }

    std::optional<json> readJson(fs::path const& p) {
        auto text = readFile(p);
        if (!text) return std::nullopt;
        // vanilla files sometimes have comments in them
        json j = json::parse(*text, nullptr, false, true);
        if (j.is_discarded()) return std::nullopt;
        return j;
    }

    // ---------------------------------------------------------------- images (WIC)

    struct PackImage {
        UINT width = 0, height = 0;
        std::vector<uint8_t> px; // 32bpp BGRA, non-premultiplied

        uint8_t* at(UINT x, UINT y) { return &px[(static_cast<size_t>(y) * width + x) * 4]; }
    };

    std::optional<PackImage> loadPng(IWICImagingFactory* factory, fs::path const& p) {
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(p.wstring().c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())))
            return std::nullopt;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return std::nullopt;
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(factory->CreateFormatConverter(conv.GetAddressOf()))) return std::nullopt;
        if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom)))
            return std::nullopt;

        PackImage img;
        if (FAILED(conv->GetSize(&img.width, &img.height)) || img.width == 0 || img.height == 0) return std::nullopt;
        img.px.resize(static_cast<size_t>(img.width) * img.height * 4);
        if (FAILED(conv->CopyPixels(nullptr, img.width * 4, static_cast<UINT>(img.px.size()), img.px.data())))
            return std::nullopt;
        return img;
    }

    bool savePng(IWICImagingFactory* factory, fs::path const& p, PackImage& img) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        fs::remove(p, ec);

        bool ok = [&]() -> bool {
            ComPtr<IWICStream> stream;
            if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
            if (FAILED(stream->InitializeFromFilename(p.wstring().c_str(), GENERIC_WRITE))) return false;
            ComPtr<IWICBitmapEncoder> encoder;
            if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
            if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> props;
            if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()))) return false;
            if (FAILED(frame->Initialize(props.Get()))) return false;
            if (FAILED(frame->SetSize(img.width, img.height))) return false;
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            if (FAILED(frame->SetPixelFormat(&fmt)) || !IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) return false;
            if (FAILED(frame->WritePixels(img.height, img.width * 4, static_cast<UINT>(img.px.size()), img.px.data())))
                return false;
            if (FAILED(frame->Commit())) return false;
            return SUCCEEDED(encoder->Commit());
        }();

        if (!ok) fs::remove(p, ec);
        return ok;
    }

    // Squash every square frame of a flipbook down to `scale` of its height, keeping it on the bottom edge.
    // This makes both the on-screen fire and fire on the ground shorter.
    PackImage squashFrames(PackImage& src, float scale) {
        PackImage out { src.width, src.height, std::vector<uint8_t>(src.px.size(), 0) };
        const UINT w = src.width;
        const UINT frames = std::max(1u, src.height / w);
        const UINT newH = std::clamp<UINT>(static_cast<UINT>(std::lround(w * scale)), 1, w);

        for (UINT f = 0; f < frames; f++) {
            const UINT top = f * w;
            for (UINT y = 0; y < newH; y++) {
                const UINT srcY = std::min(w - 1, static_cast<UINT>(static_cast<uint64_t>(y) * w / newH));
                for (UINT x = 0; x < w; x++) {
                    if (top + srcY >= src.height || top + (w - newH) + y >= src.height) continue;
                    memcpy(out.at(x, top + (w - newH) + y), src.at(x, top + srcY), 4);
                }
            }
        }
        return out;
    }

    // Shrink an image toward its center, with transparent space around it.
    PackImage shrinkCentered(PackImage& src, float scale) {
        PackImage out { src.width, src.height, std::vector<uint8_t>(src.px.size(), 0) };
        const UINT nw = std::clamp<UINT>(static_cast<UINT>(std::lround(src.width * scale)), 1, src.width);
        const UINT nh = std::clamp<UINT>(static_cast<UINT>(std::lround(src.height * scale)), 1, src.height);
        const UINT ox = (src.width - nw) / 2;
        const UINT oy = (src.height - nh) / 2;

        for (UINT y = 0; y < nh; y++) {
            const UINT sy = std::min(src.height - 1, static_cast<UINT>(static_cast<uint64_t>(y) * src.height / nh));
            for (UINT x = 0; x < nw; x++) {
                const UINT sx = std::min(src.width - 1, static_cast<UINT>(static_cast<uint64_t>(x) * src.width / nw));
                memcpy(out.at(ox + x, oy + y), src.at(sx, sy), 4);
            }
        }
        return out;
    }

    // Small purple diamond on near-black, matching the Enta theme.
    PackImage makeIcon() {
        PackImage img { 64, 64, std::vector<uint8_t>(64 * 64 * 4) };
        for (UINT y = 0; y < 64; y++) {
            for (UINT x = 0; x < 64; x++) {
                uint8_t* p = img.at(x, y);
                int d = std::abs(static_cast<int>(x) - 32) + std::abs(static_cast<int>(y) - 32);
                if (d < 18) { // accent #8B3DFF
                    p[0] = 0xFF, p[1] = 0x3D, p[2] = 0x8B;
                } else if (d < 22) { // lighter rim
                    p[0] = 0xFF, p[1] = 0x9A, p[2] = 0xC8;
                } else { // background #12081F
                    p[0] = 0x1F, p[1] = 0x08, p[2] = 0x12;
                }
                p[3] = 0xFF;
            }
        }
        return img;
    }

    // ---------------------------------------------------------------- json edits

    json lowerBy(json const& v, float amount) {
        if (v.is_number()) return v.get<double>() - amount;
        if (v.is_string()) return "(" + v.get<std::string>() + ") - " + std::format("{:g}", amount);
        return v;
    }

    void lowerPositionY(json& pos, float amount) {
        if (pos.is_array() && pos.size() >= 2) {
            pos[1] = lowerBy(pos[1], amount);
        }
    }

    // Lower the shield in first person only, so other players still see it normally.
    bool lowerShield(json& j, float amount) {
        if (!j.contains("animations") || !j["animations"].is_object()) return false;
        bool changed = false;

        for (auto& [name, anim] : j["animations"].items()) {
            if (name.find("first_person") == std::string::npos || !anim.contains("bones")) continue;

            for (auto& [boneName, bone] : anim["bones"].items()) {
                if (!bone.contains("position")) continue;
                json& pos = bone["position"];

                if (pos.is_array()) {
                    lowerPositionY(pos, amount);
                    changed = true;
                } else if (pos.is_object()) { // keyframes: "0.1": [x, y, z] or { "pre": [...], "post": [...] }
                    for (auto& [time, kf] : pos.items()) {
                        if (kf.is_array()) {
                            lowerPositionY(kf, amount);
                        } else if (kf.is_object()) {
                            if (kf.contains("pre")) lowerPositionY(kf["pre"], amount);
                            if (kf.contains("post")) lowerPositionY(kf["post"], amount);
                        }
                    }
                    changed = true;
                }
            }
        }
        return changed;
    }

    json scaleCount(json const& v, float k) {
        if (k <= 0.f) return 0;
        if (v.is_number()) return std::max(1.0, std::floor(v.get<double>() * k));
        if (v.is_string()) return std::format("math.max(1, math.floor(({}) * {:g}))", v.get<std::string>(), k);
        return v;
    }

    bool scaleParticles(json& j, float k) {
        if (!j.contains("particle_effect") || !j["particle_effect"].contains("components")) return false;
        json& comps = j["particle_effect"]["components"];
        bool changed = false;

        auto scaleFields = [&](const char* component, std::initializer_list<const char*> fields) {
            if (!comps.contains(component) || !comps[component].is_object()) return;
            for (auto* field : fields) {
                if (comps[component].contains(field)) {
                    comps[component][field] = scaleCount(comps[component][field], k);
                    changed = true;
                }
            }
        };
        scaleFields("minecraft:emitter_rate_instant", { "num_particles" });
        scaleFields("minecraft:emitter_rate_steady", { "spawn_rate", "max_particles" });
        scaleFields("minecraft:emitter_rate_manual", { "max_particles" });
        return changed;
    }

    std::string manifest() {
        json m = {
            { "format_version", 2 },
            { "header",
              {
                  { "name", "Enta Pack" },
                  { "description", "Made by Enta Client: low fire, low shield, small totem, fewer particles." },
                  { "uuid", kHeaderUuid },
                  { "version", { 1, 0, 0 } },
                  { "min_engine_version", { 1, 20, 0 } },
              } },
            { "modules",
              json::array({ {
                  { "type", "resources" },
                  { "uuid", kModuleUuid },
                  { "version", { 1, 0, 0 } },
                  { "description", "Enta Pack" },
              } }) },
        };
        return m.dump(4);
    }
}

std::string EntaPack::Options::signature() const {
    return std::format("v{}|{}|{}:{:.0f}|{}:{:.1f}|{}:{:.0f}|{:.0f}|{:.0f}", kGeneratorVersion, active, lowFire,
                       fireHeight, lowShield, shieldOffset, smallTotem, totemSize, combatParticles, ambientParticles);
}

EntaPack::EntaPack()
    : Module("EntaPack", L"Enta Pack",
             L"Builds a resource pack with low fire, low shield, a smaller totem pop and fewer particles for more FPS. "
             L"Turn on \"Enta Pack\" once in Settings > Global Resources.",
             GAME, nokeybind) {
    auto onChange = [this](Setting&) { markDirty(); };

    addSetting("lowFire", L"Low Fire", L"Makes fire shorter, both on your screen and on the ground.", lowFire)
        ->userUpdateCallback = onChange;
    addSliderSetting("fireHeight", L"Fire Height", L"How tall fire is, in percent of normal.", fireHeight,
                     FloatValue(20.f), FloatValue(90.f), FloatValue(5.f), "lowFire"_istrue)
        ->userUpdateCallback = onChange;

    addSetting("lowShield", L"Low Shield", L"Moves your shield lower in first person so it blocks less of your view.",
               lowShield)
        ->userUpdateCallback = onChange;
    addSliderSetting("shieldOffset", L"Shield Offset", L"How far down to move the shield.", shieldOffset,
                     FloatValue(1.f), FloatValue(8.f), FloatValue(0.5f), "lowShield"_istrue)
        ->userUpdateCallback = onChange;

    addSetting("smallTotem", L"Small Totem Pop",
               L"Makes the totem smaller when it pops. Also makes the totem icon in your inventory smaller.",
               smallTotem)
        ->userUpdateCallback = onChange;
    addSliderSetting("totemSize", L"Totem Size", L"Totem size in percent of normal.", totemSize, FloatValue(30.f),
                     FloatValue(90.f), FloatValue(5.f), "smallTotem"_istrue)
        ->userUpdateCallback = onChange;

    addSliderSetting("combatParticles", L"Combat Particles",
                     L"Percent of totem, crit, explosion and potion particles to keep. 0 turns them off, 100 is "
                     L"normal. Lower means more FPS in fights.",
                     combatParticles, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f))
        ->userUpdateCallback = onChange;
    addSliderSetting("ambientParticles", L"Block & Ambient Particles",
                     L"Percent of block breaking, smoke and rain splash particles to keep. 100 is normal.",
                     ambientParticles, FloatValue(0.f), FloatValue(100.f), FloatValue(5.f))
        ->userUpdateCallback = onChange;

    addActionSetting("rebuild", LocalizedString("enta.pack.rebuild", L"Rebuild Pack"),
                     LocalizedString("enta.pack.rebuild.desc", L"Rebuild the pack now, for example after a game update."),
                     [this] { startBuild(isEnabled(), true); });
    addActionSetting("openFolder", LocalizedString("enta.pack.open", L"Open Pack Folder"),
                     LocalizedString("enta.pack.open.desc", L"Open the folder the pack is saved in."),
                     [] { openPackFolder(); });

    listen<UpdateEvent>(static_cast<EventListenerFunc>(&EntaPack::onUpdate));
}

EntaPack::~EntaPack() {
    if (job.valid()) job.wait();
}

EntaPack::Options EntaPack::snapshot(bool active) {
    Options o;
    o.active = active;
    o.lowFire = std::get<BoolValue>(lowFire);
    o.fireHeight = std::get<FloatValue>(fireHeight);
    o.lowShield = std::get<BoolValue>(lowShield);
    o.shieldOffset = std::get<FloatValue>(shieldOffset);
    o.smallTotem = std::get<BoolValue>(smallTotem);
    o.totemSize = std::get<FloatValue>(totemSize);
    o.combatParticles = std::get<FloatValue>(combatParticles);
    o.ambientParticles = std::get<FloatValue>(ambientParticles);
    return o;
}

void EntaPack::markDirty() {
    lastChange = std::chrono::steady_clock::now();
    dirty = true;
}

void EntaPack::onEnable() {
    startBuild(true, false);
}

void EntaPack::onDisable() {
    // leaves an empty pack behind, so it can stay turned on in Global Resources without changing anything
    startBuild(false, false);
}

void EntaPack::startBuild(bool active, bool force) {
    if (job.valid() && job.wait_for(0s) != std::future_status::ready) {
        // one build at a time; run this one when the current one finishes
        queued = true;
        queuedActive = active;
        if (force) queuedForce = true;
        return;
    }
    if (job.valid()) job.get();
    job = std::async(std::launch::async, &EntaPack::build, snapshot(active), force);
}

void EntaPack::onUpdate(Event&) {
    if (queued && (!job.valid() || job.wait_for(0s) == std::future_status::ready)) {
        queued = false;
        startBuild(queuedActive, queuedForce.exchange(false));
        return;
    }

    // wait until the sliders stop moving before rebuilding
    if (dirty && isEnabled() && std::chrono::steady_clock::now() - lastChange > 1s) {
        dirty = false;
        startBuild(true, false);
    }
}

void EntaPack::openPackFolder() {
    auto dir = findPackDir();
    if (!dir) {
        say("Couldn't find your Minecraft com.mojang folder.");
        return;
    }
    std::error_code ec;
    fs::create_directories(*dir, ec);
    ShellExecuteW(nullptr, L"open", dir->wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void EntaPack::build(Options opts, bool force) {
    auto packDir = findPackDir();
    if (!packDir) {
        say("Enta Pack: couldn't find your Minecraft com.mojang folder.");
        return;
    }

    std::error_code ec;
    const fs::path sigPath = *packDir / kSignatureFile;
    const std::string sig = opts.signature();
    const bool firstTime = !fs::exists(*packDir / "manifest.json", ec);

    if (!force && !firstTime) {
        if (auto old = readFile(sigPath); old && *old == sig) return; // nothing changed
    }

    // start clean: only remove the folders this module writes
    for (auto sub : { "textures", "animations", "particles" }) fs::remove_all(*packDir / sub, ec);

    if (!writeFile(*packDir / "manifest.json", manifest())) {
        say("Enta Pack: couldn't write to " + packDir->string());
        return;
    }

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory2> wic;
    CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    std::vector<std::string> problems;
    int written = 0;

    if (wic) {
        PackImage icon = makeIcon();
        savePng(wic.Get(), *packDir / "pack_icon.png", icon);
    }

    if (opts.active) {
        const auto vanilla = vanillaPackDirs();
        if (vanilla.empty()) problems.push_back("couldn't find the game's vanilla resources");

        auto texture = [&](std::string const& rel, auto&& transform) {
            if (!wic) return;
            auto src = findVanillaFile(vanilla, rel);
            if (!src) {
                problems.push_back("missing " + rel);
                return;
            }
            auto img = loadPng(wic.Get(), *src);
            if (!img) {
                problems.push_back("couldn't read " + rel);
                return;
            }
            PackImage out = transform(*img);
            if (savePng(wic.Get(), *packDir / fs::path(rel), out))
                written++;
            else
                problems.push_back("couldn't save " + rel);
        };

        if (opts.lowFire && opts.fireHeight < 100.f) {
            const float k = opts.fireHeight / 100.f;
            for (auto* rel : kFireTextures) texture(rel, [k](PackImage& img) { return squashFrames(img, k); });
        }

        if (opts.smallTotem && opts.totemSize < 100.f) {
            const float k = opts.totemSize / 100.f;
            texture("textures/items/totem.png", [k](PackImage& img) { return shrinkCentered(img, k); });
        }

        if (opts.lowShield && opts.shieldOffset > 0.f) {
            const std::string rel = "animations/shield.animation.json";
            auto src = findVanillaFile(vanilla, rel);
            auto j = src ? readJson(*src) : std::nullopt;
            if (j && lowerShield(*j, opts.shieldOffset) && writeFile(*packDir / rel, j->dump(2)))
                written++;
            else
                problems.push_back("couldn't make the low shield");
        }

        auto particles = [&](auto const& names, float percent) {
            if (percent >= 100.f) return;
            const float k = percent / 100.f;
            for (auto* name : names) {
                const std::string rel = std::string("particles/") + name + ".json";
                auto src = findVanillaFile(vanilla, rel);
                if (!src) continue; // some particles don't exist in older game versions
                auto j = readJson(*src);
                if (j && scaleParticles(*j, k) && writeFile(*packDir / rel, j->dump(2))) written++;
            }
        };
        particles(kCombatParticles, opts.combatParticles);
        particles(kAmbientParticles, opts.ambientParticles);
    }
    // when the module is off, the pack stays but is empty

    wic.Reset();
    if (SUCCEEDED(coHr)) CoUninitialize();

    writeFile(sigPath, sig);

    if (!opts.active) {
        say("Enta Pack is off, the pack is now empty. Rejoin to see the change.");
        return;
    }

    std::string msg = std::format("Enta Pack updated ({} files).", written);
    if (!problems.empty()) {
        msg += " " + std::string(kSect) + "eSkipped: ";
        for (size_t i = 0; i < problems.size(); i++) msg += (i ? ", " : "") + problems[i];
        msg += std::string(kSect) + "r.";
    }
    say(msg);

    if (firstTime)
        say("First time setup: go to Settings > Global Resources > My Packs and activate " + std::string(kSect) +
            "dEnta Pack" + kSect + "r. After that, changes apply when you rejoin a world or server.");
    else
        say("Rejoin your world or server to see the changes.");
}
