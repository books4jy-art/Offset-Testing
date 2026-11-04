// Features tab: ready-made controls (toggles, sliders, buttons) built on the game's own events and a few of its
// variables, laid out like TheVoid's menu. Other offsets tab: TheVoid's address table, ported to 0.35.2.
//
// How a feature runs: it starts one of the game's events from Lua and keeps it in the Lua table OffsetLab[key], so it
// can be stopped again. Toggles start the event when switched on and stop it when switched off; sliders start it again
// with the new value when you let go; every slider has a reset button that puts the game's normal behaviour back.
#include "lab.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "canvas_api.h"
#include "imgui.h"
#include "json.h"

namespace lab {

namespace {

const ImVec4 kSky300(0.490f, 0.827f, 0.988f, 1.f);
const ImVec4 kRed300(0.988f, 0.647f, 0.647f, 1.f);
const ImVec4 kAmber300(0.988f, 0.827f, 0.302f, 1.f);

void Dim(const char* fmt, ...) IM_FMTARGS(1);
void Dim(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.70f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

std::string Num(float v) {
    char b[32];
    snprintf(b, sizeof b, "%.4f", v);
    return b;
}

// ---------------------------------------------------------------- game types by name

std::map<std::string, size_t> gTypeIndex;

void IndexTypes() {
    if (!gTypeIndex.empty() || !GameBase()) return;
    size_t n = 0;
    const LabType* t = Types(&n);
    for (size_t i = 0; i < n; i++) {
        std::string s = GameString(t[i].nameAddr);
        if (!s.empty() && Fnv1a(s) == t[i].nameHash) gTypeIndex[s] = i;
    }
}

// Option names of one enum setting, read from the game (empty if not found).
std::vector<std::string> EnumOptions(const char* type, const char* setting) {
    std::vector<std::string> out;
    IndexTypes();
    auto it = gTypeIndex.find(type);
    if (it == gTypeIndex.end()) return out;
    size_t n = 0;
    const LabType& t = Types(&n)[it->second];
    for (uint32_t i = 0; i < t.settingCount; i++) {
        const LabSetting& s = Setting(t.settingFirst + i);
        if (s.kind != kEnum || s.enumIndex == 0xFFFF || GameString(s.nameAddr) != setting) continue;
        const LabEnum& e = Enum(s.enumIndex);
        for (uint32_t k = 0; k < e.count; k++) out.push_back(GameString(EnumOptionAddr(e.first + k)));
        break;
    }
    return out;
}

// False when the game says this type isn't an event (firing it would abort the game).
bool SafeToFire(const char* type, bool allowUnlisted = false) {
    IndexTypes();
    auto it = gTypeIndex.find(type);
    if (it == gTypeIndex.end()) {
        if (allowUnlisted) return true;  // registered in a way the generator doesn't see; the game returns nil if absent
        Log("Feature: %s isn't in this game version", type);
        return false;
    }
    if (EventStatusOf(it->second) == kNotEvent) {
        Log("Feature: %s isn't an event; not fired", type);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- Lua

struct Set {
    std::string name, value;  // value as Lua code
    std::string fallback;     // tried when value isn't accepted (enums: the option's number)
};

// Saved events are dropped when the level changes: the game deletes them, and using a deleted event's handle
// crashes the game ("Invalid meta class id").
const char* kPrelude =
    "OffsetLab = OffsetLab or {}\n"
    "do local ok, lvl = pcall(function() return game:levelName() end)\n"
    "  if ok and OffsetLab._level ~= lvl then OffsetLab = {_level = lvl} end end\n"
    "local function stop(k) local e = OffsetLab[k]; OffsetLab[k] = nil\n"
    "  if e then pcall(function() e:SetEnabled(false) end) end end\n";
// Events have Start, SetEnabled and GetEnabled in the game's Lua (the EventBarn binding); there is no Stop, so a
// feature is switched off with SetEnabled(false).

bool Run(const std::string& what, const std::string& script, const std::string& key = "") {
    // what == "" for repeats (Scatter, repeated Shout): only problems are logged
    int r = RunLua(script, what.empty() ? "Repeat" : what, key);
    const char* w = what.empty() ? "Repeat" : what.c_str();
    if (r == -1) Log("%s: enter the world first", w);
    else if (r == -3) Log("%s: the game isn't running frames right now", w);
    else if (r != 0) Log("%s: the game reported a script error (%d)", w, r);
    else if (!what.empty()) Log("%s", w);
    return r == 0;
}

// Starts event `type` with the settings, keeping it as OffsetLab[key] (an earlier one under the key is stopped).
std::string Quote(const std::string& s);

std::vector<float> NumbersIn(const std::string& t) {
    std::vector<float> v;
    const char* p = t.c_str();
    while (*p) {
        if (isdigit((unsigned char)*p) || ((*p == '-' || *p == '.') && (isdigit((unsigned char)p[1]) || p[1] == '.'))) {
            char* end = nullptr;
            v.push_back(strtof(p, &end));
            p = end;
        } else {
            p++;
        }
    }
    return v;
}

// Every setting of the event at the game's own default (objects such as npc: nil). Events made from Lua don't get
// these: AvatarDisableShadow read its npc setting while it still held uninitialised memory and crashed the game.
std::vector<Set> GameDefaults(const char* type) {
    std::vector<Set> out;
    auto it = gTypeIndex.find(type);
    if (it == gTypeIndex.end()) {
        out.push_back({"npc", "nil", ""});  // not in the table (AvatarPlayEmote): at least no stray npc
        return out;
    }
    size_t n = 0;
    const LabType& t = Types(&n)[it->second];
    for (uint32_t i = 0; i < t.settingCount; i++) {
        const LabSetting& st = Setting(t.settingFirst + i);
        std::string name = GameString(st.nameAddr);
        if (name.empty() || !(isalpha((unsigned char)name[0]))) continue;
        bool plain = true;
        for (char c : name) plain &= isalnum((unsigned char)c) || c == '_';
        if (!plain) continue;
        std::string def = st.defaultAddr ? GameString(st.defaultAddr) : std::string();
        if (def.size() >= 2 && def.front() == '"' && def.back() == '"') def = def.substr(1, def.size() - 2);
        std::vector<float> nums = NumbersIn(def);
        char b[160];
        switch (st.kind) {
            case kBool: out.push_back({name, def.find("true") != std::string::npos ? "true" : "false", ""}); break;
            case kFloat:
                snprintf(b, sizeof b, "%g", nums.empty() ? (st.hasRange == 1 ? st.minValue : 0.f) : nums[0]);
                out.push_back({name, b, ""});
                break;
            case kInt:
                snprintf(b, sizeof b, "%d", nums.empty() ? 0 : (int)nums[0]);
                out.push_back({name, b, ""});
                break;
            case kText:
                out.push_back({name, Quote(def.size() < 64 && def.find(' ') == std::string::npos ? def : ""), ""});
                break;
            case kVec2: case kVec3: case kVec4: case kColor3: {
                int k = st.kind == kVec2 ? 2 : st.kind == kVec4 ? 4 : 3;
                std::string v = "{";
                for (int j = 0; j < k; j++) {
                    snprintf(b, sizeof b, "%s%g", j ? ", " : "", j < (int)nums.size() ? nums[j] : (st.kind == kColor3 ? 1.f : 0.f));
                    v += b;
                }
                out.push_back({name, v + "}", ""});
                break;
            }
            case kEnum: {
                if (st.enumIndex == 0xFFFF) break;
                const LabEnum& e = Enum(st.enumIndex);
                // "State_1".."State_8" are lists of other events/objects (OutfitSelect's onClosetOpen...), not
                // choices: give them nil like any object, never text or a number
                if (e.count && GameString(EnumOptionAddr(e.first)) == "State_1") {
                    out.push_back({name, "nil", ""});
                    break;
                }
                int pick = 0;
                for (uint32_t j = 0; j < e.count; j++)
                    if (!def.empty() && GameString(EnumOptionAddr(e.first + j)) == def) pick = (int)j;
                if (e.count) out.push_back({name, Quote(GameString(EnumOptionAddr(e.first + pick))), std::to_string(pick)});
                break;
            }
            case kObject: out.push_back({name, "nil", ""}); break;
            default:
                if (st.objTypeAddr || name == "npc" || name == "npcToMimic") out.push_back({name, "nil", ""});
        }
    }
    return out;
}

// Events that do their thing and finish: the game deletes them by itself, so they're never kept or switched off
// (switching off a deleted one crashed the game: a second Cat curse, a second outfit, Sunglasses off).
bool OneShot(const char* type) {
    static const char* const kOnce[] = {
        "GrantSpell", "RemoveSpell", "AvatarSetOutfit", "AvatarDoShout", "AvatarBreakHandHold", "SetAvatarMinYSpeed",
        "CameraPhotoFlash", "StopAvatarAnimSeq", "AvatarSetStance", "AvatarPauseAnim", "KillLocalAvatar",
        "SetAvatarGravityMult", "SetAvatarRunSpeedMult", "AvatarCrawl", "PlayAvatarAnimSeq", "AvatarPlayEmote",
        "SetCameraShake", "CollectWingBuffServerRequestEvent", "DepositWingBuffServerRequestEvent",
        "ConvertWingBuffsServerRequestEvent", "DropWingBuffServerRequestEvent",
        // 0.11.0: these stop themselves in their first update (their update calls the game's event stop, 0x171F82C)
        "AvatarSetOutfitDye", "AvatarSpeak", "AvatarChargeEnergy", "AvatarSetInnerGlow", "LocalDoAudienceReact"};
    for (const char* t : kOnce)
        if (!strcmp(t, type)) return true;
    return false;
}

// The Lua that starts event `type` (to go after kPrelude), keeping it as OffsetLab[key] unless it's a one-shot.
// "" when the event mustn't be fired.
std::string EventLua(const std::string& keyIn, const char* type, const std::vector<Set>& given,
                     bool allowUnlisted = false) {
    if (!SafeToFire(type, allowUnlisted)) return std::string();
    const std::string key = OneShot(type) ? std::string() : keyIn;
    std::vector<Set> sets = GameDefaults(type);  // first the game's defaults, then ours
    sets.insert(sets.end(), given.begin(), given.end());
    std::string s = "do\n";
    if (!key.empty()) s += "stop(\"" + key + "\")\n";
    s += "local e = game:eventBarn():AddEventByMetaName(\"" + std::string(type) + "\")\n";
    s += "if not e then error(\"" + std::string(type) + " not found\") end\n";
    s += "local bad = {}\n";
    for (const auto& x : sets) {
        s += "if not pcall(function() e:" + x.name + "(" + x.value + ") end) then ";
        if (!x.fallback.empty())
            s += "if not pcall(function() e:" + x.name + "(" + x.fallback + ") end) then bad[#bad+1] = \"" + x.name +
                 "\" end ";
        else
            s += "bad[#bad+1] = \"" + x.name + "\" ";
        s += "end\n";
    }
    s += "e:Start()\n";
    if (!key.empty()) s += "OffsetLab[\"" + key + "\"] = e\n";
    s += "if #bad > 0 and print then pcall(print, \"Offset Lab: " + std::string(type) +
         " didn't take \" .. table.concat(bad, \", \")) end\n";
    return s + "end\n";
}

bool StartEvent(const std::string& what, const std::string& keyIn, const char* type, const std::vector<Set>& given,
                bool allowUnlisted = false) {
    std::string lua = EventLua(keyIn, type, given, allowUnlisted);
    if (lua.empty()) return false;
    // coalescing key for the queue: the feature's key (also for one-shots, e.g. one per outfit slot), else the type
    const std::string queueKey = keyIn.empty() ? std::string("once:") + type : keyIn;
    return Run(what, kPrelude + lua, queueKey);
}

bool StopEvent(const std::string& what, const std::string& key) {
    return Run(what, std::string(kPrelude) + "stop(\"" + key + "\")\n", key);
}

std::string Quote(const std::string& s) {  // a Lua string
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c >= 32) o += c;
    }
    return o + "\"";
}

// ---------------------------------------------------------------- controls

// A toggle that runs an event while on.
bool Toggle(const char* label, bool* on, const char* help) {
    bool changed = ImGui::Checkbox(label, on);
    if (help && *help) {
        ImGui::Indent(ImGui::GetFrameHeight() + 8);
        Dim("%s", help);
        ImGui::Unindent(ImGui::GetFrameHeight() + 8);
    }
    return changed;
}

// Slider + reset button. Returns 1 when let go after a change, 2 when reset.
int Slider(const char* label, float* v, float lo, float hi, float normal, const char* fmt) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
    ImGui::SliderFloat("##s", v, lo, hi, fmt);
    int r = ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(Px(130), 0))) {
        *v = normal;
        r = 2;
    }
    ImGui::PopID();
    return r;
}

