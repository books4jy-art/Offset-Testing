// Features tab: ready-made controls (toggles, sliders, buttons) built on the game's own events and a few of its
// variables, laid out like TheVoid's menu. Other offsets tab: TheVoid's address table, ported to 0.35.2.
//
// How a feature runs: it starts one of the game's events from Lua and keeps it in the Lua table OffsetLab[key], so it
// can be stopped again. Toggles start the event when switched on and stop it when switched off; sliders start it again
// with the new value when you let go; every slider has a reset button that puts the game's normal behaviour back.
#include "lab.h"

#include "dobby.h"

#include <algorithm>
#include <atomic>
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
#include "dobby.h"
#include <atomic>

namespace lab {

namespace {

const ImVec4 kSky300(0.490f, 0.827f, 0.988f, 1.f);
const ImVec4 kRed300(0.988f, 0.647f, 0.647f, 1.f);
const ImVec4 kAmber300(0.988f, 0.827f, 0.302f, 1.f);

// ---------------------------------------------------------------- AccountAddCurrency native hook
// currencyType is kUnknown in the type table: the game has no Lua setter for it, so we write the byte directly into the
// event object before its handler reads it. The handler at 0xf6c620 reads currencyType as uint8_t from event+0x4C.
constexpr uintptr_t kAddCurrencyHandler = 0xF6C620;
constexpr uint32_t kAddCurrencyFirstInstr = 0xA9BD7BFD;  // stp x29, x30, [sp, #-0x30]!

struct CurrencyEntry { const char* name; uint8_t value; };
static const CurrencyEntry kCurrencyMap[] = {
    {"kCurrencyType_Candle", 1},
    {"kCurrencyType_Vip", 2},
    {"kCurrencyType_Heart", 3},
    {"kCurrencyType_Wax", 4},
    {"kCurrencyType_RainbowCandle", 5},
    {"kCurrencyType_RainbowHeart", 6},
    {"kCurrencyType_DayTemple", 7},
    {"kCurrencyType_StormKey", 8},
    {"kCurrencyType_DawnHeart", 9},
    {"kCurrencyType_DayHeart", 10},
    {"kCurrencyType_RainHeart", 11},
    {"kCurrencyType_DuskHeart", 12},
    {"kCurrencyType_SunsetHeart", 13},
    {"kCurrencyType_NightHeart", 14},
    {"kCurrencyType_StormHeart", 15},
    {"kCurrencyType_SeasonCandle", 16},
    {"kCurrencyType_SeasonWax", 17},
    {"kCurrencyType_Prestige", 18},
    {"kCurrencyType_PrestigeWax", 19},
    {"kCurrencyType_SeasonPassToken", 20},
    {"kCurrencyType_HeartWax", 21},
    {"kCurrencyType_SeasonHeart", 22},
    {"kCurrencyType_EventCandle", 23},
};

static std::atomic<uint8_t> gPendingCurrencyType{0};
static int gCurrencyHookState = 0;

void OnAddCurrencyHandler(void*, DobbyRegisterContext* ctx) {
    uint8_t pending = gPendingCurrencyType.exchange(0, std::memory_order_acq_rel);
    if (pending) {
        auto* event = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(ctx->general.regs.x0));
        event[0x4C] = pending;
    }
}

bool InstallCurrencyHook() {
    if (gCurrencyHookState == 1) return true;
    if (gCurrencyHookState == -1) return false;
    uintptr_t base = GameBase();
    if (!base) return false;
    uint32_t code = 0;
    if (!ReadMemory(base + kAddCurrencyHandler, &code, 4) || code != kAddCurrencyFirstInstr) {
        Log("Currency: handler at 0x%X has unexpected code (%08X, want %08X)",
            (unsigned)kAddCurrencyHandler, code, kAddCurrencyFirstInstr);
        gCurrencyHookState = -1;
        return false;
    }
    if (DobbyInstrument(reinterpret_cast<void*>(base + kAddCurrencyHandler), OnAddCurrencyHandler) != 0) {
        Log("Currency: couldn't hook 0x%X", (unsigned)kAddCurrencyHandler);
        gCurrencyHookState = -1;
        return false;
    }
    Log("Currency: hooked AccountAddCurrency handler at 0x%X", (unsigned)kAddCurrencyHandler);
    gCurrencyHookState = 1;
    return true;
}

uint8_t CurrencyValue(const std::string& name) {
    for (const auto& e : kCurrencyMap)
        if (name == e.name) return e.value;
    return 0;
}

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
    bool required = false;    // not accepted: the event isn't started (a script error instead)
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
        "SetCameraShake", "AvatarGrindToPos", "AP27ShepherdStaff", "AP27Shield", "AP27Spear", "AP27ManateeFigurine", "EnablePlayerProp", "WearTeaSet", "TeaTableAction", "ProtoTrampoline", "PropSimpleAnim", "PlaceableAction", "PlacePlushieTossPlaceable", "PlacePuzzleKey", "PlacePlaceablePickup", "AwakenDarkCreatures", "DarkHornBlast", "ReleaseBirds", "Crab_Straighten", "BirdFlockOverrides", "Npc_SetReflected", "Npc_Reflect", "PlayMusic", "SetMusicKey", "PlaySound2D", "AudioSettingsMenu", "OpenAppleMusicMenu", "OpenNoteMenu", "OpenNextVisualNoteMenu", "RefreshNotesMenu", "ClearSharedSpaceShrineLocalNote", "TriggerToyEvent", "SocialToggleStarryUmbrella", "SocialStarryOutfitEvent", "SocialFireworks", "RiddleShrineNoteAnswerEffect", "LaunchFirework", "SetCloudsEnabled", "InfiniteScrollClouds", "CloudNoiseVelocity", "SetRainParams", "SetWindIntensity", "SetWind", "CollectWingBuffServerRequestEvent", "DepositWingBuffServerRequestEvent",
        "ConvertWingBuffsServerRequestEvent", "DropWingBuffServerRequestEvent",
        // 0.11.0: these stop themselves in their first update (their update calls the game's event stop, 0x171F82C)
        "AvatarSetOutfitDye", "AvatarSpeak", "AvatarChargeEnergy", "AvatarSetInnerGlow", "LocalDoAudienceReact",
        // 0.12.1: candle events, same (AvatarSetCandleState 0x11F5A60, SocialGift 0x1EB9EB4 end in 0x171F82C)
        "AvatarSetCandleState", "SocialGift",
        // 0.14.0: stops itself when its fireOnce (+0x26) is set (update 0x124BE88); only ever started with it
        "AvatarExitFlight",
        // 0.15.0 (Spawn tab): stops itself once every start position's star has landed (update 0x205DCDC, 0x205E5AC)
        "StarFallShower",
        // 0.16.0 (Reward animations): screen Fx that stop themselves when drawn (GetEmoteFx update 0x1F6CCA0 and
        // BreakEmoteFx 0x1F6367C end in 0x1720558; StarBlingFx 0x18FA6EC in 0x171F82C)
        "GetEmoteFx", "BreakEmoteFx", "StarBlingFx",
        // 0.16.2: account event that fires once (adds currency, then the game deletes it)
        "AccountAddCurrency",
        // 0.16.2: purchase animation, stops itself when drawn
        "PlayPurchaseFX",
    };
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
        else if (x.required)
            s += "error(\"" + std::string(type) + ": " + x.name + " wasn't taken; not started\") ";
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

bool SetFreeZoom(bool on);  // after SetCodePatch

// First-person view: a Dobby pre-call hook on the camera update (0x29213EC, x0 = camera) zeroes the camera distance
// (+0x5A8) while on. Placed the first time it's switched on, after checking the function's first word.
std::atomic<bool> gFpv{false};
bool gFpvHooked = false;
void FpvHook(void*, DobbyRegisterContext* ctx) {
    if (!gFpv.load(std::memory_order_relaxed)) return;
    uintptr_t cam = ctx->general.regs.x0;
    float d = 1.f;
    if (cam && ReadMemory(cam + 0x5A8, &d, 4) && std::isfinite(d)) *reinterpret_cast<float*>(cam + 0x5A8) = 0.f;
}
bool SetFirstPerson(bool on) {
    if (on && !gFpvHooked) {
        uint32_t first = 0;
        uintptr_t a = GameBase() + 0x29213EC;
        if (!GameBase() || !ReadMemory(a, &first, 4) || first != 0xD10343FFu) {
            Log("First-person view: the camera function isn't what 0.35.2 has (0x%08X)", first);
            return false;
        }
        if (DobbyInstrument(reinterpret_cast<void*>(a), FpvHook) != 0) {
            Log("First-person view: couldn't hook the camera function");
            return false;
        }
        gFpvHooked = true;
    }
    gFpv = on;
    Log("First-person view %s", on ? "on" : "off");
    return true;
}

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
    bool currencyServer = false;
    // currency
    std::vector<std::string> currencies;
    bool currenciesRead = false;
    int curType = 0;
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

// Auto walk (0.16.5): the game's AvatarGrindToPos walks you toward a target clump; the target marker is kept ahead
// of you in the chosen direction (moved every 0.25 s from your live position). SetAvatarVelocity was dropped: it
// writes velocityZ into the Y slot (0x123B538), so "forward" pushed you up. The grind event ends itself on arrival, so
// it's a one-shot (never kept or switched off): Off puts the target where you stand, so it arrives and ends.
double gWalkNext = 0, gWalkRestart = 0;
void WalkMarker(bool ahead) {
    float p[3];
    if (!LocalAvatarPos(p, nullptr)) return;
    float rad = M.walkDir * 3.14159265f / 180.f, d = ahead ? std::max(6.f, M.walkSpeed * 3.f) : 0.f;
    char b[200];
    snprintf(b, sizeof b, "OffsetLab.walkM:pos({%.3f, %.3f, %.3f})\n", p[0] + sinf(rad) * d, p[1], p[2] + cosf(rad) * d);
    Run("", std::string(kPrelude) +
                "if not OffsetLab.walkM then OffsetLab.walkM = game:markerBarn():CreateMarker(0)\n"
                "  local c = game:clumpBarn():CreateClumpWithSize(1); c:SetAt(0, OffsetLab.walkM); OffsetLab.walkC = c end\n" +
                b,
        "walkm");
}
void WalkStart() {
    StartEvent("", "", "AvatarGrindToPos",
               {{"target", "OffsetLab.walkC", ""}, {"speed", Num(std::max(0.5f, M.walkSpeed)), ""},
                {"comeToStop", "false", ""}, {"smoothTurning", "true", ""}, {"breakHandhold", "false", ""}});
}
void Walk() {
    gWalkNext = gWalkRestart = 0;
    WalkMarker(M.walk);
    if (M.walk) WalkStart(), Log("Auto walk %.1f at %.0f deg", M.walkSpeed, M.walkDir);
    else Log("Auto walk off");
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

void PlayerMoreMovement();
void PlayerPoseLook();
void PlayerConvenience();

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
    Dim("Walks you at a set speed in a fixed direction (degrees around the world, 0 = the level's forward), with the game's own walk-to-a-point (AvatarGrindToPos).");
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
    PlayerMoreMovement();

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

    PlayerPoseLook();
    PlayerConvenience();

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
    static bool freeZoom = false;
    if (Toggle("Unlimited zoom", &freeZoom,
               "Pinch the camera as far in or out as you like (the camera's zoom clamp store, 0x291F078, skipped; as "
               "in TSM). Off: the game's code put back."))
        if (!SetFreeZoom(freeZoom)) freeZoom = !freeZoom;
    static bool fpv = false;
    if (Toggle("First-person view", &fpv,
               "The camera sits in your avatar's head: its distance (+0x5A8) is set to 0 each time the game's camera "
               "update (0x29213EC) runs, as TSM does. Experimental. Off: normal camera."))
        if (!SetFirstPerson(fpv)) fpv = false;
}

