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

#include "imgui.h"

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
bool SafeToFire(const char* type) {
    IndexTypes();
    auto it = gTypeIndex.find(type);
    if (it == gTypeIndex.end()) {
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

const char* kPrelude =
    "OffsetLab = OffsetLab or {}\n"
    "local function stop(k) local e = OffsetLab[k]; OffsetLab[k] = nil\n"
    "  if e then if not pcall(function() e:Stop() end) then pcall(function() e:Terminate() end) end end end\n";

bool Run(const std::string& what, const std::string& script) {
    // what == "" for repeats (Scatter, repeated Shout): only problems are logged
    int r = RunLua(script);
    const char* w = what.empty() ? "Repeat" : what.c_str();
    if (r == -1) Log("%s: enter the world first", w);
    else if (r != 0) Log("%s: the game reported a script error (%d)", w, r);
    else if (!what.empty()) Log("%s", w);
    return r == 0;
}

// Starts event `type` with the settings, keeping it as OffsetLab[key] (an earlier one under the key is stopped).
bool StartEvent(const std::string& what, const char* key, const char* type, const std::vector<Set>& sets) {
    if (!SafeToFire(type)) return false;
    std::string s = kPrelude;
    s += "stop(\"" + std::string(key) + "\")\n";
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
    s += "e:Start()\nOffsetLab[\"" + std::string(key) + "\"] = e\n";
    s += "if #bad > 0 and print then pcall(print, \"Offset Lab: " + std::string(type) +
         " didn't take \" .. table.concat(bad, \", \")) end\n";
    return Run(what, s);
}

bool StopEvent(const std::string& what, const char* key) {
    return Run(what, std::string(kPrelude) + "stop(\"" + key + "\")\n");
}

std::string Quote(const std::string& s) {
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
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
    ImGui::SliderFloat("##s", v, lo, hi, fmt);
    int r = ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(130, 0))) {
        *v = normal;
        r = 2;
    }
    ImGui::PopID();
    return r;
}