int SliderInt(const char* label, int* v, int lo, int hi, int normal) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
    ImGui::SliderInt("##s", v, lo, hi);
    int r = ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(Px(130), 0))) {
        *v = normal;
        r = 2;
    }
    ImGui::PopID();
    return r;
}

void Section(const char* title, const char* what) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    if (what && *what) Dim("%s", what);
}

// ---------------------------------------------------------------- the features

// Spell ids for "Transform avatar" (the game's own ids; labels are made from them). Forms first, then other spells.
struct Spell { const char* id; bool form; };
const Spell kSpells[] = {
    {"rabbit_form", true}, {"rabbit_form_pin", true}, {"oreo_form", true}, {"chibi_form", true},
    {"cat_curse", true}, {"crab_curse", true}, {"crab_curse_starpin", true}, {"dark_creature_curse", true},
    {"dragon_dance", true}, {"diving", true}, {"triggered_tiny", true}, {"tiny", true}, {"shrink", true},
    {"grow", true}, {"huge", true}, {"resize_potion", true}, {"potion_growth_tiny", true},
    {"potion_growth_huge", true}, {"potion_growth_keychain", true}, {"height_small_3x_handhold", true},
    {"glow", false}, {"glow_rainbow", false}, {"glow_tgc", false}, {"hairtint", false},
    {"hairtint_rainbow", false}, {"trail", false}, {"trail_bicolor", false}, {"trail_rainbow", false},
    {"ground_trail_rainbow", false}, {"ground_trail_star", false}, {"bloom_rose_trail", false},
    {"bloom_sakura", false}, {"butterfly_pastel", false}, {"rainbow", false}, {"rainbow_handhold", false},
    {"gravity", false}, {"energy", false}, {"energy_potion", false}, {"potion_recharge_thermo", false},
    {"repellant_krill", false}, {"star_protector", false}, {"candle_trick", false}, {"crab_rock_trick", false},
    {"currency_trick", false}, {"dnd", false}, {"color_collector", false}, {"birthdayhat_krill", false},
    {"shout_crab", false}, {"shout_jelly", false}, {"shout_manta", false}, {"shout_aurora_keychain", false},
    {"shout_manatee_keychain", false}, {"shout_kizunaaicn", false}, {"fireworks", false}, {"firework_crab", false},
    {"firework_jelly", false}, {"firework_manta", false}, {"dark_dragon_firework", false},
    {"outfit_wing_none", false}, {"outfit_face", false}, {"outfit_hat_ballooncrab", false},
    {"outfit_hat_ballooncrown", false}, {"outfit_hat_balloonjellyfish", false}, {"outfit_hat_balloonkrill", false},
    {"outfit_hat_balloonmanta", false}, {"outfit_horn_ballooncrab", false}, {"outfit_horn_ballooncrown", false},
    {"outfit_horn_balloonjellyfish", false}, {"outfit_horn_balloonkrill", false},
    {"outfit_horn_balloonmanta", false}, {"outfit_prop_birthdaycake_giant", false},
    {"outfit_prop_bluebird_swing", false}, {"outfit_prop_cannon_pin", false}, {"timeline_aurora_concert", false},
    {"timeline_birthdaycrab", false}, {"timeline_daynight", false}, {"timeline_fortune_fireworks", false},
    {"timeline_nature_turtle", false}, {"timeline_newyear_fireworks", false}, {"timeline_oreo", false},
};

std::string SpellLabel(const char* id) {
    std::string s = id;
    for (auto& c : s) if (c == '_') c = ' ';
    if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
    return s;
}

// The patch for Surface Friction: the game's "super slidey" switch (a byte) and the two instructions that load the
// slippery value (0.2) it uses, at 0x249E524 / 0x249E528 (0.35.2).
constexpr uint32_t kSlideySwitch = 0x34AFCEC;
constexpr uint32_t kSlideyValueAt = 0x249E524;
constexpr uint32_t kSlideyOriginal[2] = {0x529999A8, 0x72A7C988};  // mov w8,#0xcccd; movk w8,#0x3e4c,lsl #16
// The game's "disable terrain" switch (a byte); TheVoid's world.hide_terrain, 0x3169CDC in 0.35.1.
constexpr uint32_t kTerrainSwitch = 0x3169CEC;

struct Features {
    // transform
    float spellSeconds = 60.f;
    char spellFilter[64] = "";
    char customSpell[64] = "";
    bool showAllSpells = false;
    // movement
    float hover = 0.f;
    bool antigravity = false;
    float speed = 1.f;
    bool scatter = false;
    double scatterNext = 0;
    bool crawl = false;
    bool kill = false;
    bool flight = false;
    bool walkUnderWater = false;
    // wings
    int capeLevel = 7;
    bool capeOn = false;
    // world
    bool terrainOff = false;
    bool noCollision = false;
    bool barrierOff = false;
    bool shadowOff = false;
    float friction = 1.f;
    bool frictionOn = false;
    // shout
    float shoutColor[3] = {1, 1, 1};
    float shoutIntensity = 1.f;
    int voice = 0;
    bool voiceOn = false;
    bool shoutRepeat = false;
    float shoutPerSecond = 1.f;
    double shoutNext = 0;
    std::vector<std::string> voices;
    bool voicesRead = false;
} F;

bool ReadByte(uint32_t off, uint8_t& v) { return GameBase() && ReadMemory(GameBase() + off, &v, 1); }

bool SetSwitch(const char* what, uint32_t off, bool on) {
    uint8_t cur = 0xFF;
    if (!ReadByte(off, cur) || cur > 1) {
        Log("%s: the switch at 0x%X doesn't look right (value %u); not changed", what, off, cur);
        return false;
    }
    uint8_t v = on ? 1 : 0;
    bool ok = WriteGameMemory(off, &v, 1);
    Log("%s %s%s", what, on ? "on" : "off", ok ? "" : " (write failed)");
    return ok;
}

// No collision: the call at 0x296CAD0 fills the list of things you could collide with; its count is set to 0 just
// before (0x296CAA0), so skipping the call (a NOP) leaves the list empty.
constexpr uint32_t kCollisionCallAt = 0x296CAD0;
constexpr uint32_t kCollisionCall = 0x94000C7C;  // bl 0x296FCC0
constexpr uint32_t kNop = 0xD503201F;

bool SetNoCollision(bool on) {
    uint32_t cur = 0;
    if (!GameBase() || !ReadMemory(GameBase() + kCollisionCallAt, &cur, 4) || (cur != kCollisionCall && cur != kNop)) {
        Log("No collision: the game code isn't what 0.35.2 has (0x%08X); not changed", cur);
        return false;
    }
    uint32_t v = on ? kNop : kCollisionCall;
    bool ok = WriteGameMemory(kCollisionCallAt, &v, 4);
    Log("No collision %s%s", on ? "on" : "off", ok ? "" : " (write failed)");
    return ok;
}

void ApplyFriction(bool on, float value) {
    uint32_t cur[2] = {0, 0};
    if (!GameBase() || !ReadMemory(GameBase() + kSlideyValueAt, cur, sizeof cur)) {
        Log("Surface friction: game code not readable");
        return;
    }
    bool original = cur[0] == kSlideyOriginal[0] && cur[1] == kSlideyOriginal[1];
    bool ours = (cur[0] & 0xFFE0001F) == 0x52800008 && (cur[1] & 0xFFE0001F) == 0x72A00008;
    if (!original && !ours) {
        Log("Surface friction: the game code isn't what this build expects; not changed");
        return;
    }
    if (on) {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        uint32_t code[2] = {0x52800008u | ((bits & 0xFFFF) << 5), 0x72A00008u | ((bits >> 16) << 5)};
        if (WriteGameMemory(kSlideyValueAt, code, sizeof code)) SetSwitch("Surface friction", kSlideySwitch, true);
    } else {
        SetSwitch("Surface friction", kSlideySwitch, false);
        WriteGameMemory(kSlideyValueAt, kSlideyOriginal, sizeof kSlideyOriginal);
    }
}

void Shout(bool repeat = false) {
    char color[96];
    snprintf(color, sizeof color, "{%.3f, %.3f, %.3f}", F.shoutColor[0], F.shoutColor[1], F.shoutColor[2]);
    StartEvent(repeat ? "" : "Shout", "shout", "AvatarDoShout", {{"color", color, ""}, {"intensity", Num(F.shoutIntensity), ""}});
}

void ApplyVoice() {
    if (!F.voiceOn || F.voice <= 0 || F.voice >= (int)F.voices.size()) {
        StopEvent("Voice back to your own", "voice");
        return;
    }
    StartEvent("Voice: " + F.voices[F.voice], "voice", "AvatarSetVoice",
               {{"voiceType", Quote(F.voices[F.voice]), std::to_string(F.voice)},
                {"immediateMode", "true", ""},
                {"fireOnce", "false", ""}});
}

// Hover: rise at kHoverRise per second for (height / kHoverRise) seconds, then hold there weightless (upward speed 0,
// gravity 0). Reset switches it off and you land.
constexpr float kHoverRise = 3.f;
double gHoverHoldAt = 0;

void Hover() {
    gHoverHoldAt = 0;
    if (F.hover <= 0.f) {
        StopEvent("Hover off", "hover");
        return;
    }
    StartEvent("Hover: rising " + Num(F.hover), "hover", "AvatarAntiGravity",
               {{"upwardSpeed", Num(kHoverRise), ""}, {"gravityMult", "0", ""}, {"forceTakeoff", "true", ""}});
    gHoverHoldAt = ImGui::GetTime() + F.hover / kHoverRise;
}

void HoverHold() {
    StartEvent("Hover: holding", "hover", "AvatarAntiGravity",
               {{"upwardSpeed", "0", ""}, {"gravityMult", "0", ""}, {"forceTakeoff", "false", ""}});
}