// ---------------------------------------------------------------- Sky (0.17.0): environment, clouds, weather
// Environment: the game's own environment values (TheVoid's world.environment.* table, ported to 0.35.2; defaults read
// from the game file: most 1.0, sun height 45, moon phase 0.5, draw distance 15000, fog height 0.1, fog brightness
// 0.8). The override switch (0x30B00A8, TheVoid's world.environment.enable / TSM's allow override) is set while any
// is changed. Each value is checked to be a finite, sane float before writing; Reset writes the default back.
struct EnvVar { const char* label; uint32_t at; float lo, hi, def; bool needsOverride; };
const EnvVar kEnv[] = {
    {"Exposure", 0x2DFFBEC, 0.f, 4.f, 1.f, false},       {"Sun intensity", 0x2DFFBF0, 0.f, 4.f, 1.f, false},
    {"Sun height", 0x2DFFBF4, -90.f, 90.f, 45.f, false}, {"Sun sideways", 0x30B00B8, 0.f, 360.f, 0.f, true},
    {"Sun size", 0x2DFFBF8, 0.f, 5.f, 1.f, false},       {"Sun to moon", 0x30B00BC, 0.f, 1.f, 0.f, true},
    {"Eclipse", 0x30B00C0, 0.f, 1.f, 0.f, true},         {"Moon phase", 0x2DFFBFC, 0.f, 1.f, 0.5f, false},
    {"Draw distance", 0x2DFFBE8, 1000.f, 50000.f, 15000.f, false},
    {"Underwater bloom", 0x2DFFC00, 0.f, 4.f, 1.f, false}, {"Fog density", 0x2DFFBDC, 0.f, 4.f, 1.f, false},
    {"Fog height", 0x2DFFBE4, 0.f, 2.f, 0.1f, false},    {"Fog brightness", 0x2DFFBD4, 0.f, 2.f, 0.8f, false},
    {"Atmosphere density", 0x2DFFBE0, 0.f, 4.f, 1.f, false},
    {"Point light (unverified)", 0x2DFFC04, 0.f, 4.f, 1.f, false},
};
constexpr uint32_t kEnvOverride = 0x30B00A8;
float gEnv[IM_ARRAYSIZE(kEnv)];
bool gEnvRead = false;

bool EnvWrite(const EnvVar& e, float v) {
    float cur = 0;
    if (!GameBase() || !ReadMemory(GameBase() + e.at, &cur, 4) || !std::isfinite(cur) || std::fabs(cur) > 1e6f) {
        Log("%s: the value at 0x%X doesn't look right; not changed", e.label, e.at);
        return false;
    }
    if (e.needsOverride) SetSwitch("Environment override", kEnvOverride, true);
    bool ok = WriteGameMemory(e.at, &v, 4);
    Log("%s %.2f%s", e.label, v, ok ? "" : " (write failed)");
    return ok;
}

struct SkyState {
    bool clouds = true;
    float cloudScroll = 0.f;
    float rainRate = 4.f, rainIntensity = 1.f, rainOsc = 0.5f;
    float wind = 5.f;
    float push = 0.f, pushDir = 0.f;
} SK;

void SkySection() {
    if (!gEnvRead && GameBase()) {
        gEnvRead = true;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kEnv); i++) {
            float v = kEnv[i].def;
            if (ReadMemory(GameBase() + kEnv[i].at, &v, 4) && std::isfinite(v) && std::fabs(v) < 1e6f) gEnv[i] = v;
            else gEnv[i] = kEnv[i].def;
        }
    }
    Section("ENVIRONMENT", "The game's own sky and light values, on your screen only. Reset = the game's default.");
    for (int i = 0; i < (int)IM_ARRAYSIZE(kEnv); i++) {
        ImGui::TextUnformatted(kEnv[i].label);
        int r = Slider(kEnv[i].label, &gEnv[i], kEnv[i].lo, kEnv[i].hi, kEnv[i].def,
                       kEnv[i].hi > 1000 ? "%.0f" : "%.2f");
        if (r) EnvWrite(kEnv[i], gEnv[i]);
    }
    if (ImGui::Button("Reset all environment")) {
        for (int i = 0; i < (int)IM_ARRAYSIZE(kEnv); i++) gEnv[i] = kEnv[i].def, EnvWrite(kEnv[i], kEnv[i].def);
        SetSwitch("Environment override", kEnvOverride, false);
    }

    Section("CLOUDS", nullptr);
    if (Toggle("Clouds", &SK.clouds, "Show or hide the clouds (SetCloudsEnabled)."))
        StartEvent(SK.clouds ? "Clouds on" : "Clouds off", "", "SetCloudsEnabled",
                   {{"enableClouds", SK.clouds ? "true" : "false", ""}});
    ImGui::TextUnformatted("Cloud drift speed");
    if (int r = Slider("cloudscroll", &SK.cloudScroll, 0.f, 50.f, 0.f, "%.1f"))
        StartEvent("Cloud drift " + Num(SK.cloudScroll), "", "InfiniteScrollClouds",
                   {{"scrollSpeed", Num(r == 2 ? 0.f : SK.cloudScroll), ""}});
    if (ImGui::Button("Cloud texture swirl: fast")) StartEvent("Cloud swirl fast", "", "CloudNoiseVelocity",
                                                                {{"noiseScrollVelocity", "{60, 0, 0}", ""}});
    ImGui::SameLine();
    if (ImGui::Button("Normal##swirl")) StartEvent("Cloud swirl normal", "", "CloudNoiseVelocity",
                                                   {{"noiseScrollVelocity", "{10, 0, 0}", ""}});

    Section("WEATHER", nullptr);
    ImGui::TextUnformatted("Rain (fall rate / intensity / gusts)");
    bool rain = false;
    rain |= Slider("rainrate", &SK.rainRate, 0.f, 20.f, 4.f, "fall %.1f") != 0;
    rain |= Slider("rainint", &SK.rainIntensity, 0.f, 5.f, 1.f, "intensity %.2f") != 0;
    rain |= Slider("rainosc", &SK.rainOsc, 0.f, 2.f, 0.5f, "gusts %.2f") != 0;
    if (rain)
        StartEvent("Rain settings", "", "SetRainParams",
                   {{"fallRate", Num(SK.rainRate), ""}, {"intensity", Num(SK.rainIntensity), ""},
                    {"oscillationAmount", Num(SK.rainOsc), ""}, {"transitionDuration", "1.0", ""},
                    {"harmful", "false", ""}});
    Dim("Rain only shows where the level has rain; it never drains your light from here (harmful off).");
    ImGui::TextUnformatted("Wind strength");
    if (int r = Slider("windint", &SK.wind, 0.f, 30.f, 5.f, "%.1f"))
        StartEvent(r == 2 ? "Wind back to normal" : "Wind " + Num(SK.wind), "", "SetWindIntensity",
                   {{"intensity", Num(SK.wind), ""}, {"reset", r == 2 ? "true" : "false", ""}});
    ImGui::TextUnformatted("Wind push (force / direction)");
    bool push = Slider("windforce", &SK.push, 0.f, 50.f, 0.f, "force %.1f") != 0;
    push |= Slider("winddir", &SK.pushDir, 0.f, 360.f, 0.f, "%.0f deg") != 0;
    if (push) {
        float rad = SK.pushDir * 3.14159265f / 180.f;
        char v[96];
        snprintf(v, sizeof v, "{%.3f, 0, %.3f}", sinf(rad), cosf(rad));
        StartEvent("Wind push " + Num(SK.push), "", "SetWind", {{"windForce", Num(SK.push), ""}, {"windDir", v, ""}});
    }
}

// Effects & toys (0.18.0): the Weather, sky & visuals events that need no game objects. Wing/footstep effects and
// the snow props run while on (kept, switched off with SetEnabled(false)); the rest are one-shots.
struct VisToggle { const char* label; const char* type; bool on; };
VisToggle gVisToggles[] = {
    {"Almond blossom wings", "AlmondBlossomWingEffects", false}, {"Anniversary 2026 wings", "Anni2026WingEffects", false},
    {"Blue tear wings", "BlueTearWingEffects", false},           {"Bluebird wings", "BluebirdWingEffects", false},
    {"Maypole wings", "MaypoleWingEffects", false},             {"Flower footsteps", "FlowerFootstepEffects", false},
    {"Flower horn", "FlowerHornEffects", false},                 {"Snowboard", "Snowboard", false},
    {"Snowball mittens", "SnowBallMittens", false},
};
void EffectsSection() {
    Section("WING & FOOTSTEP EFFECTS, PROPS", "The game's own effect events, on your screen. Off switches them off.");
    for (auto& t : gVisToggles) {
        ImGui::PushID(t.type);
        if (ImGui::Checkbox(t.label, &t.on)) {
            if (t.on) StartEvent(std::string(t.label) + " on", std::string("vis:") + t.type, t.type, {});
            else StopEvent(std::string(t.label) + " off", std::string("vis:") + t.type);
        }
        ImGui::PopID();
    }
    Section("ONE-OFF EFFECTS", nullptr);
    struct Once { const char* label; const char* type; };
    static const Once kOnce[] = {{"Starry umbrella (toggle)", "SocialToggleStarryUmbrella"},
                                 {"Starry outfit", "SocialStarryOutfitEvent"},
                                 {"Fireworks", "SocialFireworks"},
                                 {"Riddle shrine answer", "RiddleShrineNoteAnswerEffect"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kOnce); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kOnce[i].label, ImVec2(Px(250), 0))) StartEvent(kOnce[i].label, "", kOnce[i].type, {});
    }
    if (ImGui::Button("Launch firework here", ImVec2(Px(250), 0))) {
        float p[3];
        if (!LocalAvatarPos(p)) {
            Log("Launch firework: your position couldn't be read");
        } else {
            char b[160];
            snprintf(b, sizeof b, "OffsetLab.fwM:pos({%.3f, %.3f, %.3f})\n", p[0], p[1] + 1.f, p[2]);
            Run("", std::string(kPrelude) + "OffsetLab.fwM = OffsetLab.fwM or game:markerBarn():CreateMarker(0)\n" + b,
                "fwm");
            StartEvent("Firework", "", "LaunchFirework", {{"sourceMarker", "OffsetLab.fwM", ""}});
        }
    }
    Dim("Not added from this page: footstep/trail events with hidden settings (use Items > Walk effects), firework "
        "bursts/rockets, credits, art book and mesh/water/ocean/spirit-shader events (they need level objects).");
}