int SliderInt(const char* label, int* v, int lo, int hi, int normal) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
    ImGui::SliderInt("##s", v, lo, hi);
    int r = ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(130, 0))) {
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
    {"peewee", true}, {"glow", false}, {"glow_rainbow", false}, {"glow_tgc", false}, {"hairtint", false},
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

void Hover() {
    if (F.hover <= 0.f) {
        StopEvent("Hover off", "hover");
        return;
    }
    StartEvent("Hover " + Num(F.hover), "hover", "AvatarAntiGravity",
               {{"upwardSpeed", Num(F.hover), ""}, {"gravityMult", "0", ""}, {"forceTakeoff", "true", ""}});
}

void SpellButton(const Spell& s) {
    std::string label = SpellLabel(s.id);
    if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
        StartEvent("Transform: " + label, "spell", "GrantSpell",
                   {{"buffName", Quote(s.id), ""}, {"duration", Num(F.spellSeconds), ""}, {"networked", "false", ""}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.id);
}

}  // namespace

void FeaturesTab() {
    bool inWorld = GameObject() != 0;
    if (!inWorld) ImGui::TextColored(kAmber300, "Enter the world to use these.");
    Dim("Everything here is local to your game. Sliders apply when you let go; Reset puts the game's normal "
        "behaviour back. Results show in the Log tab.");

    if (ImGui::BeginChild("##features", ImVec2(0, 0), ImGuiChildFlags_None)) {
        // ---- transform
        Section("TRANSFORM AVATAR", "Becomes a form for the time below (the game's spell effects, local only).");
        Slider("Duration", &F.spellSeconds, 5.f, 600.f, 60.f, "%.0f s");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputTextWithHint("##spf", "search", F.spellFilter, sizeof F.spellFilter);
        ImGui::SameLine();
        ImGui::Checkbox("Other spells too", &F.showAllSpells);
        if (ImGui::BeginChild("##spells", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 6.5f),
                              ImGuiChildFlags_Border)) {
            for (const auto& s : kSpells) {
                if (!s.form && !F.showAllSpells) continue;
                if (F.spellFilter[0] && !strstr(s.id, F.spellFilter)) continue;
                ImGui::PushID(s.id);
                SpellButton(s);
                ImGui::PopID();
            }
        }
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

        // ---- movement
        Section("MOVEMENT", nullptr);
        ImGui::TextUnformatted("Player hover");
        Dim("Lifts you off the ground and holds you up. The value is how fast you rise; set it low to hover "
            "gently, Reset to land.");
        if (Slider("hover", &F.hover, 0.f, 10.f, 0.f, "%.2f")) Hover();

        if (Toggle("Antigravity", &F.antigravity, "No gravity for your avatar."))
            StartEvent(F.antigravity ? "Antigravity on" : "Antigravity off", "gravity", "SetAvatarGravityMult",
                       {{"gravityMult", F.antigravity ? "0" : "1", ""}});

        ImGui::TextUnformatted("Speed up / down");
        Dim("Walking and running speed (1 = normal).");
        if (Slider("speed", &F.speed, 0.1f, 6.f, 1.f, "%.2fx"))
            StartEvent("Speed " + Num(F.speed), "speed", "SetAvatarRunSpeedMult", {{"runSpeed", Num(F.speed), ""}});

        if (Toggle("Scatter", &F.scatter, "Keeps letting go of hand-holds and carries (yours and others' on you).") &&
            !F.scatter)
            StopEvent("Scatter off", "scatter");
        if (F.scatter && inWorld && ImGui::GetTime() >= F.scatterNext) {
            F.scatterNext = ImGui::GetTime() + 0.5;
            StartEvent("", "scatter", "AvatarBreakHandHold",
                       {{"breakFromLeader", "true", ""}, {"breakFromFollowers", "true", ""},
                        {"breakCarryChain", "true", ""}});
        }

        if (Toggle("Crawl mode", &F.crawl, "Your avatar crawls.")) {
            if (F.crawl) StartEvent("Crawl on", "crawl", "AvatarCrawl", {{"crawlAmount", "1.0", ""}});
            else StopEvent("Crawl off", "crawl");
        }
        if (Toggle("Force flight", &F.flight, "Keeps you in flight mode while on.")) {
            if (F.flight) StartEvent("Force flight on", "flight", "AvatarForceEnableFlightMode", {{"fireOnce", "false", ""}});
            else StopEvent("Force flight off", "flight");
        }
        if (Toggle("Walk under water", &F.walkUnderWater, "Walk along the bottom instead of swimming.")) {
            if (F.walkUnderWater) StartEvent("Walk under water on", "water", "WalkUnderWater", {});
            else StopEvent("Walk under water off", "water");
        }
        if (Toggle("Kill avatar", &F.kill, "Your avatar dies (the game's own death), and keeps dying while on.")) {
            if (F.kill) StartEvent("Kill avatar on", "kill", "KillLocalAvatar", {});
            else StopEvent("Kill avatar off", "kill");
        }

        // ---- winged light
        Section("WINGED LIGHT", nullptr);
        ImGui::TextUnformatted("Temporary (this game session only)");
        Dim("Shows your cape at this wing level. Switch off or Reset to get your real level back.");
        bool changed = ImGui::Checkbox("Use this level", &F.capeOn);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
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
        ImGui::TextUnformatted("Permanent");
        ImGui::TextColored(kRed300, "Not available.");
        Dim("Your real winged light count is kept by the game's servers; changing it would mean changing your "
            "account, which Offset Lab doesn't do.");

        // ---- world
        Section("WORLD & COLLISION", nullptr);
        if (Toggle("Disable terrain", &F.terrainOff,
                   "Turns the ground and its collision off, so you fall or pass through barriers. (TheVoid's "
                   "world.hide_terrain switch.)"))
            if (!SetSwitch("Disable terrain", kTerrainSwitch, F.terrainOff)) F.terrainOff = !F.terrainOff;
        if (Toggle("Disable reset barrier", &F.barrierOff,
                   "Experimental: sets the level's boundary plane to none (AvatarSetBoundaryPlane left empty), so "
                   "going past it shouldn't reset you. Switch off to give the level its plane back.")) {
            if (F.barrierOff) StartEvent("Reset barrier off", "barrier", "AvatarSetBoundaryPlane", {});
            else StopEvent("Reset barrier back", "barrier");
        }
        if (Toggle("Disable shadow", &F.shadowOff, "Removes your avatar's shadow.")) {
            if (F.shadowOff) StartEvent("Shadow off", "shadow", "AvatarDisableShadow", {});
            else StopEvent("Shadow back", "shadow");
        }
        ImGui::TextUnformatted("Surface friction");
        Dim("How much grip the ground has: low = slippery like ice (the game's own slippery value is 0.2). "
            "Reset = the game's normal ground.");
        r = Slider("friction", &F.friction, 0.f, 2.f, 1.f, "%.2f");
        if (r == 1) F.frictionOn = true, ApplyFriction(true, F.friction);
        if (r == 2) F.frictionOn = false, ApplyFriction(false, 0);

        // ---- shout
        Section("SHOUT", "Your call: colour, strength, voice; Shout once or keep repeating.");
        if (!F.voicesRead && GameBase()) {
            F.voicesRead = true;
            F.voices = EnumOptions("OnShout", "voiceType");
            if (F.voices.empty()) F.voices = EnumOptions("AvatarOutfitRegion", "voice");
        }
        ImGui::ColorEdit3("Colour", F.shoutColor);
        ImGui::SameLine();
        if (ImGui::Button("Reset##col")) F.shoutColor[0] = F.shoutColor[1] = F.shoutColor[2] = 1.f;
        ImGui::TextUnformatted("Intensity");
        Slider("intensity", &F.shoutIntensity, 0.f, 5.f, 1.f, "%.2f");
        if (!F.voices.empty()) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
            std::string cur = F.voices[F.voice];
            if (ImGui::BeginCombo("##voice", cur.c_str())) {
                for (int i = 0; i < (int)F.voices.size(); i++)
                    if (ImGui::Selectable(F.voices[i].c_str(), i == F.voice)) F.voice = i, F.voiceOn = i > 0, ApplyVoice();
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset##voice", ImVec2(130, 0))) F.voice = 0, F.voiceOn = false, ApplyVoice();
            Dim("Voice type (temporary; Reset gives you your own voice back).");
        } else {
            Dim("Voice types: not readable from the game yet.");
        }
        Dim("Pitch: the game has no pitch setting for shouts in 0.35.2, so it can't be changed here.");
        if (ImGui::Button("Shout", ImVec2(200, 0))) Shout();
        ImGui::SameLine();
        ImGui::Checkbox("Repeat", &F.shoutRepeat);
        ImGui::TextUnformatted("Repeat speed (shouts per second)");
        Slider("rate", &F.shoutPerSecond, 0.2f, 10.f, 1.f, "%.1f / s");
        if (F.shoutRepeat && inWorld && ImGui::GetTime() >= F.shoutNext) {
            F.shoutNext = ImGui::GetTime() + 1.0 / std::max(0.2f, F.shoutPerSecond);
            Shout(true);
        }
        ImGui::Spacing();
        Dim("More options will come here.");
    }
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