void SpellButton(const Spell& s) {
    std::string label = SpellLabel(s.id);
    if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
        StartEvent("Transform: " + label, "spell", "GrantSpell",
                   {{"buffName", Quote(s.id), ""}, {"duration", Num(F.spellSeconds), ""}, {"networked", "false", ""}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.id);
}

// ---------------------------------------------------------------- icons and game data (through Canvas)

// Draws one of the game's UI icons; false (nothing drawn) when the game has no icon by that name.
bool DrawIcon(const std::string& name, float size) {
    if (name.empty()) return false;
    UIIcon icon{};
    icon.textureId = (ImTextureID)-1;
    IconLoader::getUIIcon(name, &icon);
    if (icon.textureId == (ImTextureID)-1 || !icon.textureId) return false;
    ImGui::Image(icon.textureId, ImVec2(size, size), icon.uv0, icon.uv1);
    return true;
}

// Icon (or an empty square of the same size) and then a button with the label on the same line.
bool IconRow(const std::string& icon, const std::string& label, const char* id) {
    float sz = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    if (!DrawIcon(icon, sz)) ImGui::Dummy(ImVec2(sz, sz));
    ImGui::SameLine();
    bool r = ImGui::Button(label.c_str(), ImVec2(-1, 0));
    ImGui::PopID();
    return r;
}

// One entry of the game's outfit or collectible file: only what the lists need.
struct DefItem { std::string name, type, icon; int maxLevel = 1; };

// Reads a definition file from the game's assets once, keeping only name/type/icon/max_level (a full tree of the
// game's outfit file took a lot of memory).
void LoadDefs(const char* path, const char* arrayKey, std::vector<DefItem>& out, bool& tried) {
    if (tried || !GameBase()) return;
    tried = true;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", path);
    const char* text = Cipher::read_asset(buf);
    if (!text) {
        Log("%s: couldn't be read", path);
        return;
    }
    static const std::vector<std::string> kKeep = {"name", "type", "icon", "max_level"};
    Json j = Json::Parse(text, &kKeep);
    const std::vector<Json>* list = j.kind == Json::kArray ? &j.a : nullptr;
    if (!list)
        for (const char* k : {arrayKey, "items"})
            if (const Json* v = j.get(k)) if (v->kind == Json::kArray) { list = &v->a; break; }
    if (!list) {
        Log("%s: couldn't be parsed", path);
        return;
    }
    out.clear();
    out.reserve(list->size());
    for (const auto& it : *list) {
        DefItem d{it.str("name"), it.str("type"), it.str("icon"), (int)it.num("max_level", 1)};
        for (auto& c : d.type) c = (char)tolower((unsigned char)c);
        if (!d.name.empty()) out.push_back(std::move(d));
    }
    out.shrink_to_fit();
    Log("%s: %zu entries", path, out.size());
}

const std::vector<Json>* ArrayOf(const Json& j, const char* key) {
    if (j.kind == Json::kArray) return &j.a;
    if (const Json* v = j.get(key)) if (v->kind == Json::kArray) return &v->a;
    return nullptr;
}

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------- extra state for the newer features

struct More {
    // server (off at every start)
    bool server = false;
    // animation
    bool pause = false;
    bool tpose = false;
    double tposeHoldAt = 0;
    char anim[64] = "AirplaneArmsEmote_1";
    // follow (Lua probe)
    std::string probe;
    double probeAt = 0;
    double crawlStopAt = 0;
    double pauseNext = 0;    // Stop all animations / T-pose: short pauses renewed while on
    bool tposeHold = false;
    double gravityResetAt = 0;
    double killNext = 0;  // when to stop the "crawl 0" event after Crawl off
    // outfits
    std::vector<DefItem> outfits;
    bool outfitTried = false;
    std::vector<std::pair<std::string, std::string>> outfitTypes;  // category, its first icon
    std::vector<int> outfitRows;   // filtered list, rebuilt only when the filter changes
    std::string outfitRowsFor = "\x01";
    std::string outfitType;
    bool npcOutfits = false;
    char outfitFilter[64] = "";
    // edit outfits
    float height = 0.f, size = 0.f;
    // neck
    bool neck = false;
    // pogo
    float pogo = 8.f;
    bool pogoKeep = false;
    float pogoEvery = 1.f;
    double pogoNext = 0;
    // emotes
    std::vector<DefItem> collectibles;
    bool collectiblesTried = false;
    std::vector<int> emoteRows;
    std::string emoteRowsFor = "\x01";
    int emoteLevel = 1;
    float emoteSpeed = 1.f, emoteLoop = 0.f;
    bool emoteMirror = false;
    char emoteFilter[64] = "";
    // camera
    float shakeSpeed = 1.f, shakeForce = 1.f, shakeTime = 2.f;
    bool filter = false;
    int filterType = 0;
    float filterAmount = 1.f;
    bool filterInvert = false;
    bool sunglass = false, tint = false, surreal = false, caustics = false, sketch = false, noBars = false;
    bool rain = false;
    float rainAmount = 0.5f, rainSize = 1.f, rainDensity = 1.f;
    float slowmo = 1.f;
    std::vector<std::string> lenses;
    bool lensesRead = false;
    int lens = -1;
    bool debugCam = false, manualCam = false;
    // auto walk
    bool walk = false;
    float walkSpeed = 3.f, walkDir = 0.f;
} M;

constexpr uint32_t kAvatarHeight = 0x313970C;  // TheVoid's player.avatar_height (0x31396FC in 0.35.1), -3..3
constexpr uint32_t kAvatarScale = 0x3139710;   // TheVoid's player.avatar_scale (0x3139700 in 0.35.1), -3..3

void SetFloatVar(const char* what, uint32_t off, float v) {
    float cur = 0;
    if (!GameBase() || !ReadMemory(GameBase() + off, &cur, 4) || !(cur >= -10.f && cur <= 10.f)) {
        Log("%s: the value at 0x%X doesn't look right; not changed", what, off);
        return;
    }
    bool ok = WriteGameMemory(off, &v, 4);
    Log("%s %.2f%s", what, v, ok ? "" : " (write failed)");
}

void Walk() {
    if (!M.walk) {
        StopEvent("Auto walk off", "walk");
        return;
    }
    float rad = M.walkDir * 3.14159265f / 180.f;
    StartEvent("Auto walk " + Num(M.walkSpeed) + " at " + Num(M.walkDir) + " deg", "walk", "SetAvatarVelocity",
               {{"velocityX", Num(M.walkSpeed * sinf(rad)), ""}, {"velocityZ", Num(M.walkSpeed * cosf(rad)), ""},
                {"setVelocityX", "true", ""}, {"setVelocityZ", "true", ""}, {"fireOnce", "false", ""}});
}

void Bounce(bool quiet) {
    StartEvent(quiet ? "" : "Bounce " + Num(M.pogo), "pogo", "SetAvatarMinYSpeed",
               {{"minYSpeed", Num(M.pogo), ""}, {"forceTakeoff", "true", ""}, {"fireOnce", "true", ""}});
}

// Neck: the head keeps looking at the camera (a marker that GetCameraTransform keeps at the camera's position), so
// turning the camera turns the neck. Reading a marker's position from Lua (marker:pos() with no value) crashes the
// game natively (0.7.0 log, 0x1462018), so there are no offset sliders.
void Neck() {
    Run("", std::string(kPrelude) + "OffsetLab.camM = OffsetLab.camM or game:markerBarn():CreateMarker(0)\n");
    StartEvent("", "neckcam", "GetCameraTransform", {{"target", "OffsetLab.camM", ""}, {"fireOnce", "false", ""}});
    StartEvent("Neck turn on", "neck", "AvatarLookAt", {{"target", "OffsetLab.camM", ""}, {"overwriteAnim", "true", ""}});
}

void NeckOff() { StopEvent("Neck turn off", "neck"), StopEvent("", "neckcam"); }

void ProbeLua() {
    std::string path = DataPath("offsetlab_lua_api.txt");
    if (path.empty()) {
        Log("Lua probe: no folder to write to");
        return;
    }
    std::string s =
        "local out = {}\n"
        "local function keys(label, o)\n"
        "  if o == nil then out[#out+1] = label .. \": none\"; return end\n"
        "  local mt = getmetatable(o); local idx = mt and rawget(mt, \"__index\")\n"
        "  if type(idx) == \"table\" then for k in pairs(idx) do out[#out+1] = label .. \":\" .. tostring(k) end end\n"
        "  if type(mt) == \"table\" then for k in pairs(mt) do out[#out+1] = label .. \" meta \" .. tostring(k) end end\n"
        "  if type(o) == \"table\" then for k in pairs(o) do out[#out+1] = label .. \".\" .. tostring(k) end end\n"
        "end\n"
        "keys(\"game\", game)\n"
        "for _, b in ipairs({\"avatarBarn\", \"npcBarn\", \"playerBarn\", \"markerBarn\", \"levelBarn\", \"socialBarn\",\n"
        "  \"friendBarn\", \"netBarn\", \"cameraBarn\", \"localAvatar\", \"avatar\", \"player\"}) do\n"
        "  local ok, v = pcall(function() return game[b](game) end)\n"
        "  keys(b, ok and v or nil)\n"
        "end\n"
        "for k, v in pairs(_G) do out[#out+1] = \"_G.\" .. tostring(k) .. \" \" .. type(v) end\n"
        "table.sort(out)\n"
        "local f = io and io.open(" + Quote(path) + ", \"w\")\n"
        "if not f then error(\"no file access from Lua\") end\n"
        "f:write(table.concat(out, \"\\n\")); f:close()\n";
    if (Run("Lua probe sent; it writes offsetlab_lua_api.txt", s)) M.probeAt = ImGui::GetTime() + 1.5;
}

// ---------------------------------------------------------------- sub-tabs

void PlayerSection(bool inWorld) {
    Section("MOVEMENT", nullptr);
    ImGui::TextUnformatted("Player hover");
    Dim("Rises this high from where you are, then holds you there. Reset to land.");
    if (Slider("hover", &F.hover, 0.f, 20.f, 0.f, "%.1f m")) Hover();

    if (Toggle("Antigravity", &F.antigravity, "No gravity for your avatar.")) {
        StartEvent(F.antigravity ? "Antigravity on" : "Antigravity off", "gravity", "SetAvatarGravityMult",
                   {{"gravityMult", F.antigravity ? "0" : "1", ""}});
        if (!F.antigravity) M.gravityResetAt = ImGui::GetTime() + 1.0;  // normal gravity, then switch the event off
    }

    ImGui::TextUnformatted("Speed up / down");
    Dim("Walking and running speed (1 = normal).");
    if (Slider("speed", &F.speed, 0.1f, 6.f, 1.f, "%.2fx"))
        StartEvent("Speed " + Num(F.speed), "speed", "SetAvatarRunSpeedMult", {{"runSpeed", Num(F.speed), ""}});

    ImGui::TextUnformatted("Auto walk");
    Dim("Moves you at a set speed in a fixed direction (degrees around the world, 0 = the level's forward).");
    if (ImGui::Checkbox("Auto walk on", &M.walk)) Walk();
    if (Slider("walkspeed", &M.walkSpeed, 0.f, 20.f, 3.f, "speed %.1f") && M.walk) Walk();
    if (Slider("walkdir", &M.walkDir, 0.f, 360.f, 0.f, "%.0f deg") && M.walk) Walk();

    ImGui::TextUnformatted("Pogo stick");
    Dim("Bounces you up at this speed: higher = farther jumps. Bounce once, or keep bouncing.");
    Slider("pogo", &M.pogo, 1.f, 60.f, 8.f, "%.1f");
    if (ImGui::Button("Bounce", ImVec2(Px(200), 0))) Bounce(false);
    ImGui::SameLine();
    ImGui::Checkbox("Keep bouncing", &M.pogoKeep);
    Slider("pogoevery", &M.pogoEvery, 0.2f, 5.f, 1.f, "every %.1f s");

    if (Toggle("Scatter", &F.scatter, "Keeps letting go of hand-holds and carries (yours and others' on you).") &&
        !F.scatter)
        StopEvent("Scatter off", "scatter");
    if (Toggle("Crawl mode", &F.crawl, "Your avatar crawls.")) {
        if (F.crawl) StartEvent("Crawl on", "crawl", "AvatarCrawl", {{"crawlAmount", "1.0", ""}});
        else {  // stopping the event leaves the avatar crawling: set crawling to 0 and stand up
            StartEvent("Crawl off", "crawl", "AvatarCrawl", {{"crawlAmount", "0.0", ""}});
            StartEvent("", "stand", "AvatarSetStance",
                       {{"stand", "true", ""}, {"instant", "true", ""}, {"fireOnce", "true", ""}});
            M.crawlStopAt = ImGui::GetTime() + 1.0;
        }
    }
    if (Toggle("Force flight", &F.flight, "Keeps you in flight mode while on.")) {
        if (F.flight) StartEvent("Force flight on", "flight", "AvatarForceEnableFlightMode", {{"fireOnce", "false", ""}});
        else StopEvent("Force flight off", "flight");
    }
    if (Toggle("Walk under water", &F.walkUnderWater, "Walk along the bottom instead of swimming.")) {
        if (F.walkUnderWater) StartEvent("Walk under water on", "water", "WalkUnderWater", {});
        else StopEvent("Walk under water off", "water");
    }
    if (Toggle("Kill avatar", &F.kill, "Your avatar dies (the game's own death), again every 3 s while on."))
        M.killNext = 0, Log("Kill avatar %s", F.kill ? "on" : "off");

    Section("ANIMATION", nullptr);
    if (Toggle("Stop all animations", &M.pause, "Freezes your avatar's animation (walking included) while on.")) {
        if (M.pause) M.pauseNext = 0, Log("Animations stopped");  // the tick renews a 1 s pause while on
        else StopEvent("Animations back", "pause");
    }
    if (Toggle("T-pose", &M.tpose, "Arms out, held still (the game's arms-out animation, then frozen).")) {
        if (M.tpose) {
            StartEvent("T-pose", "tpose", "PlayAvatarAnimSeq",
                       {{"animName", Quote(M.anim), ""}, {"paradeEmote", "true", ""}, {"speedMultiplier", "1.0", ""}});
            M.tposeHoldAt = ImGui::GetTime() + 0.7;
        } else {
            M.tposeHoldAt = 0;
            M.tposeHold = false;
            StopEvent("", "tposehold");
            StopEvent("T-pose off", "tpose");
            StartEvent("", "animstop", "StopAvatarAnimSeq", {{"fadeOutTime", "0.2", ""}});
        }
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(220));
    ImGui::InputText("##anim", M.anim, sizeof M.anim);
    ImGui::SameLine();
    if (ImGui::Button("Play", ImVec2(Px(100), 0)))
        StartEvent(std::string("Animation ") + M.anim, "anim", "PlayAvatarAnimSeq",
                   {{"animName", Quote(M.anim), ""}, {"paradeEmote", "true", ""}});
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(Px(100), 0))) {
        StopEvent("Animation stopped", "anim");
        StartEvent("", "animstop", "StopAvatarAnimSeq", {{"fadeOutTime", "0.2", ""}});
    }
    Dim("Any of the game's animation names (T-pose uses AirplaneArmsEmote_1).");

    ImGui::TextUnformatted("Neck turn (experimental)");
    Dim("Your head keeps looking at the camera: turn the camera up, down or sideways and the neck follows. (Sliders "
        "aren't possible: the game crashes when Lua reads a position, which offsets would need.)");
    if (ImGui::Checkbox("Neck turn on", &M.neck)) {
        if (M.neck) Neck();
        else NeckOff();
    }

    Section("FOLLOW", nullptr);
    ImGui::TextColored(kAmber300, "Not available yet.");
    Dim("The game has no event that follows another player, NPC or creature, and Offset Lab can't read their "
        "positions yet. To find a way in, Look for access lists what the game's Lua offers (players, NPCs, "
        "barns) and saves it next to the mod as offsetlab_lua_api.txt. Send me that file and I'll build Follow on it.");
    if (ImGui::Button("Look for access")) ProbeLua();
    if (M.probeAt > 0 && ImGui::GetTime() >= M.probeAt) {
        M.probeAt = 0;
        M.probe = ReadTextFile(DataPath("offsetlab_lua_api.txt"));
        if (M.probe.empty()) Log("Lua probe: no file written (the game's Lua may have no file access; see the Log)");
    }
    if (!M.probe.empty()) {
        Dim("%zu bytes found:", M.probe.size());
        ImGui::InputTextMultiline("##probe", &M.probe[0], M.probe.size() + 1, ImVec2(-1, Px(300)),
                                  ImGuiInputTextFlags_ReadOnly);
    }
    (void)inWorld;
}