// Music (0.20.0). Instrument switches: the game's own bytes (as TSM uses them; 0.35.2 addresses from the port).
// Music: PlayMusic (netSync off, so only you hear it), PlayMusic with no name stops; SetMusicKey; PlaySound2D;
// the game's audio menus. Song and sound ids are the game's own names (the Aurora songs as TSM lists them).
struct InstrSwitch { const char* label; uint32_t at; bool on; const char* help; };
InstrSwitch gInstr[] = {
    {"Reverb", 0x34574F0, false, "Adds reverb to instruments."},
    {"Auto-play sheets", 0x34B5890, false, "Instruments play the open sheet music by themselves."},
    {"Easy mode", 0x34B58A0, false, "The game's easy instrument mode."},
    {"Radial layout", 0x34B58BC, false, "The radial key layout instead of the grid."},
};
float gMusicVol = 1.f, gMusicKey = 0.f, gSoundVol = 1.f;
char gMusicId[64] = "", gSoundId[64] = "";

void PlaySong(const char* id) {
    StartEvent(std::string("Music ") + (*id ? id : "stop"), "", "PlayMusic",
               {{"music", Quote(id), ""}, {"volume", Num(gMusicVol), ""}, {"setVolume", "true", ""},
                {"foreground", "true", ""}, {"netSync", "false", ""}, {"continueIfSame", "false", ""}});
}

void MusicSection() {
    Section("INSTRUMENTS", "The game's instrument switches (as TSM has them). Off: back to normal.");
    for (auto& i : gInstr) {
        ImGui::PushID(i.label);
        if (Toggle(i.label, &i.on, i.help))
            if (!SetSwitch(i.label, i.at, i.on)) i.on = !i.on;
        ImGui::PopID();
    }

    Section("MUSIC PLAYER", "Plays the game's own music, only for you (not synced to others).");
    ImGui::TextUnformatted("Volume");
    Slider("musvol", &gMusicVol, 0.f, 1.f, 1.f, "%.2f");
    static const char* kSongs[][2] = {{"Star Calm", "starcalm"}, {"All is Soft Inside", "allissoftinside"},
                                      {"Exhale Inhale", "exhaleinhale"}, {"Queendom", "queendom"},
                                      {"Runaway", "runaway"}, {"The Seed", "theseed"},
                                      {"The Seed Concert", "theseedconcert"},
                                      {"Through the Eyes of a Child", "throughtheeyesofachild"},
                                      {"Warrior", "warrior"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kSongs); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kSongs[i][0], ImVec2(Px(250), 0))) PlaySong(kSongs[i][1]);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::InputTextWithHint("##mid", "another music id", gMusicId, sizeof gMusicId);
    ImGui::SameLine();
    if (ImGui::Button("Play##mid") && gMusicId[0]) PlaySong(gMusicId);
    ImGui::SameLine();
    if (ImGui::Button("Stop music")) PlaySong("");
    ImGui::TextUnformatted("Music key");
    if (int r = Slider("muskey", &gMusicKey, -12.f, 12.f, 0.f, "%.0f"))
        StartEvent("Music key " + Num(gMusicKey), "", "SetMusicKey",
                   {{"value", Num(r == 2 ? 0.f : gMusicKey), ""}, {"overrideFloat", "nil", ""}});

    Section("SOUNDS", nullptr);
    ImGui::TextUnformatted("Volume");
    Slider("sndvol", &gSoundVol, 0.f, 1.f, 1.f, "%.2f");
    static const char* kSounds[] = {"BellActivate", "BlessingGain", "AuroraOrbLight", "AvatarFlame",
                                    "AP24ClockChimeTune", "AP16ConcertCheerOneshot2D", "AlicePortalOpen",
                                    "AnniversaryCannon", "AP25DyeCollect", "BoomHit"};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kSounds); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kSounds[i], ImVec2(Px(250), 0)))
            StartEvent(std::string("Sound ") + kSounds[i], "", "PlaySound2D",
                       {{"sound", Quote(kSounds[i]), ""}, {"volume", Num(gSoundVol), ""}});
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::InputTextWithHint("##sid", "another sound name", gSoundId, sizeof gSoundId);
    ImGui::SameLine();
    if (ImGui::Button("Play##sid") && gSoundId[0])
        StartEvent(std::string("Sound ") + gSoundId, "", "PlaySound2D",
                   {{"sound", Quote(gSoundId), ""}, {"volume", Num(gSoundVol), ""}});

    Section("MENUS", nullptr);
    if (ImGui::Button("Audio settings", ImVec2(Px(250), 0))) StartEvent("Audio settings", "", "AudioSettingsMenu", {});
    ImGui::SameLine();
    if (ImGui::Button("Apple Music menu", ImVec2(Px(250), 0))) StartEvent("Apple Music menu", "", "OpenAppleMusicMenu", {});
    CustomSongsSection();
}

// 0.22.0: props, creature controls and reflection from the Spirits/NPCs and Props/toys pages.
std::vector<DefItem> gPlaceables;
bool gPlaceablesTried = false;
char gPlaceFilter[64] = "";
std::vector<int> gPlaceRows;
std::string gPlaceRowsFor = "\x01";

void PropsSection() {
    Section("PROPS", "Hold props and place placeables, the game's own events. Placeables are made with "
                     "preventNetworking, so only you see them.");
    struct Btn { const char* label; const char* type; };
    static const Btn kHold[] = {{"Shepherd staff", "AP27ShepherdStaff"}, {"Shield", "AP27Shield"},
                                {"Spear", "AP27Spear"},                  {"Manatee figurine", "AP27ManateeFigurine"},
                                {"Enable player prop", "EnablePlayerProp"}, {"Wear tea set", "WearTeaSet"},
                                {"Tea table action", "TeaTableAction"},     {"Trampoline", "ProtoTrampoline"},
                                {"Prop animation", "PropSimpleAnim"},       {"Placeable action", "PlaceableAction"},
                                {"Plushie toss game", "PlacePlushieTossPlaceable"}, {"Puzzle key", "PlacePuzzleKey"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kHold); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kHold[i].label, ImVec2(Px(250), 0))) StartEvent(kHold[i].label, "", kHold[i].type, {});
    }
    ImGui::TextUnformatted("Place a placeable (only you see it)");
    LoadDefs("Data/Resources/PlaceableDefs.json", "placeables", gPlaceables, gPlaceablesTried);
    if (gPlaceables.empty()) {
        Dim("The placeable list couldn't be read from the game yet.");
        if (ImGui::Button("Try again##pl")) gPlaceablesTried = false;
        return;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##plf", "search", gPlaceFilter, sizeof gPlaceFilter);
    std::string q = Lower(gPlaceFilter);
    if (q != gPlaceRowsFor) {
        gPlaceRowsFor = q;
        gPlaceRows.clear();
        for (int i = 0; i < (int)gPlaceables.size(); i++)
            if (q.empty() || Lower(gPlaceables[i].name).find(q) != std::string::npos) gPlaceRows.push_back(i);
    }
    if (ImGui::BeginChild("##placeables", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 8.5f), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)gPlaceRows.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const DefItem& d = gPlaceables[gPlaceRows[i]];
                if (IconRow(d.icon, d.name, d.name.c_str()))
                    StartEvent("Placeable " + d.name, "", "PlacePlaceablePickup",
                               {{"defName", Quote(d.name), ""}, {"preventNetworking", "true", ""},
                                {"useExactTransform", "false", ""}});
            }
    }
    TouchScroll();
    ImGui::EndChild();
}

struct CreatureState { bool krillAsleep = false; float spacing = 1.f, scale = 0.15f, anim = 1.f; } CR;

void CreatureControlsSection() {
    Section("CREATURE CONTROLS", "The game's own creature events for the level you're in.");
    if (Toggle("Krill asleep", &CR.krillAsleep, "Puts every dark creature in the level to sleep (AwakenDarkCreatures "
                                                "awake off); off wakes them again."))
        StartEvent(CR.krillAsleep ? "Krill asleep" : "Krill awake", "", "AwakenDarkCreatures",
                   {{"awake", CR.krillAsleep ? "false" : "true", ""}, {"fireOnce", "true", ""}});
    struct Btn { const char* label; const char* type; };
    static const Btn kBtn[] = {{"Dark horn blast", "DarkHornBlast"}, {"Release birds", "ReleaseBirds"},
                               {"Flip crabs upright", "Crab_Straighten"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kBtn); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kBtn[i].label, ImVec2(Px(250), 0))) StartEvent(kBtn[i].label, "", kBtn[i].type, {});
    }
    ImGui::TextUnformatted("Bird flocks: spacing / size / animation speed");
    bool f = Slider("bfsp", &CR.spacing, 0.f, 10.f, 1.f, "spacing %.2f") != 0;
    f |= Slider("bfsc", &CR.scale, 0.05f, 2.f, 0.15f, "size %.2f") != 0;
    f |= Slider("bfan", &CR.anim, 0.1f, 4.f, 1.f, "anim %.2f") != 0;
    if (f)
        StartEvent("Bird flock look", "", "BirdFlockOverrides",
                   {{"fishSchoolType", "\"kFishSchoolType_Bird\"", "2"}, {"minSpacing", "0", ""},
                    {"maxSpacing", Num(CR.spacing), ""}, {"minScale", Num(CR.scale), ""},
                    {"maxScale", Num(CR.scale), ""}, {"animationSpeedMultiplier", Num(CR.anim), ""}});
}

struct ReflectState { bool on = false; float height = 0.f; } RF;

