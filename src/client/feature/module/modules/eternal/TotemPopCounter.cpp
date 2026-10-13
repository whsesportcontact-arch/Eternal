#include "pch.h"
#include "TotemPopCounter.h"
#include <cctype>
#include "client/Latite.h"
#include "client/misc/ClientMessageQueue.h"
#include "mc/common/network/packet/ActorEventPacket.h"
#include "mc/common/world/actor/player/Player.h"
#include "client/event/events/RenderLayerEvent.h"
#include "mc/common/client/game/FontRepository.h"
#include "mc/common/client/gui/controls/UIControl.h"
#include "mc/common/client/gui/controls/VisualTree.h"
#include "util/DrawContext.h"
#include "util/WorldToScreen.h"

namespace {
    // UTF-8 section sign, used for Minecraft color codes
    constexpr const char* kSect = "\xC2\xA7";
}

TotemPopCounter::TotemPopCounter()
    : Module("TotemPopCounter", L"Totem Pop Counter",
             L"Shows how many totems each player has popped next to their nametag, like -1, -2, -3.", GAME,
             nokeybind) {
    style.addEntry(EnumEntry(0, L"-1, -2, -3", L"Counts down for every totem popped."));
    style.addEntry(EnumEntry(1, L"+1, +2, +3", L"Counts up for every totem popped."));
    style.addEntry(EnumEntry(2, L"2 Pops", L"Shows the number of pops as words."));

    color.addEntry(EnumEntry(0, L"Red"));
    color.addEntry(EnumEntry(1, L"Gold"));
    color.addEntry(EnumEntry(2, L"Purple"));
    color.addEntry(EnumEntry(3, L"Yellow"));
    color.addEntry(EnumEntry(4, L"Aqua"));
    color.addEntry(EnumEntry(5, L"White"));

    addEnumSetting("style", L"Counter Style", L"How the pop count looks next to the name.", style);
    addEnumSetting("color", L"Counter Color", L"Color of the pop count.", color);
    addSetting("aboveHead", L"Show Above Head", L"Floats the count above each player's head.", aboveHead);
    addSetting("onNametag", L"Add To Nametag", L"Also adds the count to the end of the player's nametag.", onNametag);
    addSetting("popChatAlerts", L"Chat Alerts", L"Also say in chat when someone pops a totem.", chatAlerts);
    addSetting("countSelf", L"Count Your Own Pops", L"Also tell you in chat when you pop a totem.", countSelf,
               "popChatAlerts"_istrue);
    addSetting("resetOnDeath", L"Reset On Death", L"Reset a player's count when they die.", resetOnDeath);

    listen<PacketReceiveEvent>(static_cast<EventListenerFunc>(&TotemPopCounter::onPacketReceive));
    listen<RenderNameTagEvent>(static_cast<EventListenerFunc>(&TotemPopCounter::onRenderNameTag));
    listen<TickEvent>(static_cast<EventListenerFunc>(&TotemPopCounter::onTick));
    listen<LeaveGameEvent>(static_cast<EventListenerFunc>(&TotemPopCounter::onLeaveGame));
    listen<RenderLayerEvent>(static_cast<EventListenerFunc>(&TotemPopCounter::onRenderLayer));
}

std::string TotemPopCounter::stripFormatting(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        // skip "§x" (the section sign is 2 bytes in UTF-8, followed by the code character)
        if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 &&
            static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out += s[i];
    }
    return out;
}

// True when `name` appears in `haystack` as a whole word, so "Bob" doesn't match "Bobby".
bool TotemPopCounter::containsName(std::string const& haystack, std::string const& name) {
    if (name.empty()) return false;
    auto isNameChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    for (size_t pos = haystack.find(name); pos != std::string::npos; pos = haystack.find(name, pos + 1)) {
        bool startOk = pos == 0 || !isNameChar(haystack[pos - 1]);
        size_t end = pos + name.size();
        bool endOk = end >= haystack.size() || !isNameChar(haystack[end]);
        if (startOk && endOk) return true;
    }
    return false;
}