void TransformSection() {
    Section("TRANSFORM AVATAR", "Becomes a form for the time below (the game's spell effects, local only).");
    Slider("Duration", &F.spellSeconds, 5.f, 600.f, 60.f, "%.0f s");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##spf", "search", F.spellFilter, sizeof F.spellFilter);
    ImGui::SameLine();
    ImGui::Checkbox("Other spells too", &F.showAllSpells);
    if (ImGui::BeginChild("##spells", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 9.5f), ImGuiChildFlags_Border)) {
        for (const auto& s : kSpells) {
            if (!s.form && !F.showAllSpells) continue;
            if (F.spellFilter[0] && !strstr(s.id, F.spellFilter)) continue;
            ImGui::PushID(s.id);
            SpellButton(s);
            ImGui::PopID();
        }
    }
    TouchScroll();
    ImGui::EndChild();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##custom", "another spell id", F.customSpell, sizeof F.customSpell);
    ImGui::SameLine();
    if (ImGui::Button("Use") && F.customSpell[0])
        StartEvent(std::string("Transform: ") + F.customSpell, "spell", "GrantSpell",
                   {{"buffName", Quote(F.customSpell), ""}, {"duration", Num(F.spellSeconds), ""},
                    {"networked", "false", ""}});
    if (ImGui::Button("Back to normal (remove spells)"))
        StopEvent("Transform stopped", "spell"),
            StartEvent("All spell effects removed", "unspell", "RemoveSpell", {{"buffName", "\"\"", ""}});

    Section("WINGED LIGHT", nullptr);
    ImGui::TextUnformatted("Temporary (this game session only)");
    Dim("Shows your cape at this wing level. Switch off or Reset to get your real level back.");
    bool changed = ImGui::Checkbox("Use this level", &F.capeOn);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Px(160));
    if (ImGui::InputInt("##lvl", &F.capeLevel)) changed = F.capeOn;
    if (F.capeLevel < 0) F.capeLevel = 0;
    int r = SliderInt("cape", &F.capeLevel, 0, 20, 7);
    if (r == 2) F.capeOn = false, changed = true;
    else if (r == 1 && F.capeOn) changed = true;
    if (changed) {
        if (F.capeOn)
            StartEvent("Winged light level " + std::to_string(F.capeLevel), "cape", "AvatarOverrideCapeLevel",
                       {{"level", std::to_string(F.capeLevel), ""}, {"autoRevertOnStop", "true", ""},
                        {"restoreEnergyOnStop", "true", ""}});
        else
            StopEvent("Winged light back to your own", "cape");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Permanent (server, experimental)");
    Dim("Sends the game's own winged light requests to the server you're connected to; the server decides whether "
        "anything changes. The game normally fills these in itself, so they may be refused or do nothing. Each "
        "press is one request.");
    ImGui::Checkbox("Server requests on (off at every start)", &M.server);
    ImGui::BeginDisabled(!M.server);
    static const char* kReq[][2] = {{"Collect", "CollectWingBuffServerRequestEvent"},
                                    {"Deposit", "DepositWingBuffServerRequestEvent"},
                                    {"Convert", "ConvertWingBuffsServerRequestEvent"},
                                    {"Drop", "DropWingBuffServerRequestEvent"}};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(kReq[i][0], ImVec2(Px(170), 0)))
            StartEvent(std::string("Winged light request: ") + kReq[i][0], "wingreq", kReq[i][1], {});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", kReq[i][1]);
    }
    ImGui::EndDisabled();
}

void OutfitsSection() {
    LoadDefs("Data/Resources/OutfitDefs.json", "outfits", M.outfits, M.outfitTried);
    Section("CHANGE OUTFITS", "Wears an outfit on your avatar (AvatarSetOutfit; local). The list is the game's own "
                              "outfit file, NPC outfits included when switched on.");
    if (M.outfits.empty()) {
        Dim("The outfit list couldn't be read from the game yet (enter the world and open this again).");
        if (ImGui::Button("Try again")) M.outfitTried = false, M.outfitTypes.clear();
        return;
    }
    if (M.outfitTypes.empty()) {  // categories (outfit types), each with its first item's icon: once
        for (const auto& it : M.outfits) {
            if (it.type.empty()) continue;
            bool have = false;
            for (auto& x : M.outfitTypes) have |= x.first == it.type;
            if (!have) M.outfitTypes.push_back({it.type, it.icon});
        }
        M.outfitRowsFor = "\x01";
    }
    const auto& types = M.outfitTypes;
    if (M.outfitType.empty() && !types.empty()) M.outfitType = types[0].first;
    float sz = ImGui::GetFrameHeight();
    for (size_t i = 0; i < types.size(); i++) {
        ImGui::PushID((int)i);
        if (i % 5) ImGui::SameLine();
        ImGui::BeginGroup();
        bool sel = types[i].first == M.outfitType;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.55f));
        if (!DrawIcon(types[i].second, sz)) ImGui::Dummy(ImVec2(sz, sz));
        ImGui::SameLine(0, 4);
        if (ImGui::Button(types[i].first.c_str(), ImVec2(Px(130), 0))) M.outfitType = types[i].first;
        if (sel) ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##of", "search", M.outfitFilter, sizeof M.outfitFilter);
    ImGui::SameLine();
    ImGui::Checkbox("NPC outfits too", &M.npcOutfits);
    std::string q = Lower(M.outfitFilter);
    std::string want = M.outfitType + "|" + q + (M.npcOutfits ? "|npc" : "|kid");
    if (want != M.outfitRowsFor) {  // only when the category, search or NPC switch changed
        M.outfitRowsFor = want;
        M.outfitRows.clear();
        for (int i = 0; i < (int)M.outfits.size(); i++) {
            const auto& it = M.outfits[i];
            if (it.type != M.outfitType) continue;
            if (!M.npcOutfits && it.name.rfind("CharSkyKid", 0) != 0) continue;
            if (!q.empty() && Lower(it.name).find(q) == std::string::npos) continue;
            M.outfitRows.push_back(i);
        }
    }
    const auto& rows = M.outfitRows;
    Dim("%zu outfits", rows.size());
    if (ImGui::BeginChild("##outfits", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 10.5f), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const DefItem& d = M.outfits[rows[i]];
                const std::string& name = d.name;
                std::string label = name.rfind("CharSkyKid_", 0) == 0 ? name.substr(11) : name;
                if (IconRow(d.icon, label, name.c_str()))
                    StartEvent("Outfit: " + name, "outfit_" + M.outfitType, "AvatarSetOutfit",
                               {{M.outfitType, Quote(name), ""}});
            }
    }
    TouchScroll();
    ImGui::EndChild();
    if (ImGui::Button("Back to my outfit")) {
        for (const auto& t : types) StopEvent("", ("outfit_" + t.first).c_str());
        Log("Outfit changes stopped");
    }

    Section("EDIT OUTFITS", "Your avatar's height and size (TheVoid's height/scale values; the game holds them to "
                            "-3..3). Reset = 0.");
    if (Slider("height", &M.height, -3.f, 3.f, 0.f, "height %.2f")) SetFloatVar("Avatar height", kAvatarHeight, M.height);
    if (Slider("size", &M.size, -3.f, 3.f, 0.f, "size %.2f")) SetFloatVar("Avatar size", kAvatarScale, M.size);
    ImGui::Spacing();
    ImGui::TextUnformatted("Move and resize pieces (the game's outfit editor)");
    Dim("Opens the game's own developer editor for one outfit slot (its Lua functions DebugBodyDef(game), "
        "DebugHairDef(game)...; the same editor TSM's Outfit Debug Menu opens). Move, rotate and resize the piece "
        "there. Local to your game.");
    static const char* kEditors[][2] = {{"Body", "DebugBodyDef"}, {"Cape", "DebugWingDef"}, {"Hair", "DebugHairDef"},
                                        {"Hat", "DebugHatDef"},   {"Mask", "DebugMaskDef"}, {"Face", "DebugFaceDef"},
                                        {"Neck", "DebugNeckDef"}, {"Horn", "DebugHornDef"}, {"Feet", "DebugFeetDef"},
                                        {"Prop", "DebugPropDef"}};
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < 10; i++) {
        if (i % 5) ImGui::SameLine();
        if (ImGui::Button(kEditors[i][0], ImVec2(bw, Dp(44)))) {
            std::string fn = kEditors[i][1];
            Run(std::string("Outfit editor: ") + kEditors[i][0],
                "if type(" + fn + ") ~= \"function\" then error(\"" + fn + " isn't in this game build\") end\n" + fn +
                    "(game)\n");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s(game)", kEditors[i][1]);
    }
}