void ReflectionSection() {
    Section("REFLECTION", "The game's mirror-floor reflection of your avatar (Npc_SetReflected / Npc_Reflect with "
                          "apply to local). Off: not reflected.");
    auto apply = [] {
        StartEvent(RF.on ? "Reflection on" : "Reflection off", "", "Npc_SetReflected",
                   {{"applyToLocal", "true", ""}, {"reflected", RF.on ? "true" : "false", ""}});
        if (RF.on)
            StartEvent("", "", "Npc_Reflect", {{"applyToLocal", "true", ""}, {"reflectionPlaneHeight", Num(RF.height * 2.f), ""}});
    };
    if (ImGui::Checkbox("Reflection", &RF.on)) apply();
    ImGui::TextUnformatted("Mirror height");
    if (Slider("rfh", &RF.height, -50.f, 50.f, 0.f, "%.1f") && RF.on) apply();
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

// Scooter mode (as TSM has it): the avatar move code (0x12343FC) loads a byte that, when set, zeroes the ground
// velocity before the position update; forcing it to 1 (mov w8,#1) gives TSM's "no movement animation" glide.
const CodePatch kScooterPatch = {"Scooter mode", 0x12343FC, 0x38686808u /* ldrb w8, [x0, x8] */,
                                 0x52800028u /* mov w8, #1 */};
// Unlimited zoom (as TSM has it): the whisker camera update stores its clamped zoom distance (str s0,[x19,#0x5b0] at
// 0x291F078); without that store the camera keeps whatever distance you pinch to.
const CodePatch kFreeZoomPatch = {"Unlimited zoom", 0x291F078, 0xBD05B260u /* str s0, [x19, #0x5b0] */, kNop};

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

bool SetFreeZoom(bool on) {
    bool ok = SetCodePatch(kFreeZoomPatch, on);
    if (ok) Log("Unlimited zoom %s", on ? "on" : "off");
    return ok;
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
    double candleRedAt = 0;  // Red candle: put away first, then taken out at this time
    char speak[200] = "";
} X;

// Runs one of the game's Lua functions (its debug/tweak commands, e.g. ReviveAvatar(game)), only if this build has it.
void GameFunction(const std::string& what, const char* fn, const std::string& key) {
    std::string f = fn;
    Run(what, "if type(" + f + ") ~= \"function\" then error(\"" + f + " isn't in this game build\") end\n" + f +
                  "(game)\n", key);
}

// Your avatar's candle: AvatarSetCandleState (state at +0x28: AsIs 0 / On 1 / Off 2), an event the generator's table
// misses (registered at 0x11F44F8, parent Event). Its update (0x11F5A60) takes the candle out for On (0x1876750 with
// "premium" off: kCandleMesh_Player), puts it away for Off (0x118DF50), then stops itself.
void CandleState(const std::string& what, const char* state, const char* number) {
    StartEvent(what, "", "AvatarSetCandleState", {{"state", Quote(state), number}}, true);
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

    Section("REWARD ANIMATIONS (visual only)",
            "The game's own reward flourishes, on your screen only. Nothing is given: these Fx draw and end by "
            "themselves; their code (0.35.2) sends no server request or Rpc and writes no currency or unlock.");
    {
        struct Fx { const char* label; const char* type; };
        static const Fx kFx[] = {{"New emote", "GetEmoteFx"}, {"Emote break", "BreakEmoteFx"},
                                 {"Winged light star bling", "StarBlingFx"}, {"Purchase", "PlayPurchaseFX"}};
        for (int i = 0; i < (int)IM_ARRAYSIZE(kFx); i++) {
            if (i % 2) ImGui::SameLine();
            if (ImGui::Button(kFx[i].label, ImVec2(Px(250), 0)))
                StartEvent(std::string(kFx[i].label) + " animation", "", kFx[i].type, {});
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("More effects (the game's own debug commands, as TSM uses them)");
    Dim("Nothing is added to your account. These may send the effect to players near you (the game's own Fx "
        "messages), so others can see them too.");
    {
        struct Cmd { const char* label; const char* lua; };
        static const Cmd kCmd[] = {
            {"Level up", "if type(LevelUp) ~= \"function\" then error(\"LevelUp isn't in this build\") end\nLevelUp(game)\n"},
            {"Cape upgrade", "if type(CapeUpgrade) ~= \"function\" then error(\"CapeUpgrade isn't in this build\") end\n"
                             "CapeUpgrade(game)\n"},
            {"Explosion", "if type(DEBUG_TestExplosionFx) ~= \"function\" then error(\"DEBUG_TestExplosionFx isn't in "
                          "this build\") end\nDEBUG_TestExplosionFx(game)\n"},
        };
        for (int i = 0; i < (int)IM_ARRAYSIZE(kCmd); i++) {
            if (i % 2) ImGui::SameLine();
            if (ImGui::Button(kCmd[i].label, ImVec2(Px(250), 0)))
                Run(std::string(kCmd[i].label) + " animation", kCmd[i].lua, std::string("fx:") + kCmd[i].label);
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Currency gain (experimental)");
    Dim("The game's AccountAddCurrency with the amount 10. It's an account event: it sends an \"add 10\" request to the "
        "server you're connected to and plays the gain animation for that currency.");
    ImGui::Checkbox("Server requests on (off at every start)##cur", &M.currencyServer);
    if (!M.currenciesRead && GameBase()) {
        M.currenciesRead = true;
        M.currencies.clear();
        for (const auto& e : kCurrencyMap) M.currencies.push_back(e.name);
    }
    if (!M.currencies.empty()) {
        if (M.curType >= (int)M.currencies.size()) M.curType = 0;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        if (ImGui::BeginCombo("##cur", M.currencies[M.curType].c_str())) {
            for (int i = 0; i < (int)M.currencies.size(); i++)
                if (ImGui::Selectable(M.currencies[i].c_str(), i == M.curType)) M.curType = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!M.currencyServer);
        if (ImGui::Button("Play##cur", ImVec2(-1, 0))) {
            uint8_t cv = CurrencyValue(M.currencies[M.curType]);
            if (cv && InstallCurrencyHook()) {
                gPendingCurrencyType.store(cv, std::memory_order_release);
                StartEvent(std::string("Currency +10 ") + M.currencies[M.curType], "", "AccountAddCurrency",
                           {{"value", "10", ""}, {"name", "\"\"", ""}});
            } else if (!cv) {
                Log("Currency: unknown type %s", M.currencies[M.curType].c_str());
            }
        }
        ImGui::EndDisabled();
    } else {
        Dim("Enter the world and open this again.");
        if (ImGui::Button("Try again##cur")) M.currenciesRead = false;
    }

    Section("PROPS", nullptr);
    NotPossible("Bucket brigade",
                "The bucket is a prop event (Bucket, an AvatarPropEvent) that the game starts from an equipped bucket "
                "prop; started from Lua there's no prop for it to act on, and your own prop couldn't be put back "
                "exactly when switched off.");
}

// ---------------------------------------------------------------- Social: players' light and the friendship tree
//
// Avatars (AvatarBarn at Game +0x318): avatar i is barn + 0x30 + i * 0x10CF0 (0x11AF570), i = 0..58 (the loop at
// 0x11FC40C stops at 0x3B). Per avatar: byte +0xB870 bit 0 = your own avatar (GetLocalAvatar 0x11B2BC4 checks it);
// halfword +0x10A0C = state bits: bit 0 = unlit (drawn as the grey ghost body "CharSkyKid_Body_Ghost", 0x120F7A0),
// bit 2 = lit before (sticky, 0x11A0464..0x11A0484), bit 3 = in the level (IsActive 0x119E948), bit 14 = lit by the
// game's light call, bit 15 = skipped by the game; byte +0x10A78 = the light was already sent (0x11A853C).
constexpr uintptr_t kGameAvatarBarnOff = 0x318;  // Game +0x318: AvatarBarn (as in teleport.cpp)
constexpr uintptr_t kBarnAvatars = 0x30;
constexpr uintptr_t kAvatarStride = 0x10CF0;
constexpr int kAvatarSlots = 0x3B;
constexpr uintptr_t kAvLocal = 0xB870;
constexpr uintptr_t kAvState = 0x10A0C;
constexpr uintptr_t kAvGone1 = 0x10A10, kAvGone2 = 0x10A12;  // IsActive: both 0
constexpr uintptr_t kAvLightSent = 0x10A78;
constexpr uint32_t kCharcoalSwitch = 0x312F4B4;  // the game's "unlit players" switch (read at 0x11A048C)
constexpr uintptr_t kGameEventBarn = 0x6B0;       // what the game passes to its light call (0x18703B8, 0x11FC458)
// The game's own "light this player" (0x11A84D4, avatar, event barn): clears bit 0, sets bit 14 and, the first time
// (+0x10A78), starts the game's networked light event for that player. Called by the candle when two flames touch
// (0x18703BC, after its distance checks) and by OrbitHandholdingScene for every unlit avatar in the level (0x11FC460).
constexpr uint32_t kLightAvatarFn = 0x11A84D4;
constexpr uint32_t kLightAvatarFirst[4] = {0xD10103FF, 0xA9017BFD, 0xF90013F5, 0xA9034FF4};

// Show others: the ghost fade (0x120F73C, per avatar) clamps its amount with fmov s2, #1.0 at 0x120FA00; with
// movi d2, #0 the amount is always 0, so hidden, blocked-looking and unlit players are drawn normally.
const CodePatch kRevealPatch = {"Show others", 0x120FA00, 0x1E2E1002u /* fmov s2, #1.0 */, 0x2F00E402u /* movi d2, #0 */};
// Force light (your screen): the unlit check reads the game's switch (ldrb w12, [x11, #0x4b4] at 0x11A048C); reading
// 0 instead (mov w12, wzr) means nobody is marked unlit on your device.
const CodePatch kNoCharcoalPatch = {"Force light", 0x11A048C, 0x3952D16Cu, 0x2A1F03ECu /* mov w12, wzr */};
// Unlight (your screen): the switch check (cbz w12 at 0x11A0498) becomes a NOP and "not lit before"
// (eor w8, w10, #1 at 0x11A04AC) becomes 1, so every other player in the level is marked unlit; your own avatar and
// the ones the game skips (local byte, bit 15) are still left out by the csel at 0x11A04B4.
const CodePatch kUnlightPatches[2] = {
    {"Unlight all", 0x11A0498, 0x3400010Cu /* cbz w12, #0x11a04b8 */, kNop},
    {"Unlight all", 0x11A04AC, 0x52000148u /* eor w8, w10, #1 */, 0x52800028u /* mov w8, #1 */},
};
// Friendship tree: the friendship level of a player (0x24A92F8, reached through Game +0x830 -> +0x10; 0x24A4FE8 is
// its wrapper) ends with cbz x0 / ldr w0, [x0, #4] (0x24A938C / 0x24A9390: the friend's level, 0 for strangers). The
// menu locks an ability when its level (+0x2FC) is above it ("trust_error_tier_locked", 0x1D775A0). Patched: the
// ldr becomes mov w0, #levels (the number of levels in the game's tier list) and the cbz a NOP. Never more than the
// list holds: 0x24AF7FC / 0x24AE580 abort the game for a level past the end.
constexpr uint32_t kTierAt[2] = {0x24A938C, 0x24A9390};
constexpr uint32_t kTierOriginal[2] = {0xB4000040u /* cbz x0, #0x24a9394 */, 0xB9400400u /* ldr w0, [x0, #4] */};
constexpr uintptr_t kGameRelationship = 0x830;

struct Social {
    bool reveal = false, forceLight = false, unlight = false, tree = false, strangers = false;
    bool lightReal = false;     // Force light: also the game's networked light, one player at a time
    double lightNext = 0;
    int lightSlot = 0;          // next avatar slot to look at
    int lit = 0;                // players lit this time
    int treeLevels = 0;         // the level written into the patch (0 = not patched)
    double treeCheck = 0;
} SO;

template <class V> bool Rd(uintptr_t a, V& v) { return ReadMemory(a, &v, sizeof v); }

// A pair of code patches as one: both or neither (the first put back when the second fails). Each word keeps the
// game safe on its own, in the order they're written.
bool SetPatchPair(const CodePatch* p, bool on) {
    const CodePatch& first = on ? p[0] : p[1];
    const CodePatch& second = on ? p[1] : p[0];
    if (!SetCodePatch(first, on)) return false;
    if (!SetCodePatch(second, on)) {
        SetCodePatch(first, !on);
        return false;
    }
    return true;
}

// Number of friendship levels in the game's tier list ([[[rel +8] +0x810] +0x241F0], a vector of pointers: 0x24C281C,
// loaded when +0x16C is set: 0xEDE040). 0 when it can't be read.
int TreeLevels() {
    uintptr_t game = GameObject(), r830 = 0, rel = 0, q = 0, obj = 0, begin = 0, end = 0;
    uint32_t loaded = 0;
    if (!game || !Rd(game + kGameRelationship, r830) || !r830 || !Rd(r830 + 0x10, rel) || !rel || !Rd(rel + 8, q) ||
        !q || !Rd(q + 0x810, obj) || !obj || !Rd(obj + 0x16C, loaded) || !loaded || !Rd(obj + 0x241F0, begin) ||
        !Rd(obj + 0x241F8, end) || !begin || end < begin || (end - begin) % 8)
        return 0;
    size_t n = (end - begin) / 8;
    return n >= 1 && n <= 64 ? (int)n : 0;
}

// levels > 0: patched to report that level; 0: the game's code back. The order keeps every moment safe: on, the
// ldr is replaced first (strangers still return 0 through the cbz); off, the cbz comes back first.
bool SetTree(int levels) {
    uint32_t cur[2] = {0, 0};
    if (!GameBase() || !ReadMemory(GameBase() + kTierAt[0], cur, sizeof cur)) return false;
    bool w0ok = cur[0] == kTierOriginal[0] || cur[0] == kNop;
    bool w1ok = cur[1] == kTierOriginal[1] || (cur[1] & 0xFFE0001F) == 0x52800000u;
    if (!w0ok || !w1ok) {
        Log("Friendship tree: the game code at 0x%X isn't what 0.35.2 has; not changed", kTierAt[0]);
        return false;
    }
    if (levels > 0) {
        uint32_t mov = 0x52800000u | ((uint32_t)levels << 5);  // mov w0, #levels
        if (!WriteGameMemory(kTierAt[1], &mov, 4) || !WriteGameMemory(kTierAt[0], &kNop, 4)) {
            WriteGameMemory(kTierAt[0], &kTierOriginal[0], 4);
            WriteGameMemory(kTierAt[1], &kTierOriginal[1], 4);
            return false;
        }
    } else {
        bool ok = WriteGameMemory(kTierAt[0], &kTierOriginal[0], 4);
        ok &= WriteGameMemory(kTierAt[1], &kTierOriginal[1], 4);
        return ok;
    }
    return true;
}

// Whether avatar slot i is another player the game would show unlit and hasn't lit yet (the conditions of the game's
// own loop at 0x11FC40C, plus "not lit before" and "light not sent yet"). Reads only.
bool LightCandidate(uintptr_t barn, int i, uintptr_t* avOut) {
    if (i < 0 || i >= kAvatarSlots) return false;
    uintptr_t av = barn + kBarnAvatars + (uintptr_t)i * kAvatarStride;
    uint8_t local = 1, gone1 = 1, gone2 = 1, sent = 1;
    uint16_t st = 0;
    if (!Rd(av + kAvLocal, local) || !Rd(av + kAvState, st) || !Rd(av + kAvGone1, gone1) || !Rd(av + kAvGone2, gone2) ||
        !Rd(av + kAvLightSent, sent))
        return false;
    if ((local & 1) || gone1 || gone2 || sent || (st & 0x8000) || !(st & 8) || (st & 4) || (st & 0x4000)) return false;
    if (avOut) *avOut = av;
    return true;
}

// Runs on the game's main thread (RunOnGameThread): checks everything again, then calls the game's light function
// exactly as its own callers do.
void LightOnGameThread(uintptr_t slot) {
    uintptr_t base = GameBase(), game = GameObject(), barn = 0, evb = 0, av = 0;
    uint32_t first[4] = {0, 0, 0, 0};
    uint8_t charcoal = 0;
    if (!base || !game || !Rd(game + kGameAvatarBarnOff, barn) || !barn || !Rd(game + kGameEventBarn, evb) || !evb)
        return;
    if (!ReadMemory(base + kLightAvatarFn, first, sizeof first) || memcmp(first, kLightAvatarFirst, sizeof first))
        return;
    if (!ReadMemory(base + kCharcoalSwitch, &charcoal, 1) || charcoal != 1) return;
    if (!LightCandidate(barn, (int)slot, &av)) return;
    using LightFn = void (*)(uintptr_t, uintptr_t);
    reinterpret_cast<LightFn>(base + kLightAvatarFn)(av, evb);
}

void SetReveal(bool on) {
    if (SetCodePatch(kRevealPatch, on)) Log("Show others %s", on ? "on" : "off");
    else SO.reveal = !on;
}

void SetForceLight(bool on) {
    if (SetCodePatch(kNoCharcoalPatch, on)) Log("Force light all %s", on ? "on" : "off");
    else SO.forceLight = !on;
    SO.lightNext = 0;
    SO.lit = 0;
    if (!SO.forceLight) SO.lightReal = false;
}

void SetUnlight(bool on) {
    if (SetPatchPair(kUnlightPatches, on)) Log("Unlight all %s", on ? "on" : "off");
    else SO.unlight = !on;
}

// Offer emotes to strangers. The player menu (RelationshipMenu) is filled with every ability of category "player"
// whatever the friendship (0x1D8F934), but its builder (0x1D97C88) asks the game's "is this player my friend in this
// session" (0x17A19E0: the uuid in Game +0x778's table; the same check the game's hand-hold lead uses, 0x11AF9D8)
// at 0x1D97D6C. Not a friend: abilities of trust tier 0 and 1 are left out of the menu (0x1D98C64) and the rest
// skip the friend-only checks. Patched: that call becomes mov w0, #1 (the call only reads; x0/x1 are loaded
// again right after it), so the menu is built as for a friend. Second word: the menu's node builder (0x1D941C8)
// checks that the base of an upgraded ability ("hug_2" needs "hug") was bought with that friend (0x1D767D0 ->
// 0x11859BC, the friend record; strangers have none); its tbz at 0x1D94810 becomes a NOP so that row isn't locked.
// The offer itself (the button's press, 0x1D78E14) needs no friend record: it calls the game's own offer
// (0x118DE84: your avatar, the ability, the target's avatar id at button +0x2DC), which is what other players see.
constexpr uint32_t kAllAbilitiesSwitch = 0x3455E25;  // byte, 0 normally; TSM's kEnableAllRelationshipAbilities
const CodePatch kStrangerPatches[1] = {
    {"Offer emotes to strangers", 0x1D97D6C, 0x97E8271Du /* bl 0x17a19e0 */, 0x52800020u /* mov w0, #1 */},
};
// (0.16.4: the second patch, 0x1D94810 "base ability bought with this friend", made every upgraded ability show
// next to its base: emotes appeared twice. Dropped; TSM doesn't patch it either.)

// Notes (0.19.0): the game's note menus (OpenNoteMenu, OpenNextVisualNoteMenu, RefreshNotesMenu,
// ClearSharedSpaceShrineLocalNote: events with no settings) and the message items through TriggerToyEvent
// (consumableName, the same path the message spells take). The *Note types on the Music & sound page are saved note
// records, not events, so they can't be fired.
void NotesSection() {
    Section("NOTES & MESSAGES", "The game's own note menus and message items. Notes you write are posted the "
                                "game's normal way.");
    struct Btn { const char* label; const char* type; };
    static const Btn kMenus[] = {{"Notes menu", "OpenNoteMenu"}, {"Next note", "OpenNextVisualNoteMenu"},
                                 {"Refresh notes", "RefreshNotesMenu"},
                                 {"Clear shrine note (local)", "ClearSharedSpaceShrineLocalNote"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kMenus); i++) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kMenus[i].label, ImVec2(Px(250), 0))) StartEvent(kMenus[i].label, "", kMenus[i].type, {});
    }
    ImGui::TextUnformatted("Place a message item");
    static const char* kItems[][2] = {{"Boat", "message_boat"},       {"Stone", "message_stone"},
                                      {"Lantern", "message_lantern"}, {"Crane", "message_crane"},
                                      {"Gondola", "message_gondola"}, {"Rose", "message_rose"},
                                      {"Treasure", "message_treasure"}};
    for (int i = 0; i < (int)IM_ARRAYSIZE(kItems); i++) {
        if (i % 3) ImGui::SameLine();
        if (ImGui::Button(kItems[i][0], ImVec2(Px(165), 0)))
            StartEvent(std::string("Message ") + kItems[i][0], "", "TriggerToyEvent",
                       {{"consumableName", Quote(kItems[i][1]), ""}, {"persistent", "false", ""}});
    }
    Dim("Uses TriggerToyEvent, the path the message spells take; whether the game also uses up a spell is up to the "
        "game.");
}

void SocialSection() {
    NotesSection();
    Section("SOCIAL", nullptr);
    ImGui::TextUnformatted("Speak to all");
    Dim("Says this over your avatar, like the developers' interview messages (AvatarSpeak). Experimental: who else "
        "sees it is up to the game and your server.");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Px(150));
    ImGui::InputTextWithHint("##speak", "message", X.speak, sizeof X.speak);
    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(Px(130), 0)) && X.speak[0])
        StartEvent(std::string("Said: ") + X.speak, "speak", "AvatarSpeak",
                   {{"message", Quote(X.speak), ""}, {"localizeMessage", "false", ""}});
    if (Toggle("Read all messages", &X.readAll,
               "Shows the text of chat from people you haven't befriended (and tables you're not at) instead of "
               "\"...\": the chat bubble's \"may show text\" check (0x1478CB0, 0x1478CE4) always says yes. Your screen "
               "only. Off: the game's code put back.")) {
        if (!SetPatchPair(kReadPatches, X.readAll)) X.readAll = !X.readAll;
        else Log("Read all messages %s", X.readAll ? "on" : "off");
    }

    Section("TAKE OUT CANDLE", "Holds your candle out, as if you held the candle button, to light candles and other players "
            "or to stand with a friend. Put away ends it (your normal candle button works as always).");
    float cw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    if (ImGui::Button("Red candle", ImVec2(cw, 0))) {
        CandleState("", "kAvatarCandleFlameState_Off", "2");  // a white one out stays white: away first
        X.candleRedAt = ImGui::GetTime() + 0.4;
    }
    ImGui::SameLine();
    if (ImGui::Button("White candle", ImVec2(cw, 0))) {
        X.candleRedAt = 0;
        StartEvent("White candle out", "", "SocialGift", {});
    }
    ImGui::SameLine();
    if (ImGui::Button("Put away", ImVec2(cw, 0))) {
        X.candleRedAt = 0;
        CandleState("Candle put away", "kAvatarCandleFlameState_Off", "2");
    }
    Dim("Red: your own candle, the one the candle button takes out (AvatarSetCandleState, state On; the game's "
        "player candle). White: the white candle the game shows when you offer or spend candles with a friend "
        "(SocialGift with no friend chosen: nothing is sent or spent; it needs at least one candle, with none the "
        "game may open its candle menu). Put away: AvatarSetCandleState, state Off. Experimental: what other "
        "players see is up to the game.");

    Section("PLAYERS", "On your screen unless a row says otherwise. Show others and Force light can be on together; "
            "Unlight all switches both off (they'd undo it).");
    if (Toggle("Show others client side", &SO.reveal,
               "Hidden, faded and unlit-looking players are drawn normally, for you only: the game's ghost fade "
               "(0x120FA00) always stays at 0. Off: the game's code put back.")) {
        if (SO.reveal && SO.unlight) SO.unlight = false, SetUnlight(false);
        SetReveal(SO.reveal);
    }
    if (Toggle("Force light all", &SO.forceLight,
               "Everyone in the level appears lit to you: the game's unlit check (0x11A048C) reads its switch as off. "
               "While on, the game's own candle-touch lighting doesn't start (nobody counts as unlit). Off: the game's "
               "code put back (unlit players look unlit again).")) {
        if (SO.forceLight && SO.unlight) SO.unlight = false, SetUnlight(false);
        SetForceLight(SO.forceLight);
    }
    ImGui::Indent(ImGui::GetFrameHeight() + 8);
    ImGui::BeginDisabled(!SO.forceLight || !M.server);
    ImGui::Checkbox("Also light them for real (others see it)", &SO.lightReal);
    ImGui::EndDisabled();
    Dim("Needs Force light all and the server switch (Transform > Winged light). Calls the game's own light-a-player "
        "function (0x11A84D4, the one your candle calls when flames touch and that a game scene calls for everyone "
        "in the level) for each player the game would show unlit, one every 0.5 s; the game sends each light once. "
        "Switching off stops it; a light already given stays (as in the game).%s",
        M.server ? "" : " The server switch is off.");
    if (SO.lightReal && SO.forceLight && M.server) Dim("Lights sent to the game this time: %d", SO.lit);
    ImGui::Unindent(ImGui::GetFrameHeight() + 8);
    if (Toggle("Unlight all", &SO.unlight,
               "Only on your screen: the game has no way to take light away from someone for others. Every other "
               "player in the level is marked unlit on your device (0x11A0498, 0x11A04AC). Off: the game's code put "
               "back.")) {
        if (SO.unlight) {
            if (SO.reveal) SO.reveal = false, SetReveal(false);
            if (SO.forceLight) SO.forceLight = false, SO.lightReal = false, SetForceLight(false);
        }
        SetUnlight(SO.unlight);
    }

    Section("OFFER EMOTES", nullptr);
    if (Toggle("Offer emotes to strangers", &SO.strangers,
               "Tap a player you haven't befriended and the game's player menu shows the friendship abilities (hold "
               "hands, high five, hug, carry...) as it does for a friend: the menu's \"is a friend\" check "
               "(0x1D97D6C) says yes. Use it "
               "with Unlock friendship tree below, or the abilities stay locked at friendship level 0. Off: the "
               "game's code put back (reopen the menu).")) {
        if (!SetCodePatch(kStrangerPatches[0], SO.strangers)) SO.strangers = !SO.strangers;
        else Log("Offer emotes to strangers %s", SO.strangers ? "on" : "off");
    }
    if (SO.strangers && !SO.tree) ImGui::TextColored(kAmber300, "Switch on Unlock friendship tree too.");
    Dim("The offer is the game's own: picking an ability calls the game's offer for the player you tapped (0x118DE84, "
        "your avatar + the ability + their avatar id), no friend record needed, nothing made up by the mod. Whether "
        "they can accept is up to their game and the server: the other player's game may not show or accept an "
        "offer from someone who isn't their friend (it refuses a hand-hold lead from a non-friend, 0x11AF9D8). The "
        "player must be lit (the menu only opens for players you've lit).");

    Section("FRIENDSHIP TREE", nullptr);
    if (Toggle("Unlock friendship tree (client side)", &SO.tree,
               "Every player counts as fully befriended on your device, so the player menu shows every ability "
               "unlocked: the game's friendship level (0x24A92F8) reports the top level of its own tier list. Your "
               "screen only: the other player's game and the server still decide. Off: the game's code put back.")) {
        if (SO.tree) {
            int n = TreeLevels();
            if (n <= 0) {
                Log("Friendship tree: the game's tier list isn't loaded (enter the world, open a friend's menu once)");
                SO.tree = false;
            } else if (SetTree(n)) {
                // as TSM: the game's "enable all relationship abilities" switch too, so abilities aren't
                // offered for candles first (its readers skip the purchase/gift checks)
                SetSwitch("All relationship abilities", kAllAbilitiesSwitch, true);
                SO.treeLevels = n;
                SO.treeCheck = ImGui::GetTime() + 2.0;
                Log("Friendship tree unlocked (%d levels)", n);
            } else {
                SO.tree = false;
            }
        } else {
            SetSwitch("All relationship abilities", kAllAbilitiesSwitch, false);
            if (SetTree(0)) SO.treeLevels = 0, Log("Friendship tree back to normal");
            else SO.tree = true;
        }
    }
    if (SO.tree) Dim("Reporting level %d.", SO.treeLevels);
    Dim("Also sets the game's \"enable all relationship abilities\" switch (0x3455E25, as TSM does), so abilities "
        "aren't asked for with candles first.");

    Section("OTHER", nullptr);
    if (Toggle("Audience mode (experimental)", &X.audience,
               "The audience reaction menu outside an event (AudienceSettings: show reaction menu, no reservation). "
               "Off: the game's normal audience settings back.")) ApplyAudience();
    if (Toggle("Unlock all (local only)", &X.unlockAll,
               "Everything the game checks with its \"has unlock\" (emotes, outfits, quests, spells...) counts as "
               "unlocked on your device (0xDFC144 always returns yes). Local only: the server still decides anything "
               "it checks (wearing, buying, saving). Off: the game's code put back; you may need a level change to "
               "see your real unlocks again.")) {
        if (!SetCodePatch(kUnlockPatch, X.unlockAll)) X.unlockAll = !X.unlockAll;
        else Log("Unlock all %s", X.unlockAll ? "on" : "off");
    }
}

