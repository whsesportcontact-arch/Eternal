#include "pch.h"
#include "ShulkerPreview.h"
#include "mc/common/world/ItemStack.h"
#include "mc/common/util/BasicDataOutput.h"
#include <cctype>

namespace {
    // ---------- recording a tag's serialized form ----------
    struct NbtTok {
        enum Kind : uint8_t { Num, Real, Str, Bytes } kind = Num;
        int64_t i = 0;
        std::string s;
    };

    std::string readStringSpan(void* span) {
        if (!span) return {};
        auto w = reinterpret_cast<uintptr_t*>(span);
        uintptr_t a = w[0], b = w[1];
        const char* data;
        size_t size;
        // std::string_view is {data, size}; older gsl spans were {size, data}. Accept either.
        if (a < 0x10000 && b > 0x10000) {
            size = a;
            data = reinterpret_cast<const char*>(b);
        } else {
            data = reinterpret_cast<const char*>(a);
            size = b;
        }
        if (!data || size > 0x8000) return {};
        return std::string(data, size);
    }

    class NbtRecorder : public SDK::BasicDataOutput {
    public:
        std::vector<NbtTok> toks;

        void writeString(void* span) override { toks.push_back({ NbtTok::Str, 0, readStringSpan(span) }); }
        void writeLongString(void* span) override { toks.push_back({ NbtTok::Str, 0, readStringSpan(span) }); }
        void writeFloat(float) override { toks.push_back({ NbtTok::Real }); }
        void writeDouble(long double) override { toks.push_back({ NbtTok::Real }); }
        void writeByte(char c) override { toks.push_back({ NbtTok::Num, static_cast<unsigned char>(c) }); }
        void writeShort(int16_t v) override { toks.push_back({ NbtTok::Num, v }); }
        void writeInt(int v) override { toks.push_back({ NbtTok::Num, v }); }
        void writeLongLong(int64_t v) override { toks.push_back({ NbtTok::Num, v }); }
        void writeBytes(const void*, uint64_t) override { toks.push_back({ NbtTok::Bytes }); }
    };

    // SEH guard: if this game version's serializer doesn't match, fail quietly instead of crashing.
    bool writeTagSafe(SDK::Tag* tag, SDK::BasicDataOutput* out) {
        __try {
            tag->write(*out);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ---------- parsing the recording back into a tree ----------
    struct Node {
        int type = 0;
        std::string name;
        int64_t i = 0;
        std::string s;
        std::vector<Node> kids;
    };

    struct Parser {
        std::vector<NbtTok> const& t;
        size_t pos = 0;
        bool ok = true;

        NbtTok const* next() {
            if (pos >= t.size()) {
                ok = false;
                return nullptr;
            }
            return &t[pos++];
        }
        int64_t num() {
            auto x = next();
            if (!x || x->kind != NbtTok::Num) {
                ok = false;
                return 0;
            }
            return x->i;
        }
        std::string str() {
            auto x = next();
            if (!x || x->kind != NbtTok::Str) {
                ok = false;
                return {};
            }
            return x->s;
        }
        void skipArray(int64_t len) {
            if (pos < t.size() && t[pos].kind == NbtTok::Bytes) {
                pos++;
                return;
            }
            if (len < 0 || len > 1 << 20) {
                ok = false;
                return;
            }
            for (int64_t k = 0; k < len && ok; k++) next();
        }

        void payload(Node& n, int depth) {
            if (depth > 32) {
                ok = false;
                return;
            }
            switch (n.type) {
            case 1: case 2: case 3: case 4: n.i = num(); break;  // byte short int long
            case 5: case 6: next(); break;                     // float double
            case 7: case 11: case 12: skipArray(num()); break; // byte/int/long arrays
            case 8: n.s = str(); break;
            case 9: {
                int elemType = static_cast<int>(num());
                int64_t count = num();
                if (!ok || count < 0 || count > 100000 || elemType < 0 || elemType > 12) {
                    ok = false;
                    return;
                }
                for (int64_t k = 0; k < count && ok; k++) {
                    Node c;
                    c.type = elemType;
                    payload(c, depth + 1);
                    n.kids.push_back(std::move(c));
                }
                break;
            }
            case 10: compound(n, depth + 1); break;
            default: ok = false;
            }
        }

        void compound(Node& n, int depth) {
            while (ok) {
                int type = static_cast<int>(num());
                if (!ok || type == 0) return;
                if (type < 1 || type > 12) {
                    ok = false;
                    return;
                }
                Node c;
                c.type = type;
                c.name = str();
                payload(c, depth);
                n.kids.push_back(std::move(c));
            }
        }
    };

    Node const* findItemsList(Node const& n, int depth = 0) {
        if (depth > 8) return nullptr;
        for (auto& k : n.kids) {
            if (k.type == 9 && k.name == "Items") return &k;
            if (k.type == 10)
                if (auto r = findItemsList(k, depth + 1)) return r;
        }
        return nullptr;
    }

    std::wstring prettyName(std::string id) {
        if (auto c = id.find(':'); c != std::string::npos) id = id.substr(c + 1);
        std::wstring out;
        bool upper = true;
        for (char ch : id) {
            if (ch == '_') {
                out += L' ';
                upper = true;
                continue;
            }
            out += upper ? static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(ch)))
                         : static_cast<wchar_t>(ch);
            upper = false;
        }
        return out;
    }
}