void EmotesSection() {
    LoadDefs("Data/Resources/CollectibleDefs.json", "collectibles", M.collectibles, M.collectiblesTried);
    Section("EMOTES", "Plays any emote on your avatar (AvatarPlayEmote), with level, speed and loop time.");
    SliderInt("level", &M.emoteLevel, 1, 6, 1);
    Slider("espeed", &M.emoteSpeed, 0.1f, 4.f, 1.f, "speed %.2f");
    Slider("eloop", &M.emoteLoop, 0.f, 30.f, 0.f, M.emoteLoop == 0.f ? "loop: forever" : "loop %.1f s");
    ImGui::Checkbox("Mirror", &M.emoteMirror);
    ImGui::SameLine();
    if (ImGui::Button("Stop emote"))
        StartEvent("Emote stopped", "", "StopAvatarAnimSeq", {{"fadeOutTime", "0.2", ""}});
    if (M.collectibles.empty()) {
        Dim("The emote list couldn't be read from the game yet (enter the world and open this again).");
        if (ImGui::Button("Try again")) M.collectiblesTried = false;
        return;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ef", "search", M.emoteFilter, sizeof M.emoteFilter);
    std::string q = Lower(M.emoteFilter);
    if (q != M.emoteRowsFor) {  // only when the search changed
        M.emoteRowsFor = q;
        M.emoteRows.clear();
        for (int i = 0; i < (int)M.collectibles.size(); i++) {
            const auto& it = M.collectibles[i];
            if (it.type != "emote") continue;
            if (!q.empty() && Lower(it.name).find(q) == std::string::npos) continue;
            M.emoteRows.push_back(i);
        }
    }
    const auto& rows = M.emoteRows;
    if (ImGui::BeginChild("##emotes", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 12.5f), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const DefItem& d = M.collectibles[rows[i]];
                std::string name = d.name, icon = d.icon;
                if (icon.size() > 4 && icon.compare(icon.size() - 4, 4, "Anim") == 0) icon.replace(icon.size() - 4, 4, "0");
                int maxLv = d.maxLevel;
                std::string label = name + "  (levels 1-" + std::to_string(maxLv) + ")";
                if (IconRow(icon, label, name.c_str())) {
                    int lv = std::min(std::max(1, M.emoteLevel), std::max(1, maxLv));
                    StartEvent("Emote " + name + " level " + std::to_string(lv), "emote", "AvatarPlayEmote",
                               {{"name", Quote(name), ""}, {"level", std::to_string(lv), ""},
                                {"speedMultiplier", Num(M.emoteSpeed), ""}, {"emoteLoopDuration", Num(M.emoteLoop), ""},
                                {"mirror", M.emoteMirror ? "true" : "false", ""}},
                               true);
                }
            }
    }
    TouchScroll();
    ImGui::EndChild();
}