// Social repeats: one networked light every 0.5 s while on; the friendship level kept within the tier list.
void SocialTick(double t) {
    if (SO.forceLight && SO.lightReal && M.server && t >= SO.lightNext) {
        SO.lightNext = t + 0.5;
        uintptr_t game = GameObject(), barn = 0;
        uint8_t charcoal = 0;
        if (game && Rd(game + kGameAvatarBarnOff, barn) && barn && ReadByte(kCharcoalSwitch, charcoal) && charcoal == 1)
            for (int k = 0; k < kAvatarSlots; k++) {
                int i = (SO.lightSlot + k) % kAvatarSlots;
                if (!LightCandidate(barn, i, nullptr)) continue;
                SO.lightSlot = (i + 1) % kAvatarSlots;
                if (RunOnGameThread(LightOnGameThread, (uintptr_t)i)) SO.lit++;
                break;
            }
    }
    if (SO.tree && t >= SO.treeCheck) {
        SO.treeCheck = t + 2.0;
        int n = TreeLevels();
        if (n <= 0) {  // list gone (level change, reload): the game's own code back
            if (SetTree(0)) SO.tree = false, SO.treeLevels = 0, Log("Friendship tree: tier list gone; back to normal");
        } else if (n != SO.treeLevels && SetTree(n)) {
            SO.treeLevels = n;
        }
    }
}