std::string TotemPopCounter::colorCode() const {
    static constexpr char codes[] = { 'c', '6', 'd', 'e', 'b', 'f' };
    int idx = color.getSelectedKey();
    if (idx < 0 || idx >= static_cast<int>(sizeof(codes))) idx = 0;
    return std::string(kSect) + codes[idx];
}

d2d::Color TotemPopCounter::drawColor() const {
    switch (color.getSelectedKey()) {
    case 1: return { 1.f, 0.67f, 0.f };     // gold
    case 2: return { 0.8f, 0.45f, 1.f };    // purple
    case 3: return { 1.f, 1.f, 0.33f };     // yellow
    case 4: return { 0.33f, 1.f, 1.f };     // aqua
    case 5: return { 1.f, 1.f, 1.f };       // white
    default: return { 1.f, 0.33f, 0.33f }; // red
    }
}

std::string TotemPopCounter::formatCount(int pops) const {
    switch (style.getSelectedKey()) {
    case 1:
        return "+" + std::to_string(pops);
    case 2:
        return std::to_string(pops) + (pops == 1 ? " Pop" : " Pops");
    default:
        return "-" + std::to_string(pops);
    }
}

void TotemPopCounter::onPacketReceive(Event& evG) {
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
    auto* pkt = ev.getPacket();
    if (!pkt || pkt->getID() != SDK::PacketID::ACTOR_EVENT) return;

    auto* actorEvent = static_cast<SDK::ActorEventPacket*>(pkt);
    const bool popped = actorEvent->eventID == SDK::ActorEventID::TALISMAN_ACTIVATE;
    const bool died = actorEvent->eventID == SDK::ActorEventID::DEATH_ANIMATION;
    if (!popped && !died) return;

    const uint64_t id = actorEvent->runtimeID;

    if (died) {
        if (std::get<BoolValue>(resetOnDeath)) {
            std::lock_guard guard(lock);
            entries.erase(id);
        }
        return;
    }

    auto* ci = SDK::ClientInstance::get();
    if (!ci) return;

    // Find who popped. Only players get counted; mobs holding totems are ignored.
    SDK::Actor* actor = nullptr;
    if (ci->minecraft) {
        if (auto* level = ci->minecraft->getLevel()) {
            for (auto* a : level->getRuntimeActorList()) {
                if (a && a->getRuntimeID() == id) {
                    actor = a;
                    break;
                }
            }
        }
    }
    if (actor && !actor->isPlayer()) return;

    auto* local = ci->getLocalPlayer();
    const bool isSelf = local && local->getRuntimeID() == id;

    int pops;
    std::string name;
    {
        std::lock_guard guard(lock);
        auto& entry = entries[id];
        if (actor) {
            entry.playerName = static_cast<SDK::Player*>(actor)->playerName;
            entry.nameTag = actor->getNameTag();
        }
        pops = ++entry.pops;
        name = entry.playerName;
    }

    if (!std::get<BoolValue>(chatAlerts)) return;
    if (isSelf && !std::get<BoolValue>(countSelf)) return;
    if (!isSelf && name.empty()) return;

    std::string who = isSelf ? std::string("You") : stripFormatting(name);
    Latite::getClientMessageQueue().push(std::string(kSect) + "d[Enta]" + kSect + "r " + who +
                                         " popped a totem! " + colorCode() +
                                         "(" + formatCount(pops) + ")");
}