void CameraSection() {
    Section("CAMERA SHAKE", nullptr);
    Slider("shspeed", &M.shakeSpeed, 0.f, 10.f, 1.f, "speed %.2f");
    Slider("shforce", &M.shakeForce, 0.f, 10.f, 1.f, "force %.2f");
    Slider("shtime", &M.shakeTime, 0.f, 60.f, 2.f, "%.1f s");
    if (ImGui::Button("Shake", ImVec2(Px(200), 0)))
        StartEvent("Camera shake", "shake", "SetCameraShake",
                   {{"speed", Num(M.shakeSpeed), ""}, {"force", Num(M.shakeForce), ""}, {"duration", Num(M.shakeTime), ""},
                    {"fadeInDuration", "0.2", ""}, {"fadeOutDuration", "0.5", ""}});

    Section("FILTERS & SHADES", nullptr);
    static const char* kFilters[] = {"kFilterType_Standard", "kFilterType_Red", "kFilterType_Orange",
                                     "kFilterType_Yellow", "kFilterType_Green", "kFilterType_Cyan", "kFilterType_Blue",
                                     "kFilterType_Magenta", "kFilterType_Silver", "kFilterType_Infrared",
                                     "kFilterType_Custom"};
    auto applyFilter = [] {
        if (!M.filter) {
            StopEvent("Filter off", "filter");
            return;
        }
        StartEvent(std::string("Filter ") + (kFilters[M.filterType] + 12), "filter", "CameraDesaturationEffect",
                   {{"filterType", Quote(kFilters[M.filterType]), std::to_string(M.filterType)},
                    {"intensityFloat", Num(M.filterAmount), ""}, {"invert", M.filterInvert ? "true" : "false", ""},
                    {"changeTime", "0.3", ""}});
    };
    if (ImGui::Checkbox("Colour filter", &M.filter)) applyFilter();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
    if (ImGui::BeginCombo("##ft", kFilters[M.filterType] + 12)) {
        for (int i = 0; i < 11; i++)
            if (ImGui::Selectable(kFilters[i] + 12, i == M.filterType)) M.filterType = i, applyFilter();
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##ft", ImVec2(Px(130), 0))) M.filterType = 0, M.filter = false, applyFilter();
    if (Slider("famount", &M.filterAmount, 0.f, 1.f, 1.f, "strength %.2f") && M.filter) applyFilter();
    if (ImGui::Checkbox("Invert", &M.filterInvert) && M.filter) applyFilter();

    struct Fx { const char* label; bool* on; const char* type; const char* help; };
    Fx fx[] = {
        {"Random tint", &M.tint, "CameraRandomTint", "Keeps tinting the view with changing colours."},
        {"Surreal blue", &M.surreal, "CameraSurrealBlueEffect", "Blue dream-like look."},
        {"Caustics", &M.caustics, "CameraCausticsEffect", "Underwater light ripples."},
        {"Sketch (art book)", &M.sketch, "CameraArtBookEffects", "Outlined, drawn look."},
        {"Hide cinematic bars", &M.noBars, "DisableCinematicBar", "No black bars in cutscenes."},
    };
    for (auto& f : fx) {
        ImGui::PushID(f.type);
        if (Toggle(f.label, f.on, f.help)) {
            if (*f.on) StartEvent(std::string(f.label) + " on", f.type, f.type, {});
            else StopEvent(std::string(f.label) + " off", f.type);
        }
        ImGui::PopID();
    }

    auto applyRain = [] {
        if (!M.rain) {
            StopEvent("Rain on screen off", "rain");
            return;
        }
        StartEvent("Rain on screen", "rain", "CameraScreenRainDropEffect",
                   {{"intensity", Num(M.rainAmount), ""}, {"dropSize", Num(M.rainSize), ""},
                    {"density", Num(M.rainDensity), ""}});
    };
    if (Toggle("Rain on screen", &M.rain, "Drops running down the screen.")) applyRain();
    if (Slider("rainamt", &M.rainAmount, 0.f, 1.f, 0.5f, "amount %.2f") && M.rain) applyRain();
    if (Slider("rainsize", &M.rainSize, 0.2f, 4.f, 1.f, "drop size %.2f") && M.rain) applyRain();
    if (Slider("raindens", &M.rainDensity, 0.5f, 3.f, 1.f, "density %.2f") && M.rain) applyRain();

    Section("TIME & LENS", nullptr);
    ImGui::TextUnformatted("Slow motion");
    int r = Slider("slowmo", &M.slowmo, 0.05f, 1.f, 1.f, "speed %.2f");
    if (r == 1) StartEvent("Slow motion " + Num(M.slowmo), "slowmo", "SetBulletTime",
                           {{"speed", Num(M.slowmo), ""}, {"duration", "100000", ""}, {"fadeIn", "0.2", ""}});
    if (r == 2) StopEvent("Normal speed", "slowmo");
    if (ImGui::Button("Photo flash")) StartEvent("Flash", "flash", "CameraPhotoFlash", {});
    if (!M.lensesRead && GameBase()) {
        M.lensesRead = true;
        M.lenses = EnumOptions("LevelCameraDefaults", "lensPreset");
    }
    if (!M.lenses.empty()) {
        ImGui::TextUnformatted("Lens (field of view)");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
        if (ImGui::BeginCombo("##lens", M.lens < 0 ? "level default" : M.lenses[M.lens].c_str())) {
            for (int i = 0; i < (int)M.lenses.size(); i++)
                if (ImGui::Selectable(M.lenses[i].c_str(), i == M.lens)) {
                    M.lens = i;
                    StartEvent("Lens " + M.lenses[i], "lens", "LevelCameraDefaults",
                               {{"lensPreset", Quote(M.lenses[i]), std::to_string(i)}});
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##lens", ImVec2(Px(130), 0))) M.lens = -1, StopEvent("Lens back to the level's", "lens");
    }

    Section("FREE CAMERA (experimental)", "The game's manual camera; it may do nothing in a release build.");
    if (Toggle("Manual camera", &M.manualCam, "ManualControlCamera.")) {
        if (M.manualCam) StartEvent("Manual camera on", "manualcam", "ManualControlCamera", {});
        else StopEvent("Manual camera off", "manualcam");
    }
    Dim("Drone mode: the game's DebugCamera isn't an event, so it can't be started from here. Sunglasses was "
        "removed: it crashed the game when switched off (it has settings Offset Lab can't see to fill in).");
    Dim("First-person view and unlimited zoom need changes to the game's camera code that I haven't checked for "
        "0.35.2 yet; they'll come in a later version.");
}

void WorldSection() {
    Section("WORLD & COLLISION", nullptr);
    if (Toggle("No collision", &F.noCollision,
               "Experimental: the game's collision search returns nothing, so you pass through the ground, walls and "
               "barriers (you'll fall). Switch off to get collision back."))
        if (!SetNoCollision(F.noCollision)) F.noCollision = !F.noCollision;
    if (Toggle("Hide terrain", &F.terrainOff,
               "Hides the visible ground (collision stays). TheVoid's world.hide_terrain switch."))
        if (!SetSwitch("Hide terrain", kTerrainSwitch, F.terrainOff)) F.terrainOff = !F.terrainOff;
    if (Toggle("Disable reset barrier", &F.barrierOff,
               "Experimental: moves the level's reset plane far below the world (the game needs a marker for it: "
               "with none it stops with \"No marker provided for boundary plane\"), so going out of bounds "
               "shouldn't reset you. Switch off to give the level its own plane back.")) {
        if (F.barrierOff) {
            Run("", std::string(kPrelude) +
                        "OffsetLab.lowM = OffsetLab.lowM or game:markerBarn():CreateMarker(0)\n"
                        "OffsetLab.lowM:pos({0, -100000, 0})\n");
            StartEvent("Reset barrier moved far down", "barrier", "AvatarSetBoundaryPlane",
                       {{"planeMarker", "OffsetLab.lowM", ""}});
        } else {
            StopEvent("Reset barrier back", "barrier");
        }
    }
    if (Toggle("Disable shadow", &F.shadowOff, "Removes your avatar's shadow.")) {
        if (F.shadowOff) StartEvent("Shadow off", "shadow", "AvatarDisableShadow", {});
        else StopEvent("Shadow back", "shadow");
    }
    ImGui::TextUnformatted("Surface friction");
    Dim("How much grip the ground has: low = slippery like ice (the game's own slippery value is 0.2). "
        "Reset = the game's normal ground.");
    int r = Slider("friction", &F.friction, 0.f, 2.f, 1.f, "%.2f");
    if (r == 1) F.frictionOn = true, ApplyFriction(true, F.friction);
    if (r == 2) F.frictionOn = false, ApplyFriction(false, 0);
}

void ShoutSection() {
    Section("SHOUT", "Your call: colour, strength, voice; Shout once or keep repeating.");
    if (!F.voicesRead && GameBase()) {
        F.voicesRead = true;
        F.voices = EnumOptions("OnShout", "voiceType");
        if (F.voices.empty()) F.voices = EnumOptions("AvatarOutfitRegion", "voice");
        // "…_Count" is the number of voices, not a voice
        F.voices.erase(std::remove_if(F.voices.begin(), F.voices.end(), [](const std::string& v) {
            return v.size() > 6 && v.compare(v.size() - 6, 6, "_Count") == 0; }), F.voices.end());
    }
    ImGui::ColorEdit3("Colour", F.shoutColor);
    ImGui::SameLine();
    if (ImGui::Button("Reset##col")) F.shoutColor[0] = F.shoutColor[1] = F.shoutColor[2] = 1.f;
    ImGui::TextUnformatted("Intensity");
    Slider("intensity", &F.shoutIntensity, 0.f, 5.f, 1.f, "%.2f");
    if (!F.voices.empty()) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
        std::string cur = F.voices[F.voice];
        if (ImGui::BeginCombo("##voice", cur.c_str())) {
            for (int i = 0; i < (int)F.voices.size(); i++)
                if (ImGui::Selectable(F.voices[i].c_str(), i == F.voice)) F.voice = i, F.voiceOn = i > 0, ApplyVoice();
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##voice", ImVec2(Px(130), 0))) F.voice = 0, F.voiceOn = false, ApplyVoice();
        Dim("Voice type (temporary; Reset gives you your own voice back).");
    } else {
        Dim("Voice types: not readable from the game yet.");
    }
    Dim("Pitch: the game has no pitch setting for shouts in 0.35.2, so it can't be changed here.");
    if (ImGui::Button("Shout", ImVec2(Px(200), 0))) Shout();
    ImGui::SameLine();
    ImGui::Checkbox("Repeat", &F.shoutRepeat);
    ImGui::TextUnformatted("Repeat speed (shouts per second)");
    Slider("rate", &F.shoutPerSecond, 0.2f, 10.f, 1.f, "%.1f / s");
}


// ---------------------------------------------------------------- 0.11.0: energy, modes, items, social

// A one-word change to the game's code: the word is checked against 0.35.2's before writing (ours is accepted too,
// so switching off after a crash of the mod still works) and put back exactly when switched off.
struct CodePatch { const char* what; uint32_t at; uint32_t original; uint32_t patched; };

// Unlock all: the game's "account has unlock" check (0xDFC144, bool (barn, name)) returns 1 when the name is in its
// first set (0xEE74F8 lookup), otherwise the result of a second lookup. The branch to the second lookup
// (0xDFC174, cbz x0) becomes a NOP, so the function always returns 1. One aligned word: always a whole instruction.
const CodePatch kUnlockPatch = {"Unlock all", 0xDFC174, 0xB40000C0u /* cbz x0, #0xdfc18c */, kNop};
// Read all messages: the chat bubble code (0x1478CAC..0x1478CE8) works out in w22 whether a message's text may be
// shown (table/friend checks). The two places that can make it 0 (mov w22, wzr / orr w22, w0, w8) give 1 instead;
// every other path already gives 1.
const CodePatch kReadPatches[2] = {
    {"Read all messages", 0x1478CB0, 0x2A1F03F6u /* mov w22, wzr */, 0x52800036u /* mov w22, #1 */},
    {"Read all messages", 0x1478CE4, 0x2A080016u /* orr w22, w0, w8 */, 0x52800036u /* mov w22, #1 */},
};

bool SetCodePatch(const CodePatch& p, bool on) {
    uint32_t cur = 0;
    if (!GameBase() || !ReadMemory(GameBase() + p.at, &cur, 4) || (cur != p.original && cur != p.patched)) {
        Log("%s: the game code at 0x%X isn't what 0.35.2 has (0x%08X); not changed", p.what, p.at, cur);
        return false;
    }
    uint32_t v = on ? p.patched : p.original;
    if (cur == v) return true;
    if (!WriteGameMemory(p.at, &v, 4)) {
        Log("%s: write failed", p.what);
        return false;
    }
    return true;
}

// The game's debug switches (bytes, 0 normally; TSM's labels): kInvincibility (energy and breath only go up, breath
// kept full: 0x11EEF8C, 0x11EF000, 0x11EAE84), kAutoCharge (always charging as if at a light: 0x11EBC78),
// kDisableRainDrain (no drain in rain: 0x11EB2FC).
constexpr uint32_t kInvincibleSwitch = 0x3131638;
constexpr uint32_t kAutoChargeSwitch = 0x3131634;
constexpr uint32_t kRainDrainSwitch = 0x3131648;

struct Extra {
    float charge = 1.f;
    bool infEnergy = false, infOxygen = false, invincible = false;
    bool micro = false, broom = false, audience = false;
    double audienceStopAt = 0;  // the neutral AudienceSettings is switched off a moment after it ran
    bool unlockAll = false, readAll = false;
    std::string glowOn, walkOn;  // spell granted by Glow / Walk effects ("" = none)
    int glowstick = -1;          // index into kGlowColors, -1 = off
    int dyeSlot = 0, dyeRegion = 0;
    std::vector<std::string> dyes;
    bool dyesRead = false;
    bool closet = false;
    char speak[200] = "";
} X;

// Runs one of the game's Lua functions (its debug/tweak commands, e.g. ReviveAvatar(game)), only if this build has it.
void GameFunction(const std::string& what, const char* fn, const std::string& key) {
    std::string f = fn;
    Run(what, "if type(" + f + ") ~= \"function\" then error(\"" + f + " isn't in this game build\") end\n" + f +
                  "(game)\n", key);
}

void NotPossible(const char* label, const char* why) {
    bool off = false;
    ImGui::BeginDisabled();
    ImGui::Checkbox(label, &off);
    ImGui::EndDisabled();
    ImGui::Indent(ImGui::GetFrameHeight() + 8);
    ImGui::TextColored(kAmber300, "Not possible in 0.35.2 yet.");
    Dim("%s", why);
    ImGui::Unindent(ImGui::GetFrameHeight() + 8);
}

// Glow / Walk effects: one spell at a time. The one before is removed (RemoveSpell with its name) in the same script
// that grants the new one; both are one-shots, never kept. "" = just remove.
void SwapSpell(const std::string& what, std::string& active, const std::string& id, const char* key) {
    std::string s = kPrelude;
    if (!active.empty()) {
        std::string r = EventLua("", "RemoveSpell", {{"buffName", Quote(active), ""}});
        if (r.empty()) return;
        s += r;
    }
    if (!id.empty()) {
        std::string g = EventLua("", "GrantSpell", {{"buffName", Quote(id), ""}, {"duration", "36000", ""},
                                                    {"networked", "false", ""}});
        if (g.empty()) return;
        s += g;
    }
    if (s.size() == strlen(kPrelude)) return;
    if (Run(what, s, key)) active = id;
}

void SpellRow(const char* const* ids, size_t n, std::string& active, const char* key, const char* clearWhat) {
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    for (size_t i = 0; i < n; i++) {
        if (i % 3) ImGui::SameLine();
        bool on = active == ids[i];
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.60f));
        std::string label = SpellLabel(ids[i]) + "##" + key;
        if (ImGui::Button(label.c_str(), ImVec2(bw, 0)) && !on) SwapSpell(SpellLabel(ids[i]) + " on", active, ids[i], key);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ids[i]);
    }
    std::string clear = std::string("Clear##") + key;
    if (ImGui::Button(clear.c_str(), ImVec2(Px(200), 0)) && !active.empty()) SwapSpell(clearWhat, active, "", key);
    if (!active.empty()) {
        ImGui::SameLine();
        Dim("on: %s", active.c_str());
    }
}

struct GlowColor { const char* label; float r, g, b; };
const GlowColor kGlowColors[] = {
    {"Red", 1.f, 0.12f, 0.12f},   {"Orange", 1.f, 0.5f, 0.05f}, {"Yellow", 1.f, 0.9f, 0.1f},
    {"Green", 0.15f, 1.f, 0.2f},  {"Cyan", 0.1f, 0.95f, 1.f},   {"Blue", 0.15f, 0.3f, 1.f},
    {"Purple", 0.6f, 0.2f, 1.f},  {"Pink", 1.f, 0.35f, 0.75f},  {"White", 1.f, 1.f, 1.f},
    {"Rainbow", -1.f, 0.f, 0.f},
};