// ---------------------------------------------------------------- 0.14.0: more Player rows
//
// Events checked in the 0.35.2 code (update = slot 0x140 of the type's vtable):
// - AvatarExitFlight (update 0x124BE88): ends flight (game +0x300 -> +0xACA4 = 1, avatar state 2), then stops itself
//   when its fireOnce (+0x26) is set: started with fireOnce, never kept.
// - SetAvatarVelocity (0x123B4E4): X -> avatar +0x130, Y -> +0x134 and Z -> +0x134 as well (the game's Z writes the
//   Y slot), then stops itself when fireOnce (+0x37). So only X and Y pushes work: Dash is along the world X axis.
// - SetFlightParams (0x22BECC4), SetAvatarWindParams (0x22BA734), DisableJump (0x22BBC1C), DisableCliffJump
//   (0x22B972C), DisableWeakJumpDown (0x22C1FC0): set an "override this frame" flag + value for the player every
//   frame while running; switched off with the event.
// - DisableLocalPlayerBounce: no update of its own (the game checks whether one is running).
// - AvatarPoseForceWalkCycle (0x123AA18): forceWeak (+0x26) / forceNervous (+0x27) set the walk cycle every frame.
// - AvatarShrinkGrow (0x123AC90): every frame moves the current size/height (+0x34/+0x38) toward targetScale /
//   targetHeight at `speed`, on the same -3..3 scale as the outfit size/height values (0x3139710 / 0x313970C); -3,
//   the default, is the bottom of that range (smallest). Reset: grow back to your own values, then switch off.
// - SetAvatarGhost (0x1B65664), SetAvatarCapeTattered (0x1B7D3A4), HideAvatarFlightTrail (0x124A1CC): applied every
//   frame while running.
// - AvatarSetStance, AvatarSetInnerGlow: one-shots (already in OneShot()).
//
// Code and data:
// - Fast flap: the flap's busy time ([flap +0xD0], compared with the time since the flap at 0x22D4068 / 0x22D4B18:
//   no new flap before it) is worked out at 0x22D49F0 as fmadd s0, s0, s1, s2 (1.5 or 2.0 s, less 0.5 s with
//   speed). Patched: fmov s0, #0.5 (the shortest the game itself uses, 0x22D4988).
// - Anti-AFK: the AfkBarn's "idle longer than the limit" check (0xF8F330, used for "Start afk" / "Start full afk"
//   at 0x1182990 and by creatures) ends in cset w0, gt at 0xF8F374; patched: mov w0, wzr (never idle).
// - Fast home: the home hold (0x1EBE1BC) waits until its timer (+0x2E8) reaches 5.25 s (b.lt at 0x1EBE348) after
//   the first 0.25 s; patched: NOP, so it goes home after the first 0.25 s.
// - Fade all avatars: byte 0x312F490 (read at 0x11A8E08): when set, every avatar's fade target is 0 (ethereal),
//   on your screen.
const CodePatch kFastFlapPatch = {"Fast flap", 0x22D49F0, 0x1F010800u /* fmadd s0, s0, s1, s2 */,
                                  0x1E2C1000u /* fmov s0, #0.5 */};