void TotemPopCounter::onRenderNameTag(Event& evG) {
    if (!std::get<BoolValue>(onNametag)) return;
    auto& ev = reinterpret_cast<RenderNameTagEvent&>(evG);
    std::string* tag = ev.getNametag();
    if (!tag || tag->empty()) return;

    const Entry* match = nullptr;
    std::string suffix;
    {
        std::lock_guard guard(lock);
        if (entries.empty()) return;

        // 1) exact nametag match (most reliable)
        for (auto& [id, entry] : entries) {
            if (entry.pops > 0 && !entry.nameTag.empty() && entry.nameTag == *tag) {
                match = &entry;
                break;
            }
        }

        // 2) the player's name shows up somewhere in the tag (ranks, team prefixes, Nickname module, etc.)
        if (!match) {
            std::string plain = stripFormatting(*tag);
            size_t bestLen = 0;
            for (auto& [id, entry] : entries) {
                if (entry.pops <= 0) continue;
                std::string name = stripFormatting(entry.playerName);
                if (name.size() > bestLen && containsName(plain, name)) {
                    match = &entry;
                    bestLen = name.size();
                }
            }
        }

        if (!match) return;
        suffix = " " + colorCode() + formatCount(match->pops) + kSect + "r";
    }

    // Put the counter at the end of the first line, so it sits right next to the name on multi-line tags.
    size_t newline = tag->find('\n');
    if (newline == std::string::npos)
        tag->append(suffix);
    else
        tag->insert(newline, suffix);
}

void TotemPopCounter::refreshNames(SDK::Level* level) {
    if (!level) return;
    std::lock_guard guard(lock);
    if (entries.empty()) return;

    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || !actor->isPlayer()) continue;
        auto it = entries.find(actor->getRuntimeID());
        if (it == entries.end()) continue;
        it->second.playerName = static_cast<SDK::Player*>(actor)->playerName;
        it->second.nameTag = actor->getNameTag();
    }
}

void TotemPopCounter::onTick(Event& evG) {
    auto& ev = reinterpret_cast<TickEvent&>(evG);
    // nametags can change (rank updates, team colors), so re-read them about once a second
    if (++tickCounter % 20 != 0) return;
    refreshNames(ev.getLevel());
}

// Draw the count floating above each player's head (on the HUD layer, so it's always readable).
void TotemPopCounter::onRenderLayer(Event& evG) {
    if (!std::get<BoolValue>(aboveHead)) return;
    auto& ev = reinterpret_cast<RenderLayerEvent&>(evG);
    auto* view = ev.getScreenView();
    if (!view || !view->visualTree || !view->visualTree->rootControl ||
        view->visualTree->rootControl->name != "hud_screen")
        return;

    auto* ci = SDK::ClientInstance::get();
    auto* local = ci ? ci->getLocalPlayer() : nullptr;
    auto* level = (ci && ci->minecraft) ? ci->minecraft->getLevel() : nullptr;
    if (!local || !level) return;

    std::unordered_map<uint64_t, int> pops;
    {
        std::lock_guard guard(lock);
        for (auto& [id, entry] : entries)
            if (entry.pops > 0) pops[id] = entry.pops;
    }
    if (pops.empty()) return;

    MCDrawUtil dc { ev.getUIRenderContext(), ci->minecraftGame->getFontRepository()->getMinecraftFont() };
    const Vec3 me = local->getPos();
    const d2d::Color col = drawColor();
    bool drew = false;

    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || !actor->isPlayer() || actor == local) continue;
        auto it = pops.find(actor->getRuntimeID());
        if (it == pops.end()) continue;

        // just above the nametag
        AABB box = actor->getBoundingBox();
        const Vec3 head((box.lower.x + box.higher.x) * 0.5f, box.higher.y + 0.95f, (box.lower.z + box.higher.z) * 0.5f);
        auto screen = WorldToScreen::convert(head);
        if (!screen) continue;

        const float dx = head.x - me.x, dy = head.y - me.y, dz = head.z - me.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float size = std::clamp(320.f / std::max(dist, 1.f), 14.f, 36.f); // smaller when far away

        const std::wstring text = util::StrToWStr(formatCount(it->second));
        d2d::Rect rect(screen->x - 150.f, screen->y - size, screen->x + 150.f, screen->y);
        dc.drawText(rect, text, col, Renderer::FontSelection::PrimaryRegular, size, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        drew = true;
    }
    if (drew) dc.flush();
}

void TotemPopCounter::onLeaveGame(Event&) {
    std::lock_guard guard(lock);
    entries.clear();
}

void TotemPopCounter::onDisable() {
    std::lock_guard guard(lock);
    entries.clear();
}