// Glowstick: AvatarRainbowParty tints your avatar while it runs (its update runs every frame and doesn't stop itself);
// a specific colour or the shifting rainbow. Clear switches it off.
void Glowstick(int i) {
    if (i < 0) {
        if (X.glowstick >= 0) StopEvent("Glowstick off", "glowstick");
        X.glowstick = -1;
        return;
    }
    const GlowColor& c = kGlowColors[i];
    bool rainbow = c.r < 0;
    char col[96];
    snprintf(col, sizeof col, "{%.3f, %.3f, %.3f}", rainbow ? 1.f : c.r, rainbow ? 1.f : c.g, rainbow ? 1.f : c.b);
    if (StartEvent(std::string("Glowstick: ") + c.label, "glowstick", "AvatarRainbowParty",
                   {{"tintLocalPlayer", "true", ""}, {"tintRemotePlayers", "false", ""},
                    {"useSpecificColor", rainbow ? "false" : "true", ""}, {"color", col, ""},
                    {"tintStrength", "0.6", ""}, {"colorBoost", "0.35", ""}, {"innerGlow", "0.002", ""},
                    {"lightIntensity", "5.0", ""}, {"hueShiftRate", "1.0", ""}}))
        X.glowstick = i;
}

// Audience mode and Broom mode share the game's AudienceSettings (one set of audience settings at a time): its update
// writes the settings every frame while it runs. Off: the neutral settings (no reaction menu, your own avatar) run
// for a moment, then it's switched off.
void ApplyAudience() {
    bool any = X.audience || X.broom;
    std::vector<Set> sets = {
        {"requireReservation", "false", ""}, {"replaceLocalAvatar", X.broom ? "true" : "false", ""},
        {"replaceOtherAvatars", "false", ""}, {"showReactionMenu", X.audience ? "true" : "false", ""},
        {"allowHandhold", "true", ""}, {"filterWithNetFlags", "false", ""},
        {"visType", Quote(X.broom ? "Broomstick" : ""), ""}, {"entityTintStrength", "0", ""},
        {"entityScaleMult", "1", ""}, {"emoteTintStrength", "0", ""}};
    std::string what = !any ? "Audience settings back to normal"
                            : std::string(X.broom ? "Broom mode on" : "Broom mode off") +
                                  (X.audience ? ", audience mode on" : ", audience mode off");
    if (StartEvent(what, "audience", "AudienceSettings", sets)) X.audienceStopAt = any ? 0 : ImGui::GetTime() + 1.0;
}

const char* const kDyeSlots[] = {"body", "wing", "hair", "hat", "mask", "face", "neck", "horn", "feet", "prop"};

// Dye outfits: AvatarSetOutfitDye on what you're wearing (local, temporary: a one-shot; your real dyes come back
// when the outfit is loaded again). Its defaults are the developers' descriptions, so every setting is given.
void Dye(const std::string& color, bool random, bool remove) {
    bool all = X.dyeSlot == 0;
    std::string slot = all ? "body" : kDyeSlots[X.dyeSlot - 1];
    std::string what = remove ? "Dye removed" : random ? "Random dye" : "Dye " + color;
    what += all ? " (all slots)" : " (" + slot + (X.dyeRegion ? ", secondary)" : ", primary)");
    StartEvent(what, "dye_" + slot + std::to_string(X.dyeRegion), "AvatarSetOutfitDye",
               {{"npc", "nil", ""}, {"slotName", Quote(slot), ""}, {"region", std::to_string(X.dyeRegion), ""},
                {"removeDye", remove ? "true" : "false", ""}, {"flash", "false", ""},
                {"randomDyeColor", random ? "true" : "false", ""}, {"allSlots", all ? "true" : "false", ""},
                {"dyeColor", Quote(color.empty() ? "white" : color), ""}, {"dyeColorString", "nil", ""},
                {"ignoreIfNone", "false", ""}});
}

// Spawn closet: the game's OutfitSelect (a closet button) with its button marker put at the camera
// (GetCameraTransform once, then dropped to the floor by raycastButtonToFloor) and no region. Kept as "closet"; Remove
// switches it off. Lua can't read positions, so the camera is the only place a marker can be put near you.
void SpawnCloset() {
    std::string cam = EventLua("", "GetCameraTransform", {{"target", "OffsetLab.closetM", ""}, {"fireOnce", "true", ""}});
    std::string closet = EventLua("closet", "OutfitSelect",
                                  {{"closet", "true", ""}, {"buttonPos", "OffsetLab.closetM", ""},
                                   {"raycastButtonToFloor", "true", ""}, {"avatarRegion", "nil", ""},
                                   {"persistPreviewRegion", "nil", ""}, {"showAllPurchased", "false", ""},
                                   {"showAllPurchasedType", Quote("kOutfitSlot_Body"), "0"}, {"onlyOneTab", "false", ""},
                                   {"shopName", "\"\"", ""}, {"lightingIntensity", "1.0", ""}});
    if (cam.empty() || closet.empty()) return;
    if (Run("Closet spawned under the camera", std::string(kPrelude) +
                                                   "OffsetLab.closetM = OffsetLab.closetM or game:markerBarn():CreateMarker(0)\n" +
                                                   cam + closet,
            "closet"))
        X.closet = true;
}

void EnergySection() {
    Section("ENERGY", "Your cape's energy (the light used to fly).");
    ImGui::TextUnformatted("Charge energy");
    Dim("Adds this much energy at once (AvatarChargeEnergy; the game's energy units, 1 = one wedge).");
    Slider("charge", &X.charge, 0.1f, 20.f, 1.f, "%.1f");
    if (ImGui::Button("Charge", ImVec2(Px(200), 0)))
        StartEvent("Charged " + Num(X.charge), "charge", "AvatarChargeEnergy",
                   {{"chargeRate", "0", ""}, {"addAmount", Num(X.charge), ""}, {"duration", "0", ""},
                    {"glow", "true", ""}, {"onlyChargeWhenNotUsing", "false", ""}, {"chargeBreath", "false", ""}});
    if (Toggle("Infinite energy", &X.infEnergy,
               "Always charging, as if next to a light, and no drain in the rain (the game's auto-charge and "
               "no-rain-drain switches). Off: both back to normal.")) {
        bool ok = SetSwitch("Auto charge", kAutoChargeSwitch, X.infEnergy);
        if (ok && !SetSwitch("No rain drain", kRainDrainSwitch, X.infEnergy)) {
            SetSwitch("Auto charge", kAutoChargeSwitch, !X.infEnergy);
            ok = false;
        }
        if (!ok) X.infEnergy = !X.infEnergy;
    }
    if (Toggle("Infinite oxygen", &X.infOxygen,
               "Breath under water kept full every frame (AvatarSetEnergy: set breath, continuous). Off: normal "
               "breathing.")) {
        if (X.infOxygen)
            StartEvent("Infinite oxygen on", "oxygen", "AvatarSetEnergy",
                       {{"value", "1000", ""}, {"increment", "false", ""}, {"setArmor", "false", ""},
                        {"setBreath", "true", ""}, {"setEnergy", "false", ""}, {"setEnergyCapMult", "false", "0"},
                        {"continuous", "true", ""}});
        else
            StopEvent("Infinite oxygen off", "oxygen");
    }

    Section("LIFE", nullptr);
    if (Toggle("Invincibility", &X.invincible,
               "Damage can't take your light or breath (the game's own invincibility switch: energy only goes up, "
               "breath stays full). Falling out of the world still resets you. Off: normal.")) {
        if (!SetSwitch("Invincibility", kInvincibleSwitch, X.invincible)) X.invincible = !X.invincible;
    }
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    if (ImGui::Button("Revive avatar", ImVec2(bw, Dp(44)))) GameFunction("Revive avatar", "ReviveAvatar", "revive");
    ImGui::SameLine();
    if (ImGui::Button("Break elder mask", ImVec2(bw, Dp(44))))
        GameFunction("Elder mask broken", "BreakElderArmor", "eldermask");
    ImGui::SameLine();
    if (ImGui::Button("Restore elder mask", ImVec2(bw, Dp(44))))
        GameFunction("Elder mask restored", "RepairElderArmor", "eldermask");
    Dim("The game's own debug commands ReviveAvatar(game), BreakElderArmor(game) and RepairElderArmor(game) "
        "(experimental: if this build doesn't have one, the Log says so).");
}

void ModesSection() {
    Section("AVATAR MODES", nullptr);
    if (Toggle("Mini mode (experimental)", &X.micro,
               "Everyone looks tiny, on your screen only (SetAvatarMicroMode: its update shrinks every avatar each "
               "frame while it runs). Off: switched off, everyone back to normal size.")) {
        if (X.micro) StartEvent("Mini mode on", "micro", "SetAvatarMicroMode", {});
        else StopEvent("Mini mode off", "micro");
    }
    if (Toggle("Broom mode (experimental)", &X.broom,
               "Your avatar is shown as the game's Broomstick (AudienceSettings: replace your avatar with that "
               "look). Off: your own avatar back.")) ApplyAudience();
    NotPossible("Swim on ground",
                "The game swims only inside its water volumes; no event or Lua function switches the avatar to "
                "swimming elsewhere, and the avatar's water check hasn't been found and verified for 0.35.2.");
    NotPossible("Swim through air", "Same reason as Swim on ground.");
}