const CodePatch kAntiAfkPatch = {"Anti-AFK", 0xF8F374, 0x1A9FD7E0u /* cset w0, gt */, 0x2A1F03E0u /* mov w0, wzr */};
const CodePatch kFastHomePatch = {"Fast home", 0x1EBE348, 0x54000CCBu /* b.lt #0x1ebe4e0 */, kNop};
constexpr uint32_t kFadeAllSwitch = 0x312F490;

struct PlayerMore {
    float dash = 15.f;
    float cruise = 2.f, lift = 1.f;
    bool cruiseOn = false, liftOn = false;
    bool noWind = false, noJump = false, noCliff = false, noBounce = false, noWeakDown = false;
    bool fastFlap = false;
    bool freeze = false;
    float freezeAt[3] = {0, 0, 0};
    double freezeNext = 0;
    bool weak = false, nervous = false;
    float size = 0.f, height = 0.f, growSpeed = 1.f;
    double growStopAt = 0;
    bool ghost = false, ghostNoLight = false, fadeAll = false;
    bool tattered = false;
    double tatteredStopAt = 0;
    float glow = 1.f;
    bool noTrail = false;
    bool antiAfk = false, fastHome = false;
} PM;

void FlightParams() {
    if (!PM.cruiseOn && !PM.liftOn) {
        StopEvent("Glide & lift back to normal", "flightparams");
        return;
    }
    StartEvent("Glide " + (PM.cruiseOn ? Num(PM.cruise) : std::string("normal")) + ", lift " +
                   (PM.liftOn ? Num(PM.lift) : std::string("normal")),
               "flightparams", "SetFlightParams",
               {{"cruiseAngleDegreesEnabled", PM.cruiseOn ? "true" : "false", ""},
                {"cruiseAngleDegrees", Num(PM.cruise), ""},
                {"liftMultEnabled", PM.liftOn ? "true" : "false", ""},
                {"liftMult", Num(PM.lift), ""}});
}

// A toggle that keeps one of the game's events running while on (no settings).
void EventToggle(const char* label, bool* on, const char* key, const char* type, const char* help) {
    if (Toggle(label, on, help)) {
        if (*on) StartEvent(std::string(label) + " on", key, type, {});
        else StopEvent(std::string(label) + " off", key);
    }
}

void WalkCycle() {
    if (!PM.weak && !PM.nervous) {
        StopEvent("Walk back to normal", "walkcycle");
        return;
    }
    StartEvent(PM.weak && PM.nervous ? "Weak + nervous walk" : PM.weak ? "Weak walk" : "Nervous walk", "walkcycle",
               "AvatarPoseForceWalkCycle",
               {{"forceWeak", PM.weak ? "true" : "false", ""}, {"forceNervous", PM.nervous ? "true" : "false", ""}});
}

void ShrinkGrow(float size, float height, const std::string& what) {
    PM.growStopAt = 0;
    StartEvent(what, "shrinkgrow", "AvatarShrinkGrow",
               {{"targetScale", Num(size), ""}, {"targetHeight", Num(height), ""}, {"speed", Num(PM.growSpeed), ""}});
}

void Ghost() {
    if (!PM.ghost) {
        StopEvent("Ghost look off", "ghost");
        return;
    }
    StartEvent("Ghost look on", "ghost", "SetAvatarGhost", {{"disableOmniLight", PM.ghostNoLight ? "true" : "false", ""}});
}

void Stance(const char* label, const char* state, int index, bool sit, bool sleep, bool stand) {
    StartEvent(std::string("Stance: ") + label, "", "AvatarSetStance",
               {{"avatarState", Quote(state), std::to_string(index)}, {"overwriteState", "true", ""},
                {"sit", sit ? "true" : "false", ""}, {"sleep", sleep ? "true" : "false", ""},
                {"stand", stand ? "true" : "false", ""}, {"instant", "true", ""}, {"fireOnce", "true", ""}});
}

void PatchToggle(const char* label, bool* on, const CodePatch& p, const char* help) {
    if (Toggle(label, on, help)) {
        if (!SetCodePatch(p, *on)) *on = !*on;
        else Log("%s %s", label, *on ? "on" : "off");
    }
}

void PlayerMoreMovement() {
    if (ImGui::Button("Land now", ImVec2(Px(260), 0)))
        StartEvent("Land now", "", "AvatarExitFlight", {{"fireOnce", "true", ""}});
    Dim("Ends flight where you are (AvatarExitFlight, once).");

    ImGui::TextUnformatted("Dash / launch");
    Slider("dash", &PM.dash, 1.f, 80.f, 15.f, "speed %.0f");
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    struct D { const char* label; float x, y; };
    const D dash[3] = {{"Launch up", 0, 1}, {"Dash +X", 1, 0}, {"Dash -X", -1, 0}};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(dash[i].label, ImVec2(bw, 0)))
            StartEvent(std::string(dash[i].label) + " " + Num(PM.dash), "", "SetAvatarVelocity",
                       {{"velocityX", Num(dash[i].x * PM.dash), ""}, {"velocityY", Num(dash[i].y * PM.dash), ""},
                        {"setVelocityX", dash[i].x != 0 ? "true" : "false", ""},
                        {"setVelocityY", dash[i].y != 0 ? "true" : "false", ""}, {"fireOnce", "true", ""}});
    }
    Dim("One push (SetAvatarVelocity, fireOnce). World axes, not your facing: the game's event writes its Z value "
        "into the up slot (0x123B538), so a push along Z or \"forward\" can't be made; X is east/west of the level.");

    ImGui::TextUnformatted("Glide & lift");
    Dim("SetFlightParams while on: glide angle in degrees (the game's cruise pitch; SetCruisePitch's default is 2) and "
        "lift multiplier (1 = normal). Reset: the game's own value.");
    if (ImGui::Checkbox("Glide angle", &PM.cruiseOn)) FlightParams();
    int r = Slider("cruise", &PM.cruise, -45.f, 45.f, 2.f, "%.0f deg");
    if (r == 2) PM.cruiseOn = false;
    if (r) FlightParams();
    if (ImGui::Checkbox("Lift", &PM.liftOn)) FlightParams();
    r = Slider("lift", &PM.lift, 0.f, 5.f, 1.f, "%.2fx");
    if (r == 2) PM.liftOn = false;
    if (r) FlightParams();

    if (Toggle("Ignore wind", &PM.noWind,
               "No wind pushing you while standing, no wind \"boom\" knock-back, no wind while recovering from a hit "
               "(SetAvatarWindParams while on). Off: normal wind.")) {
        if (PM.noWind)
            StartEvent("Ignore wind on", "wind", "SetAvatarWindParams",
                       {{"noStandingWind", "true", ""}, {"noStandingBoom", "true", ""}, {"noWindInHitRecover", "true", ""},
                        {"boomThreshEnabled", "true", ""}, {"boomThresh", "100000", ""}});
        else StopEvent("Ignore wind off", "wind");
    }
    EventToggle("No jump", &PM.noJump, "nojump", "DisableJump", "The jump button does nothing (DisableJump while on).");
    EventToggle("No cliff jump", &PM.noCliff, "nocliff", "DisableCliffJump",
                "No automatic jump off ledges (DisableCliffJump while on).");
    EventToggle("No weak jump down", &PM.noWeakDown, "noweakdown", "DisableWeakJumpDown",
                "No small hop down from low ledges (DisableWeakJumpDown while on).");
    EventToggle("No bounce", &PM.noBounce, "nobounce", "DisableLocalPlayerBounce",
                "No bouncing off bounce pads and springy things (DisableLocalPlayerBounce while on).");
    PatchToggle("Fast flap", &PM.fastFlap, kFastFlapPatch,
                "Flap again sooner: the time a flap keeps you busy (normally 1-2 s, 0x22D49F0) is 0.5 s, the "
                "game's own shortest. Each flap still costs energy. Off: the game's code put back.");
    static bool scooter = false;
    PatchToggle("Scooter mode", &scooter, kScooterPatch,
                "Glide along without the walking animation, as in TSM (0x12343FC forced to 1). Experimental. Off: the "
                "game's code put back.");
    if (Toggle("Freeze in place", &PM.freeze,
               "Holds you where you are now: the game's AvatarSetPos moves you back to that spot (momentum killed) "
               "5 times a second, the same move Teleport uses. It lets go by itself on a level change or teleport. Off: "
               "you're free again.")) {
        if (PM.freeze && !LocalAvatarPos(PM.freezeAt)) {
            Log("Freeze in place: your position can't be read right now (enter a level)");
            PM.freeze = false;
        } else {
            PM.freezeNext = 0;
            Log("Freeze in place %s", PM.freeze ? "on" : "off");
        }
    }
}