ShulkerPreview::ShulkerPreview()
    : TextModule("ShulkerPreview", L"Shulker Preview", L"Hold a shulker box and press Shift to see what's inside.",
                 HUD, 600.f) {
    addSetting("requireShift", L"Require Shift", L"Only show while Shift is held. Off = always show when holding one.",
               requireShift);
    addSetting("groupItems", L"Group Items", L"Add up stacks of the same item (e.g. 'Diamond x192').", groupItems);
    addSliderSetting("maxLines", L"Max Lines", L"How many items to list before '+ N more'.", maxLines,
                     FloatValue(3.f), FloatValue(27.f), FloatValue(1.f));
    this->textColor = ColorValue(0.92f, 0.88f, 1.f, 1.f);
    this->outlineColor = ColorValue(0.55f, 0.24f, 1.f, 1.f);
    this->showOutline = BoolValue(true);
    this->radius = FloatValue(3.f);
}

std::wstring ShulkerPreview::buildText(SDK::ItemStack* stack) {
    if (!stack->tag) return L"Shulker Box\nEmpty";

    NbtRecorder rec;
    if (!writeTagSafe(reinterpret_cast<SDK::Tag*>(stack->tag.get()), &rec)) return L"Shulker Box\n(couldn't read)";

    Node root;
    root.type = 10;
    Parser p { rec.toks };
    p.compound(root, 0);
    if (!p.ok && rec.toks.size() > 2 && rec.toks[0].kind == NbtTok::Num && rec.toks[0].i == 10 &&
        rec.toks[1].kind == NbtTok::Str) {
        // some versions write a root header (type 10 + empty name) first
        root = Node {};
        root.type = 10;
        Parser p2 { rec.toks, 2 };
        p2.compound(root, 0);
    }

    auto items = findItemsList(root);
    if (!items || items->kids.empty()) return L"Shulker Box\nEmpty";

    std::vector<std::pair<std::wstring, int64_t>> lines;
    std::map<std::wstring, int64_t> grouped;
    int stacks = 0;
    for (auto& it : items->kids) {
        std::string id;
        int64_t count = 0;
        for (auto& f : it.kids) {
            if (f.name == "Name" && f.type == 8) id = f.s;
            if (f.name == "Count") count = f.i;
        }
        if (id.empty() || id == "minecraft:air" || count <= 0) continue;
        stacks++;
        if (std::get<BoolValue>(groupItems))
            grouped[prettyName(id)] += count;
        else
            lines.push_back({ prettyName(id), count });
    }
    if (std::get<BoolValue>(groupItems)) {
        for (auto& [n, c] : grouped) lines.push_back({ n, c });
        std::sort(lines.begin(), lines.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
    }

    std::wstringstream wss;
    wss << L"Shulker Box  (" << stacks << L"/27)";
    size_t limit = static_cast<size_t>(std::get<FloatValue>(maxLines));
    for (size_t k = 0; k < lines.size() && k < limit; k++) {
        wss << L"\n" << lines[k].first << L"  x" << lines[k].second;
    }
    if (lines.size() > limit) wss << L"\n+ " << (lines.size() - limit) << L" more";
    if (lines.empty()) wss << L"\nEmpty";
    return wss.str();
}

std::wstringstream ShulkerPreview::text(bool isDefault, bool inEditor) {
    if (isDefault || inEditor) {
        return std::wstringstream(L"Shulker Box  (5/27)\nDiamond  x192\nIron Ingot  x64\nRedstone  x32");
    }

    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    if (!lp || !lp->supplies || !lp->supplies->inventory) return {};

    auto stack = lp->supplies->inventory->getItem(lp->supplies->selectedSlot);
    auto item = stack ? stack->getItem() : nullptr;
    if (!item) return {};

    std::string id = item->namespacedId.getString();
    if (id.find("shulker_box") == std::string::npos) return {};

    if (std::get<BoolValue>(requireShift) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) return {};

    // re-read when the held stack changes, and every ~half second otherwise
    if (stack != lastStack || stack->tag.get() != lastTag || ++framesSinceRead > 30) {
        lastStack = stack;
        lastTag = stack->tag.get();
        framesSinceRead = 0;
        cachedText = buildText(stack);
    }
    return std::wstringstream(cachedText);
}