void ItemsSection() {
    Section("GLOWSTICK MODE", "Your avatar glows in one colour, or the shifting rainbow (AvatarRainbowParty). One at a "
                              "time; Clear switches it off.");
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < (int)(sizeof kGlowColors / sizeof kGlowColors[0]); i++) {
        if (i % 5) ImGui::SameLine();
        bool on = X.glowstick == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.60f));
        if (ImGui::Button(kGlowColors[i].label, ImVec2(bw, 0)) && !on) Glowstick(i);
        if (on) ImGui::PopStyleColor();
    }
    if (ImGui::Button("Clear##glowstick", ImVec2(Px(200), 0))) Glowstick(-1);

    static const char* const kGlows[] = {"glow", "glow_rainbow", "glow_tgc", "hairtint", "hairtint_rainbow"};
    Section("GLOW", "The game's glow spells (GrantSpell, local, until cleared). One at a time: choosing another "
                    "removes the one before; Clear removes it (RemoveSpell).");
    SpellRow(kGlows, sizeof kGlows / sizeof kGlows[0], X.glowOn, "glow", "Glow cleared");

    static const char* const kWalks[] = {"ground_trail_rainbow", "ground_trail_star", "foot_trail_flower",
                                         "foot_trail_rose", "bloom_rose_trail", "bloom_sakura", "butterfly_pastel",
                                         "trail", "trail_bicolor", "trail_rainbow"};
    Section("WALK EFFECTS", "Trails and footprints (the game's trail spells; the last three are flight trails). "
                            "One at a time, Clear removes it.");
    SpellRow(kWalks, sizeof kWalks / sizeof kWalks[0], X.walkOn, "walk", "Walk effect cleared");

    Section("DYE OUTFITS", "Dyes what you're wearing, on your screen (AvatarSetOutfitDye; temporary: your own dyes "
                           "come back when your outfit loads again, e.g. after a level change).");
    if (!X.dyesRead && GameBase()) {
        X.dyesRead = true;
        for (const auto& o : EnumOptions("PlacePlaceable", "primaryDye")) {
            std::string n = o.rfind("kDyeColor_", 0) == 0 ? o.substr(10) : o;
            n = Lower(n);
            if (n.empty() || n == "none" || n.find("count") != std::string::npos) continue;
            X.dyes.push_back(n);
        }
    }
    static const char* const kSlotLabels[] = {"All slots", "Body", "Cape", "Hair", "Hat", "Mask",
                                              "Face", "Neck", "Horn", "Feet", "Prop"};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 4);
    if (ImGui::BeginCombo("##dyeslot", kSlotLabels[X.dyeSlot])) {
        for (int i = 0; i < 11; i++)
            if (ImGui::Selectable(kSlotLabels[i], i == X.dyeSlot)) X.dyeSlot = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    static const char* const kRegions[] = {"Primary colour", "Secondary colour"};
    if (ImGui::BeginCombo("##dyeregion", kRegions[X.dyeRegion])) {
        for (int i = 0; i < 2; i++)
            if (ImGui::Selectable(kRegions[i], i == X.dyeRegion)) X.dyeRegion = i;
        ImGui::EndCombo();
    }
    if (ImGui::Button("Random", ImVec2(Px(200), 0))) Dye("", true, false);
    ImGui::SameLine();
    if (ImGui::Button("Remove dye", ImVec2(Px(200), 0))) Dye("", false, true);
    if (X.dyes.empty()) {
        Dim("The dye colours couldn't be read from the game yet.");
    } else if (ImGui::BeginChild("##dyes", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 6.5f), ImGuiChildFlags_Border)) {
        float dw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
        for (size_t i = 0; i < X.dyes.size(); i++) {
            if (i % 3) ImGui::SameLine();
            ImGui::PushID((int)i);
            if (ImGui::Button(X.dyes[i].c_str(), ImVec2(dw, 0))) Dye(X.dyes[i], false, false);
            ImGui::PopID();
        }
    }
    if (!X.dyes.empty()) {
        TouchScroll();
        ImGui::EndChild();
    }

    Section("DYE MENU", nullptr);
    if (ImGui::Button("Open dye menu", ImVec2(Px(300), 0)))
        GameFunction("Dye menu opened", "OpenCraftingMenu", "dyemenu");
    Dim("The dye station's menu, anywhere (the game's OpenCraftingMenu(game)). A button, not a toggle: the game has no "
        "call that closes it from outside, so close it with its own button. Dyeing there is real (the server spends "
        "your dyes).");

    Section("CLOSET", nullptr);
    if (ImGui::Button("Spawn closet", ImVec2(Px(250), 0))) SpawnCloset();
    ImGui::SameLine();
    if (ImGui::Button("Remove closet", ImVec2(Px(250), 0)) && X.closet)
        StopEvent("Closet removed", "closet"), X.closet = false;
    Dim("Experimental: a closet button on the ground under the camera, near you (OutfitSelect as a closet, its button "
        "marker put at the camera). Gone after a level change.");

    Section("PROPS", nullptr);
    NotPossible("Bucket brigade",
                "The bucket is a prop event (Bucket, an AvatarPropEvent) that the game starts from an equipped bucket "
                "prop; started from Lua there's no prop for it to act on, and your own prop couldn't be put back "
                "exactly when switched off.");
    NotPossible("Take out candle (red / white)",
                "No event or Lua function makes your own avatar take out its candle: Npc_TakeOutFlame works only on "
                "NPCs, and the avatar's candle follows your input.");
}

void SocialSection() {
    Section("SOCIAL", nullptr);
    if (Toggle("Audience mode (experimental)", &X.audience,
               "The audience reaction menu outside an event (AudienceSettings: show reaction menu, no reservation). "
               "Off: the game's normal audience settings back.")) ApplyAudience();
    if (Toggle("Read all messages", &X.readAll,
               "Shows the text of chat from people you haven't befriended (and tables you're not at) instead of "
               "\"...\": the chat bubble's \"may show text\" check (0x1478CB0, 0x1478CE4) always says yes. Your screen "
               "only. Off: the game's code put back.")) {
        bool ok = SetCodePatch(kReadPatches[0], X.readAll);
        if (ok && !SetCodePatch(kReadPatches[1], X.readAll)) {
            SetCodePatch(kReadPatches[0], !X.readAll);
            ok = false;
        }
        if (!ok) X.readAll = !X.readAll;
        else Log("Read all messages %s", X.readAll ? "on" : "off");
    }
    ImGui::TextUnformatted("Speak to all");
    Dim("Says this over your avatar, like the developers' interview messages (AvatarSpeak). Experimental: who else "
        "sees it is up to the game and your server.");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
    ImGui::InputTextWithHint("##speak", "message", X.speak, sizeof X.speak);
    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(Px(130), 0)) && X.speak[0])
        StartEvent(std::string("Said: ") + X.speak, "speak", "AvatarSpeak",
                   {{"message", Quote(X.speak), ""}, {"localizeMessage", "false", ""}});

    Section("UNLOCKS", nullptr);
    if (Toggle("Unlock all (local only)", &X.unlockAll,
               "Everything the game checks with its \"has unlock\" (emotes, outfits, quests, spells...) counts as "
               "unlocked on your device (0xDFC144 always returns yes). Local only: the server still decides anything "
               "it checks (wearing, buying, saving). Off: the game's code put back; you may need a level change to "
               "see your real unlocks again.")) {
        if (!SetCodePatch(kUnlockPatch, X.unlockAll)) X.unlockAll = !X.unlockAll;
        else Log("Unlock all %s", X.unlockAll ? "on" : "off");
    }
}

int gSub = 0;

}  // namespace

// Recorder: start a recorded event again, through the same path as the features (defaults first, then the values).
bool ReplayEvent(size_t typeIndex, const std::vector<std::pair<std::string, std::string>>& values) {
    IndexTypes();
    size_t n = 0;
    const LabType* t = Types(&n);
    if (typeIndex >= n) return false;
    std::string name = GameString(t[typeIndex].nameAddr);
    if (name.empty() || Fnv1a(name) != t[typeIndex].nameHash) {
        Log("Replay: type #%zu's name doesn't match 0.35.2; not started", typeIndex);
        return false;
    }
    if (EventStatusOf(typeIndex) != kIsEvent) {
        Log("Replay: %s isn't known to be an event; not started", name.c_str());
        return false;
    }
    std::vector<Set> sets;
    for (const auto& v : values) sets.push_back({v.first, v.second, ""});
    return StartEvent("Replay " + name, "", name.c_str(), sets);
}

// Repeating actions; called every frame from Draw, whichever tab is open.
void FeaturesTick() {
    if (!GameObject()) return;
    double t = ImGui::GetTime();
    if (F.scatter && t >= F.scatterNext) {
        F.scatterNext = t + 0.5;
        StartEvent("", "scatter", "AvatarBreakHandHold",
                   {{"breakFromLeader", "true", ""}, {"breakFromFollowers", "true", ""}, {"breakCarryChain", "true", ""}});
    }
    if (F.shoutRepeat && t >= F.shoutNext) {
        F.shoutNext = t + 1.0 / std::max(0.2f, F.shoutPerSecond);
        Shout(true);
    }
    if (M.pogoKeep && t >= M.pogoNext) {
        M.pogoNext = t + std::max(0.2f, M.pogoEvery);
        Bounce(true);
    }
    if ((M.pause || M.tposeHold) && t >= M.pauseNext) {
        M.pauseNext = t + 0.5;
        StartEvent("", M.pause ? "pause" : "tposehold", "AvatarPauseAnim", {{"duration", "1.0", ""}});
    }
    if (gHoverHoldAt > 0 && t >= gHoverHoldAt) {
        gHoverHoldAt = 0;
        HoverHold();
    }
    if (M.gravityResetAt > 0 && t >= M.gravityResetAt) {
        M.gravityResetAt = 0;
        if (!F.antigravity) StopEvent("", "gravity");
    }
    if (F.kill && t >= M.killNext) {
        M.killNext = t + 3.0;
        StartEvent("", "", "KillLocalAvatar", {});
    }
    if (M.crawlStopAt > 0 && t >= M.crawlStopAt) {
        M.crawlStopAt = 0;
        if (!F.crawl) StopEvent("", "crawl"), StopEvent("", "stand");
    }
    if (X.audienceStopAt > 0 && t >= X.audienceStopAt) {
        X.audienceStopAt = 0;
        if (!X.audience && !X.broom) StopEvent("", "audience");
    }
    if (M.tpose && M.tposeHoldAt > 0 && t >= M.tposeHoldAt) {
        M.tposeHoldAt = 0;
        M.tposeHold = true;
    }
}

void FeaturesTab() {
    bool inWorld = GameObject() != 0;
    if (!inWorld) ImGui::TextColored(kAmber300, "Enter the world to use these.");
    Dim("Sliders apply when you let go; Reset puts the game's normal behaviour back. Results show in the Log tab.");
    Dim("%s", LuaStatus().c_str());
    static const char* kSubs[] = {"Player", "Energy", "Transform", "Outfits", "Items",
                                  "Emotes", "Camera", "World",     "Social",  "Shout"};
    // buttons, not a tab bar: a second tab bar inside the Features tab didn't switch on the phone
    const float subW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < 10; i++) {
        if (i % 5) ImGui::SameLine();
        bool sel = gSub == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.60f));
        if (ImGui::Button(kSubs[i], ImVec2(subW, Dp(44))) && !sel) gSub = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##features", ImVec2(0, 0), ImGuiChildFlags_None)) {
        switch (gSub) {
            case 0: PlayerSection(inWorld); break;
            case 1: EnergySection(); break;
            case 2: TransformSection(); ModesSection(); break;
            case 3: OutfitsSection(); break;
            case 4: ItemsSection(); break;
            case 5: EmotesSection(); break;
            case 6: CameraSection(); break;
            case 7: WorldSection(); break;
            case 8: SocialSection(); break;
            case 9: ShoutSection(); break;
        }
    }
    TouchScroll();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Other offsets

namespace {
struct VoidOffset {
    const char* name;
    uint32_t old352_1, now;
    const char* kind;
    const char* hint;
    const char* status;
    const char* usedBy;
};
const VoidOffset kVoid[] = {
#include "generated/void_offsets.inc"
};
char gVoidFilter[64] = "";
}  // namespace

void OtherOffsetsTab() {
    Dim("TheVoid 0.1.2's feature table (Apache-2.0, by Soyu): its names and Sky 0.35.1 addresses, ported to 0.35.2 "
        "by matching the game code that uses each one. Most aren't used by a feature yet; they're kept here so you "
        "can look at them (Memory) or watch code addresses (Native).");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##vf", "search", gVoidFilter, sizeof gVoidFilter);
    if (ImGui::BeginTable("##void", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("0.35.2");
        ImGui::TableSetupColumn("Likely");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        int i = 0;
        for (const auto& v : kVoid) {
            i++;
            if (gVoidFilter[0] && !strstr(v.name, gVoidFilter) && !strstr(v.hint, gVoidFilter)) continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.name);
            if (*v.usedBy) {
                ImGui::SameLine();
                ImGui::TextColored(kSky300, "(%s)", v.usedBy);
            }
            ImGui::TableNextColumn();
            if (v.now) ImGui::Text("0x%X %s", v.now, v.kind);
            else ImGui::TextColored(kRed300, "not found");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0.35.1: 0x%X", v.old352_1);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", *v.hint ? v.hint : "-");
            ImGui::TableNextColumn();
            Dim("%s", v.status);
            ImGui::TableNextColumn();
            if (v.now && ImGui::SmallButton("Memory")) OpenMemoryAt(v.now);
            if (v.now && strcmp(v.kind, "code") == 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Watch")) {
                    std::string err;
                    if (!StartWatch(v.now, v.name, err)) Log("Watch %s: %s", v.name, err.c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace lab