void PlayerPoseLook() {
    Section("POSE & LOOK", nullptr);
    ImGui::TextUnformatted("Stance");
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    struct St { const char* label; const char* state; int index; bool sit, sleep, stand; };
    const St st[6] = {{"Sit", "AvatarState_Sit", 2, true, false, false},
                      {"Squat", "AvatarState_Squat", 1, false, false, false},
                      {"Sleep", "AvatarState_Sleep", 3, false, true, false},
                      {"Idle sit", "AvatarState_IdleSit", 4, true, false, false},
                      {"Frozen", "AvatarState_Immobile", 5, false, false, false},
                      {"Default", "AvatarState_Default", 0, false, false, true}};
    for (int i = 0; i < 6; i++) {
        if (i % 3) ImGui::SameLine();
        if (ImGui::Button(st[i].label, ImVec2(bw, 0)))
            Stance(st[i].label, st[i].state, st[i].index, st[i].sit, st[i].sleep, st[i].stand);
    }
    Dim("AvatarSetStance, once (instant, overwriting the state). Default stands you up again; moving also ends most "
        "stances. Frozen is the game's immobile state: press Default to move again.");

    if (Toggle("Weak walk", &PM.weak, "The tired, weak walk (AvatarPoseForceWalkCycle forceWeak while on).")) WalkCycle();
    if (Toggle("Nervous walk", &PM.nervous, "The nervous walk (forceNervous while on).")) WalkCycle();

    ImGui::TextUnformatted("Smooth shrink / grow");
    Dim("Grows or shrinks you smoothly to this size and height (AvatarShrinkGrow, kept on). -3..3 is the game's own "
        "scale (the outfit size/height values; -3, the event's default, is the smallest). Reset: back to your own "
        "size and height, then switched off.");
    Slider("growspeed", &PM.growSpeed, 0.05f, 5.f, 1.f, "speed %.2f");
    int a = Slider("growsize", &PM.size, -3.f, 3.f, 0.f, "size %.2f");
    int b = Slider("growheight", &PM.height, -3.f, 3.f, 0.f, "height %.2f");
    if (a == 2 || b == 2) {
        float s = 0, h = 0;
        bool ok = GameBase() && ReadMemory(GameBase() + kAvatarScale, &s, 4) &&
                  ReadMemory(GameBase() + kAvatarHeight, &h, 4) && s >= -3.f && s <= 3.f && h >= -3.f && h <= 3.f;
        PM.size = ok ? s : 0.f, PM.height = ok ? h : 0.f;
        ShrinkGrow(PM.size, PM.height, "Shrink/grow: back to your size");
        PM.growStopAt = ImGui::GetTime() + 2.0 + 6.0 / std::max(0.05f, PM.growSpeed);
    } else if (a || b) {
        ShrinkGrow(PM.size, PM.height, "Shrink/grow: size " + Num(PM.size) + ", height " + Num(PM.height));
    }

    if (Toggle("Ghost look", &PM.ghost, "Your avatar drawn as a ghost (SetAvatarGhost while on).")) Ghost();
    ImGui::Indent(ImGui::GetFrameHeight() + 8);
    if (ImGui::Checkbox("Without its light", &PM.ghostNoLight) && PM.ghost) Ghost();
    ImGui::Unindent(ImGui::GetFrameHeight() + 8);
    if (Toggle("Fade all avatars", &PM.fadeAll,
               "Every avatar fades out to the ethereal look, on your screen: the game's switch at 0x312F490 (read at "
               "0x11A8E08). Off: the switch back to 0. (TSM's \"force ethereal\", 0x312F4A0, is the avatar drawing "
               "budget instead, 0x11A2C50: not used.)")) {
        if (!SetSwitch("Fade all avatars", kFadeAllSwitch, PM.fadeAll)) PM.fadeAll = !PM.fadeAll;
    }
    ImGui::BeginDisabled();
    bool no = false;
    ImGui::Checkbox("Charcoal look", &no);
    ImGui::EndDisabled();
    Dim("Not added: 0x312F4B4 (TSM's \"avatar charcoaling\") is the game's \"players can be unlit\" switch (read at "
        "0x11A048C, normally 1); it doesn't make you look charcoal. Force light / Unlight all (Social) use that code.");
    if (Toggle("Tattered cape", &PM.tattered, "Your cape looks torn (SetAvatarCapeTattered, amount 1, while on). Off: "
               "amount 0 for a moment, then switched off.")) {
        PM.tatteredStopAt = 0;
        StartEvent(PM.tattered ? "Tattered cape on" : "Tattered cape off", "tattered", "SetAvatarCapeTattered",
                   {{"amount", PM.tattered ? "1.0" : "0.0", ""}});
        if (!PM.tattered) PM.tatteredStopAt = ImGui::GetTime() + 1.0;
    }
    ImGui::TextUnformatted("Inner glow");
    Dim("Your avatar's inner glow (AvatarSetInnerGlow, once; it stays). Reset: glow 0.");
    if (int g = Slider("innerglow", &PM.glow, 0.f, 5.f, 0.f, "%.2f"))
        StartEvent(g == 2 ? "Inner glow off" : "Inner glow " + Num(PM.glow), "", "AvatarSetInnerGlow",
                   {{"amount", Num(PM.glow), ""}, {"stayActive", "true", ""}, {"fadeInDuration", "0.5", ""}});
    EventToggle("Hide flight trail", &PM.noTrail, "notrail", "HideAvatarFlightTrail",
                "No trail behind you when flying (HideAvatarFlightTrail while on).");
}

void PlayerConvenience() {
    Section("CONVENIENCE", nullptr);
    PatchToggle("Anti-AFK", &PM.antiAfk, kAntiAfkPatch,
                "Your game never decides you're idle: the AFK check (0xF8F330) always says no, so no AFK pose, no "
                "\"full AFK\". Your device only: the server may still drop an idle connection. Off: the game's code "
                "put back. (TheVoid's no_afk byte, 0x2DFECA0, isn't read by any code in 0.35.2: not used.)");
    PatchToggle("Fast home", &PM.fastHome, kFastHomePatch,
                "Holding the home button sends you home after 0.25 s instead of 5.25 s (0x1EBE348). Off: the game's "
                "code put back.");
}

void PlayerTick(double t) {
    if (PM.freeze && t >= PM.freezeNext) {
        PM.freezeNext = t + 0.2;
        float p[3];
        bool have = LocalAvatarPos(p);
        float dx = p[0] - PM.freezeAt[0], dy = p[1] - PM.freezeAt[1], dz = p[2] - PM.freezeAt[2];
        if (!have || dx * dx + dy * dy + dz * dz > 40.f * 40.f) {  // new level, teleport or no avatar: let go
            PM.freeze = false;
            Log("Freeze in place off (you moved to another place)");
        } else {
            HoldAvatarAt(PM.freezeAt);
        }
    }
    if (PM.growStopAt > 0 && t >= PM.growStopAt) {
        PM.growStopAt = 0;
        StopEvent("", "shrinkgrow");
    }
    if (PM.tatteredStopAt > 0 && t >= PM.tatteredStopAt) {
        PM.tatteredStopAt = 0;
        if (!PM.tattered) StopEvent("", "tattered");
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

// For the Spawn tab (spawn.cpp): the same event Lua the features use (the game's defaults first, then these values;
// kept as OffsetLab[key] unless a one-shot), the prelude it goes after, and the game's UI icons.
std::string FeatureEventLua(const std::string& key, const char* type,
                            const std::vector<std::pair<std::string, std::string>>& values,
                            const std::vector<std::string>& required) {
    std::vector<Set> sets;
    for (const auto& v : values) {
        bool req = false;
        for (const auto& r : required) req |= r == v.first;
        sets.push_back({v.first, v.second, "", req});
    }
    return EventLua(key, type, sets);
}
bool SetInvincibility(bool on) {
    if (X.invincible == on) {
        uint8_t cur = 0xFF;
        if (ReadByte(kInvincibleSwitch, cur) && cur == (on ? 1 : 0)) return true;
    }
    if (!SetSwitch("Invincibility", kInvincibleSwitch, on)) return false;
    X.invincible = on;
    return true;
}
bool InvincibilityOn() { return X.invincible; }
const char* FeatureLuaPrelude() { return kPrelude; }
bool GameIcon(const std::string& name, float size) { return DrawIcon(name, size); }

// Repeating actions; called every frame from Draw, whichever tab is open.
void FeaturesTick() {
    MusicTick();
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
    if (M.walk && t >= gWalkNext) {
        gWalkNext = t + 0.25;
        WalkMarker(true);
        if (t >= gWalkRestart) gWalkRestart = t + 2.0, WalkStart();  // in case the game stopped following
    }
    if (M.crawlStopAt > 0 && t >= M.crawlStopAt) {
        M.crawlStopAt = 0;
        if (!F.crawl) StopEvent("", "crawl"), StopEvent("", "stand");
    }
    if (X.audienceStopAt > 0 && t >= X.audienceStopAt) {
        X.audienceStopAt = 0;
        if (!X.audience && !X.broom) StopEvent("", "audience");
    }
    SocialTick(t);
    PlayerTick(t);
    if (X.candleRedAt > 0 && t >= X.candleRedAt) {
        X.candleRedAt = 0;
        CandleState("Red candle out", "kAvatarCandleFlameState_On", "1");
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
    static const char* kSubs[] = {"Player", "Energy", "Transform", "Outfits", "Items", "Emotes",
                                  "Camera", "World",  "Teleport",  "Social",  "Shout", "Visuals", "Spawn", "Music"};
    // buttons, not a tab bar: a second tab bar inside the Features tab didn't switch on the phone. The grid folds into
    // one header line (the current sub-tab and an arrow); picking a sub-tab folds it again. Saved in the prefs.
    bool collapsed = FeaturesTabsCollapsed();
    {
        char head[64];
        snprintf(head, sizeof head, "%s##subhead", kSubs[gSub]);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.60f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
        if (ImGui::Button(head, ImVec2(-1, Dp(44)))) {
            collapsed = !collapsed;
            SetFeaturesTabsCollapsed(collapsed);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        // the arrow at the button's right end: down = folded (tap to open), up = open
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        float h = (b.y - a.y) * 0.22f, cx = b.x - (b.y - a.y) * 0.5f, cy = (a.y + b.y) * 0.5f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        if (collapsed)
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(cx - h, cy - h * 0.6f), ImVec2(cx + h, cy - h * 0.6f),
                                                          ImVec2(cx, cy + h * 0.6f), col);
        else
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(cx - h, cy + h * 0.6f), ImVec2(cx, cy - h * 0.6f),
                                                          ImVec2(cx + h, cy + h * 0.6f), col);
    }
    if (!collapsed) {
        const float subW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kSubs); i++) {
            if (i % 5) ImGui::SameLine();
            bool sel = gSub == i;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.60f));
            if (ImGui::Button(kSubs[i], ImVec2(subW, Dp(44)))) {
                gSub = i;
                SetFeaturesTabsCollapsed(true);  // the space back once a sub-tab is picked
            }
            if (sel) ImGui::PopStyleColor();
        }
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##features", ImVec2(0, 0), ImGuiChildFlags_None)) {
        switch (gSub) {
            case 0: PlayerSection(inWorld); ReflectionSection(); break;
            case 1: EnergySection(); break;
            case 2: TransformSection(); ModesSection(); break;
            case 3: OutfitsSection(); break;
            case 4: ItemsSection(); PropsSection(); EffectsSection(); break;
            case 5: EmotesSection(); break;
            case 6: CameraSection(); break;
            case 7: WorldSection(); break;
            case 8: TeleportSection(); break;
            case 9: SocialSection(); break;
            case 10: ShoutSection(); break;
            case 11: SkySection(); break;
            case 12: SpawnTab(); CreatureControlsSection(); break;
            case 13: MusicSection(); break;  // the Spawn tab's content (spawn.cpp), here while the main tabs are hidden
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
