// Offset Lab window, in the Sky Mesh Lab theme (near-black #0b0d12, white text at several strengths, thin white
// borders, sky blue accents).
#include "lab.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // ClearActiveID, for drag-to-scroll

namespace lab {

namespace {

// ---------------------------------------------------------------- theme

const ImVec4 kSky400(0.220f, 0.741f, 0.973f, 1.f);  // #38bdf8
const ImVec4 kSky300(0.490f, 0.827f, 0.988f, 1.f);  // #7dd3fc
const ImVec4 kRed300(0.988f, 0.647f, 0.647f, 1.f);  // #fca5a5
const ImVec4 kAmber300(0.988f, 0.827f, 0.302f, 1.f);
inline ImVec4 White(float a) { return ImVec4(1.f, 1.f, 1.f, a); }
inline ImVec4 Sky(float a) { return ImVec4(kSky400.x, kSky400.y, kSky400.z, a); }

// Sizes follow TSM's layout, in dp: dp = 1.6667 px x the panel scale (TSM's auto-scale: the 430x545 dp layout fitted
// into 75% x 80% of the screen, at most 1). Rows at least 36 dp high, 44 dp sub-tab buttons, 20 px scrollbar.
float gDp = 1.6667f;   // px per dp right now
float gTextPx = 24.f;  // text height in px right now
float gScrollbarPx = 40.f;
float DP(float v) { return v * gDp; }

void ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;  // TSM's panel: square, docked to the edge
    s.ChildRounding = DP(12);
    s.FrameRounding = DP(6);
    s.PopupRounding = DP(8);
    s.GrabRounding = DP(6);
    s.TabRounding = DP(6);
    s.ScrollbarRounding = DP(6);
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 1.f;
    s.ItemSpacing = ImVec2(DP(8), DP(8));
    s.ItemInnerSpacing = ImVec2(DP(8), DP(6));
    s.IndentSpacing = DP(14);
    s.FramePadding = ImVec2(DP(12), std::max(DP(6), (DP(36) - gTextPx) * 0.5f));  // 36 dp rows
    s.WindowPadding = ImVec2(DP(12), DP(12));
    s.TouchExtraPadding = ImVec2(DP(2), DP(2));
    s.ScrollbarSize = gScrollbarPx;  // Scrollbar slider (default 40 px: TSM's widest)
    s.GrabMinSize = DP(20);
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.043f, 0.051f, 0.071f, 1.00f);  // #0b0d12, solid so the game doesn't show through
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = White(0.035f);
    c[ImGuiCol_PopupBg] = ImVec4(0.067f, 0.078f, 0.102f, 0.98f);
    c[ImGuiCol_Text] = White(0.97f);
    c[ImGuiCol_TextDisabled] = White(0.62f);
    c[ImGuiCol_Border] = White(0.18f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = White(0.09f);
    c[ImGuiCol_FrameBgHovered] = White(0.13f);
    c[ImGuiCol_FrameBgActive] = Sky(0.25f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.067f, 0.082f, 0.114f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = White(0.03f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = White(0.15f);
    c[ImGuiCol_ScrollbarGrabHovered] = White(0.25f);
    c[ImGuiCol_ScrollbarGrabActive] = Sky(0.60f);
    c[ImGuiCol_CheckMark] = kSky400;
    c[ImGuiCol_SliderGrab] = kSky400;
    c[ImGuiCol_SliderGrabActive] = kSky300;
    c[ImGuiCol_Button] = White(0.11f);
    c[ImGuiCol_ButtonHovered] = Sky(0.30f);
    c[ImGuiCol_ButtonActive] = Sky(0.50f);
    c[ImGuiCol_Header] = Sky(0.28f);
    c[ImGuiCol_HeaderHovered] = Sky(0.34f);
    c[ImGuiCol_HeaderActive] = Sky(0.45f);
    c[ImGuiCol_Separator] = White(0.16f);
    c[ImGuiCol_SeparatorHovered] = Sky(0.40f);
    c[ImGuiCol_SeparatorActive] = kSky400;
    c[ImGuiCol_ResizeGrip] = White(0.08f);
    c[ImGuiCol_ResizeGripHovered] = Sky(0.40f);
    c[ImGuiCol_ResizeGripActive] = kSky400;
    c[ImGuiCol_Tab] = White(0.08f);
    c[ImGuiCol_TabHovered] = Sky(0.35f);
    c[ImGuiCol_TabActive] = Sky(0.45f);
    c[ImGuiCol_TabUnfocused] = White(0.04f);
    c[ImGuiCol_TabUnfocusedActive] = Sky(0.18f);
    c[ImGuiCol_TableHeaderBg] = White(0.05f);
    c[ImGuiCol_TableBorderStrong] = White(0.12f);
    c[ImGuiCol_TableBorderLight] = White(0.06f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = White(0.025f);
    c[ImGuiCol_TextSelectedBg] = Sky(0.35f);
    c[ImGuiCol_NavHighlight] = kSky400;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

void Eyebrow(const char* text) { ImGui::TextColored(kSky300, "%s", text); }
void Dim(const char* fmt, ...) IM_FMTARGS(1);
void Dim(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, White(0.70f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, Sky(0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSky300);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.659f, 0.890f, 0.996f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.059f, 0.090f, 0.165f, 1.f));  // slate-900 on sky
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}
std::string Hex(uintptr_t v) {
    char b[32];
    snprintf(b, sizeof b, "0x%" PRIXPTR, v);
    return b;
}
bool ParseHex(const char* s, uintptr_t& out) {
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char* end = nullptr;
    unsigned long long v = strtoull(s, &end, 16);
    if (end == s) return false;
    out = static_cast<uintptr_t>(v);
    return true;
}
bool Contains(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    auto it = std::search(hay.begin(), hay.end(), needle, needle + strlen(needle),
                          [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != hay.end();
}

// "AvatarSetInnerGlow" -> "Avatar set inner glow"; "fadeInDuration" -> "Fade in duration"; keeps acronyms (UI, NPC)
std::string Friendly(const std::string& name) {
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '_') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        bool upper = std::isupper((unsigned char)c), digit = std::isdigit((unsigned char)c);
        char prev = i ? name[i - 1] : 0, next = i + 1 < name.size() ? name[i + 1] : 0;
        bool startWord = i && ((upper && (std::islower((unsigned char)prev) || std::isdigit((unsigned char)prev) ||
                                          (std::isupper((unsigned char)prev) && std::islower((unsigned char)next)))) ||
                               (digit && std::isalpha((unsigned char)prev)));
        if (startWord && !out.empty() && out.back() != ' ') out += ' ';
        out += c;
    }
    // lower-case whole words after the first, unless the word is all caps (an acronym)
    std::string res;
    size_t w = 0;
    for (size_t i = 0; i <= out.size(); i++) {
        if (i == out.size() || out[i] == ' ') {
            std::string word = out.substr(w, i - w);
            bool acronym = word.size() > 1 && std::all_of(word.begin(), word.end(), [](char ch) {
                return std::isupper((unsigned char)ch) || std::isdigit((unsigned char)ch); });
            if (!acronym) {
                for (auto& ch : word) ch = (char)std::tolower((unsigned char)ch);
                if (res.empty() && !word.empty()) word[0] = (char)std::toupper((unsigned char)word[0]);
            }
            if (!word.empty()) res += (res.empty() ? "" : " ") + word;
            w = i + 1;
        }
    }
    return res;
}
std::string Unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// ---------------------------------------------------------------- shared state

// The value of one setting in the controls (filled from the game's default).
struct Value {
    bool changed = false;  // only changed settings are sent
    bool b = false;
    float f[4] = {0, 0, 0, 1};
    int i = 0;
    int option = 0;
    std::string text;
};

struct State {
    bool namesLoaded = false;
    std::vector<std::string> names, titles;  // read from the game; readable titles
    int verified = 0, unreadable = 0;
    int area = -1;
    char search[96] = "";
    int selected = -1;
    std::vector<int> history;  // selected types, for Back / Forward
    int historyPos = -1;
    std::vector<int> recent;
    std::map<int, std::vector<Value>> values;
    std::map<int, std::vector<std::string>> options;  // enum option names, read from the game
    bool serverTypes = false;                       // off at every start
    bool fireOnChange = false;
    bool showOffsets = false;
    std::string lastResult;
    bool lastOk = true;
    // memory
    char memAddr[32] = "0";
    bool memAbsolute = false;
    int memRows = 16;
    // lua
    char lua[4096] = "-- Lua runs inside the game. Example:\n"
                     "local e = game:eventBarn():AddEventByMetaName(\"SetBulletTime\")\n"
                     "e:speed(0.3)\ne:duration(3.0)\ne:Start()\n";
    std::vector<std::string> luaHistory;
    // native functions
    char fnSearch[96] = "";
    char manualAddr[32] = "";
    std::map<uintptr_t, std::pair<uint64_t, double>> rate;
    double lastSample = 0;
    int tab = 0;
    int openTab = -1;
    // view & crash notes (saved next to the mod)
    bool prefsLoaded = false;
    float uiScale = 1.0f;     // the panel's size on top of the screen's (Size slider)
    float panelWidthDp = 500.f; // TSM's default panel width
    bool menuOpen = true;
    float scrollbarPx = 40.f;  // scrollbar width
    float handleY = 0.08f;     // the Lab handle: where along the edge (0 top - 1 bottom)
    bool dockRight = true;     // which side the handle and the panel are on
    bool featuresTabsCollapsed = false;  // Features: sub-tab buttons folded into one header line
    bool handleDragging = false;
    std::string lastFired;       // from the previous session: last function fired before the game closed
    bool lastFiredShown = false;
    std::vector<std::string> crashed;  // functions marked as crashing the game
    std::string pendingRisky;          // function waiting for "Fire anyway"
    std::vector<int8_t> evStatus;      // EventStatus per type
    bool eventsOnly = true;
    // recorder
    struct RecRow {
        uint64_t serial;
        double t, last;     // first and latest start (RecorderNow clock)
        uint32_t id;        // meta class id
        int type;           // -1 = unknown
        uint32_t count;     // consecutive starts with the same values
        std::string raw;    // the recorded bytes (RecordedValues decodes them)
    };
    struct RecStat { uint32_t n = 0; double first = 0, last = 0; };
    std::deque<RecRow> rec;
    std::vector<RecStat> recStats;   // per type
    std::vector<int> recShown;       // indexes into rec after filtering
    std::vector<int8_t> recMatch;    // per type: matches the filter
    std::string recFilterUsed = "\x01";
    bool recDirty = true;
    bool recHideNoisy = true;
    bool recWithValues = true;
    char recFilter[96] = "";
    double recStart = 0;
    uint64_t recSerial = 0, recSelected = 0;
    uint64_t recTotal = 0;
    std::string recMsg;
    bool recMsgOk = true;
} S;

bool IsCrashed(const std::string& name) {
    return std::find(S.crashed.begin(), S.crashed.end(), name) != S.crashed.end();
}

void SavePrefs() {
    std::string t = "uiScale=" + std::to_string(S.uiScale) + "\npanelWidthDp=" + std::to_string(S.panelWidthDp) +
                    "\nmenuOpen=" + std::string(S.menuOpen ? "1" : "0") + "\nscrollbarPx=" +
                    std::to_string(S.scrollbarPx) + "\nhandleY=" + std::to_string(S.handleY) +
                    "\ndockRight=" + std::string(S.dockRight ? "1" : "0") +
                    "\nfeaturesTabsCollapsed=" + std::string(S.featuresTabsCollapsed ? "1" : "0") + "\n";
    for (auto& c : S.crashed) t += "crashed=" + c + "\n";
    WriteTextFile(DataPath("offsetlab_settings.txt"), t);
}

void LoadPrefs() {
    if (S.prefsLoaded) return;
    S.prefsLoaded = true;
    std::string t = ReadTextFile(DataPath("offsetlab_settings.txt"));
    size_t a = 0;
    while (a < t.size()) {
        size_t b = t.find('\n', a);
        if (b == std::string::npos) b = t.size();
        std::string line = t.substr(a, b - a);
        if (line.rfind("uiScale=", 0) == 0) S.uiScale = std::max(0.7f, std::min(1.5f, strtof(line.c_str() + 8, nullptr)));
        if (line.rfind("panelWidthDp=", 0) == 0) S.panelWidthDp = std::max(360.f, std::min(720.f, strtof(line.c_str() + 13, nullptr)));
        if (line.rfind("menuOpen=", 0) == 0) S.menuOpen = line[9] == '1';
        if (line.rfind("scrollbarPx=", 0) == 0) S.scrollbarPx = std::max(20.f, std::min(80.f, strtof(line.c_str() + 12, nullptr)));
        if (line.rfind("handleY=", 0) == 0) S.handleY = std::max(0.f, std::min(1.f, strtof(line.c_str() + 8, nullptr)));
        if (line.rfind("dockRight=", 0) == 0) S.dockRight = line[10] == '1';
        if (line.rfind("featuresTabsCollapsed=", 0) == 0) S.featuresTabsCollapsed = line[22] == '1';
        if (line.rfind("crashed=", 0) == 0) S.crashed.push_back(line.substr(8));
        a = b + 1;
    }
    std::string last = ReadTextFile(DataPath("offsetlab_last_fired.txt"));
    size_t nl = last.find('\n');
    S.lastFired = last.substr(0, nl);
}

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void LoadNames() {
    if (S.namesLoaded || !GameBase()) return;
    size_t n = 0;
    const LabType* t = Types(&n);
    S.names.resize(n);
    S.titles.resize(n);
    for (size_t i = 0; i < n; i++) {
        std::string s = GameString(t[i].nameAddr);
        if (s.empty()) {
            S.unreadable++;
            S.names[i] = S.titles[i] = "(unreadable #" + std::to_string(i) + ")";
            continue;
        }
        if (Fnv1a(s) == t[i].nameHash) S.verified++;
        S.names[i] = s;
        S.titles[i] = Friendly(s);
    }
    S.namesLoaded = true;
    S.evStatus.resize(n);
    int events = 0, unknown = 0;
    for (size_t i = 0; i < n; i++) {
        S.evStatus[i] = (int8_t)EventStatusOf(i);
        events += S.evStatus[i] == kIsEvent;
        unknown += S.evStatus[i] == kEventUnknown;
    }
    Log("Read %d type names (%d match 0.35.2, %d unreadable); %d are events, %d unknown", (int)n, S.verified,
        S.unreadable, events, unknown);
}

void OpenMemory(uintptr_t offset) {
    snprintf(S.memAddr, sizeof S.memAddr, "%" PRIXPTR, offset);
    S.memAbsolute = false;
    S.openTab = 2;
}

void Select(int i, bool record = true) {
    if (i == S.selected) return;
    S.selected = i;
    S.lastResult.clear();
    if (record) {
        S.history.resize(S.historyPos + 1);
        S.history.push_back(i);
        S.historyPos = (int)S.history.size() - 1;
    }
    S.recent.erase(std::remove(S.recent.begin(), S.recent.end(), i), S.recent.end());
    S.recent.insert(S.recent.begin(), i);
    if (S.recent.size() > 12) S.recent.pop_back();
}

// ---------------------------------------------------------------- settings: defaults, controls, Lua

std::vector<float> Numbers(const std::string& s) {
    std::vector<float> v;
    const char* p = s.c_str();
    while (*p) {
        if (std::isdigit((unsigned char)*p) || ((*p == '-' || *p == '.') && (std::isdigit((unsigned char)p[1]) || p[1] == '.'))) {
            char* end = nullptr;
            v.push_back(strtof(p, &end));
            p = end;
        } else {
            p++;
        }
    }
    return v;
}

const std::vector<std::string>& Options(const LabSetting& st) {
    auto it = S.options.find(st.enumIndex);
    if (it != S.options.end()) return it->second;
    auto& v = S.options[st.enumIndex];
    const LabEnum& e = Enum(st.enumIndex);
    for (uint32_t k = 0; k < e.count; k++) v.push_back(GameString(EnumOptionAddr(e.first + k)));
    return v;
}

Value DefaultValue(const LabSetting& st) {
    Value v;
    std::string d = Unquote(GameString(st.defaultAddr));
    auto nums = Numbers(d);
    switch (st.kind) {
        case kBool: v.b = d == "true"; break;
        case kFloat: v.f[0] = nums.empty() ? 0.f : nums[0]; break;
        case kInt: v.i = nums.empty() ? 0 : (int)nums[0]; break;
        case kVec3: case kColor3: case kVec4: case kVec2:
            for (size_t k = 0; k < nums.size() && k < 4; k++) v.f[k] = nums[k];
            if (st.kind == kColor3 && nums.empty()) v.f[0] = v.f[1] = v.f[2] = 1.f;
            break;
        case kEnum: {
            auto& opts = Options(st);
            for (size_t k = 0; k < opts.size(); k++)
                if (opts[k] == d) v.option = (int)k;
            break;
        }
        default: v.text = (d == "null" || d == "NULL" || d == "nullptr") ? "" : d; break;
    }
    return v;
}

std::string Num(float f) {
    char b[48];
    snprintf(b, sizeof b, "%.6g", f);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}
std::string Quote(const std::string& v) {
    std::string q = "\"";
    for (char ch : v) {
        if (ch == '"' || ch == '\\') q.push_back('\\');
        q.push_back(ch);
    }
    return q + "\"";
}

std::string LuaValue(const LabSetting& st, const Value& v) {
    switch (st.kind) {
        case kBool: return v.b ? "true" : "false";
        case kFloat: return Num(v.f[0]);
        case kInt: return std::to_string(v.i);
        case kText: return Quote(v.text);
        case kVec3: case kColor3: return "{" + Num(v.f[0]) + ", " + Num(v.f[1]) + ", " + Num(v.f[2]) + "}";
        case kVec4: return "{" + Num(v.f[0]) + ", " + Num(v.f[1]) + ", " + Num(v.f[2]) + ", " + Num(v.f[3]) + "}";
        case kVec2: return "{" + Num(v.f[0]) + ", " + Num(v.f[1]) + "}";
        case kEnum: {
            auto& opts = Options(st);
            return v.option < (int)opts.size() ? Quote(opts[v.option]) : "nil";
        }
        default: return v.text.empty() ? "nil" : v.text;  // objects and unknown kinds: typed as Lua
    }
}

// Slider range from the setting's name and default.
void FloatRange(const std::string& name, float def, float& lo, float& hi) {
    std::string n = name;
    for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("alpha") || has("opacity") || has("percent") || has("01") || has("ratio") || has("chance") || has("prob"))
        lo = 0, hi = 1;
    else if (has("duration") || has("time") || has("delay") || has("interval") || has("cooldown"))
        lo = 0, hi = 30;
    else if (has("speed") || has("velocity"))
        lo = 0, hi = 50;
    else if (has("radius") || has("dist") || has("range") || has("height") || has("size") || has("length"))
        lo = 0, hi = 200;
    else if (has("scale") || has("mult") || has("intensity") || has("strength") || has("force") || has("amount"))
        lo = 0, hi = 10;
    else if (has("angle") || has("rot") || has("yaw") || has("pitch"))
        lo = -180, hi = 180;
    else
        lo = -100, hi = 100;
    float a = std::fabs(def);
    if (def < lo) lo = -a * 2;
    if (def > hi) hi = a * 2;
}

// Draws the control for one setting; returns true when the user changed it.
bool SettingControl(const LabSetting& st, const std::string& name, Value& v, float width) {
    bool ch = false;
    ImGui::SetNextItemWidth(width);
    switch (st.kind) {
        case kBool:
            ch = ImGui::Checkbox("##v", &v.b);
            ImGui::SameLine();
            ImGui::TextUnformatted(v.b ? "on" : "off");
            break;
        case kFloat: {
            float lo, hi;
            auto d = Numbers(Unquote(GameString(st.defaultAddr)));
            if (st.hasRange == 1) lo = st.minValue, hi = st.maxValue;
            else FloatRange(name, d.empty() ? 0.f : d[0], lo, hi);
            ImGui::SetNextItemWidth(width * 0.62f);
            ch |= ImGui::SliderFloat("##s", &v.f[0], lo, hi, "%.3f", st.hasRange == 1 ? ImGuiSliderFlags_AlwaysClamp : 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width * 0.36f);
            ch |= ImGui::InputFloat("##t", &v.f[0], 0, 0, "%.3f");
            if (st.hasRange == 1) v.f[0] = std::max(st.minValue, std::min(st.maxValue, v.f[0]));
            break;
        }
        case kInt:
            ImGui::SetNextItemWidth(width * 0.62f);
            {
                auto d = Numbers(Unquote(GameString(st.defaultAddr)));
                int def = d.empty() ? 0 : (int)d[0];
                int lo = st.hasRange == 1 ? (int)st.minValue : std::min(0, def * 4);
                int hi = st.hasRange == 1 ? (int)st.maxValue : std::max(100, def * 4);
                ch |= ImGui::SliderInt("##s", &v.i, lo, hi, "%d", st.hasRange == 1 ? ImGuiSliderFlags_AlwaysClamp : 0);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width * 0.36f);
            ch |= ImGui::InputInt("##t", &v.i, 0, 0);
            if (st.hasRange == 1) v.i = std::max((int)st.minValue, std::min((int)st.maxValue, v.i));
            break;
        case kText: {
            char buf[256];
            snprintf(buf, sizeof buf, "%s", v.text.c_str());
            if (ImGui::InputTextWithHint("##v", "text", buf, sizeof buf)) v.text = buf, ch = true;
            break;
        }
        case kVec3: ch = ImGui::DragFloat3("##v", v.f, 0.1f); break;
        case kColor3: ch = ImGui::ColorEdit3("##v", v.f); break;
        case kVec4: ch = ImGui::DragFloat4("##v", v.f, 0.05f); break;
        case kVec2: ch = ImGui::DragFloat2("##v", v.f, 0.05f); break;
        case kEnum: {
            auto& opts = Options(st);
            const char* cur = v.option < (int)opts.size() ? opts[v.option].c_str() : "-";
            if (ImGui::BeginCombo("##v", cur)) {
                for (int k = 0; k < (int)opts.size(); k++)
                    if (ImGui::Selectable(opts[k].c_str(), k == v.option)) v.option = k, ch = true;
                ImGui::EndCombo();
            }
            break;
        }
        default: {
            char buf[256];
            snprintf(buf, sizeof buf, "%s", v.text.c_str());
            const char* hint = st.kind == kObject ? "Lua object, e.g. game:markerBarn():CreateMarker(0)" : "Lua value";
            if (ImGui::InputTextWithHint("##v", hint, buf, sizeof buf)) v.text = buf, ch = true;
            break;
        }
    }
    return ch;
}

const char* KindLabel(uint8_t k) {
    static const char* n[] = {"unknown type", "toggle", "number", "whole number", "text", "x, y, z", "colour",
                              "4 numbers", "choice", "object", "x, y"};
    return k < 11 ? n[k] : "value";
}

std::string FireScript(int i) {
    size_t n = 0;
    const LabType& t = Types(&n)[i];
    auto& vals = S.values[i];
    std::string s = "local e = game:eventBarn():AddEventByMetaName(" + Quote(S.names[i]) + ")\n";
    s += "if e == nil then error(\"not an event\") end\n";
    for (uint32_t k = 0; k < t.settingCount && k < vals.size(); k++) {
        if (!vals[k].changed) continue;
        const LabSetting& st = Setting(t.settingFirst + k);
        s += "e:" + GameString(st.nameAddr) + "(" + LuaValue(st, vals[k]) + ")\n";
    }
    s += "__offsetLab = __offsetLab or {}\n__offsetLab[" + Quote(S.names[i]) + "] = e\n"
         "__offsetLabLevel = __offsetLabLevel or {}\n__offsetLabLevel[" + Quote(S.names[i]) + "] = game:levelName()\n";
    return s + "e:Start()\n";
}

bool LooksLikePlainValue(const std::string& t);

void Fire(int i) {
    size_t n = 0;
    const LabType& t = Types(&n)[i];
    auto& vals = S.values[i];
    for (uint32_t k = 0; k < t.settingCount && k < vals.size(); k++) {
        const LabSetting& st = Setting(t.settingFirst + k);
        if (st.kind == kObject && vals[k].changed && LooksLikePlainValue(vals[k].text)) {
            S.lastOk = false;
            S.lastResult = Friendly(GameString(st.nameAddr)) + " needs a game object, not \"" + vals[k].text +
                           "\". Clear it (reset) or give Lua that makes one.";
            Log("Not fired %s: %s", S.names[i].c_str(), S.lastResult.c_str());
            return;
        }
    }
    std::string script = FireScript(i);
    // written before firing, so if the game crashes the log and the next start say what it was
    Log("Firing %s:\n%s", S.names[i].c_str(), script.c_str());
    WriteTextFile(DataPath("offsetlab_last_fired.txt"), S.names[i] + "\n");
    int rc = RunLua(script, "Fire " + S.names[i]);
    S.lastOk = rc == 0;
    S.lastResult = rc == 0 ? "Fired " + S.titles[i] + " (a script error would show in the Log tab)"
                 : rc == -1 ? "The game isn't ready yet (enter the world first)"
                 : rc == -3 ? "The game isn't running frames right now; try again"
                            : "The game refused it (code " + std::to_string(rc) + "): not an event, or a setting is wrong";
    Log("%s: %s", S.names[i].c_str(), S.lastResult.c_str());
}

void Stop(int i) {
    std::string n = Quote(S.names[i]);
    // only in the level it was fired in: the game deletes events on a level change, and touching a deleted one
    // crashes it. Events that finish by themselves are deleted too: Stop those only while they're still going.
    int rc = RunLua("local e = __offsetLab and __offsetLab[" + n + "]\nif e == nil then error(\"not fired\") end\n"
                    "if (__offsetLabLevel or {})[" + n + "] ~= game:levelName() then __offsetLab[" + n +
                    "] = nil; error(\"fired in another level\") end\n"
                    "e:SetEnabled(false)\n__offsetLab[" + n + "] = nil\n",
                    "Stop " + S.names[i]);
    S.lastOk = rc == 0;
    S.lastResult = rc == 0 ? "Stopped " + S.titles[i] : "Couldn't stop it (it wasn't fired, or it can't be stopped)";
    Log("%s: %s", S.names[i].c_str(), S.lastResult.c_str());
}

// What a setting accepts, for the line under it. Ranges marked "from the game" come from its own Tool_MinMaxValues.
std::string Accepts(const LabSetting& st) {
    char b[200];
    if (st.hasRange == 2)
        return st.kind == kBool ? "on or off (type guessed from its name: the game doesn't say)"
                                : "a number (type guessed from its name: the game doesn't say; the slider range is a suggestion)";
    switch (st.kind) {
        case kBool: return "on or off";
        case kFloat:
            if (st.hasRange == 1) snprintf(b, sizeof b, "a number from %g to %g (the game's range)", st.minValue, st.maxValue);
            else snprintf(b, sizeof b, "any number; the slider shows a suggested range (the game gives none)");
            return b;
        case kInt:
            if (st.hasRange == 1) snprintf(b, sizeof b, "a whole number from %g to %g (the game's range)", st.minValue, st.maxValue);
            else snprintf(b, sizeof b, "any whole number; the slider shows a suggested range (the game gives none)");
            return b;
        case kText: return "text, e.g. a name the game uses";
        case kVec3: return "three numbers: x, y, z";
        case kVec2: return "two numbers: x, y";
        case kColor3: return "a colour: red, green, blue, each 0 to 1";
        case kVec4: return "four numbers";
        case kEnum: {
            snprintf(b, sizeof b, "one of %u options from the game", Enum(st.enumIndex).count);
            return b;
        }
        case kObject: {
            std::string t = st.objTypeAddr ? GameString(st.objTypeAddr) : "game object";
            return "a " + t + " (a game object, not a number or text). Leave empty unless you have Lua for one, "
                   "e.g. game:markerBarn():CreateMarker(0) for a marker";
        }
        default: return "unknown type: leave empty unless you know what it takes";
    }
}

bool LooksLikePlainValue(const std::string& t) {
    if (t.empty()) return false;
    if (t == "true" || t == "false") return true;
    if (t.front() == '"' || t.front() == '\'') return true;
    char* end = nullptr;
    strtod(t.c_str(), &end);
    return end && *end == 0;
}

// ---------------------------------------------------------------- Game functions tab

void Details(int i) {
    size_t n = 0;
    const LabType& t = Types(&n)[i];

    ImGui::PushStyleColor(ImGuiCol_Text, kSky300);
    ImGui::TextWrapped("%s", S.titles[i].c_str());
    ImGui::PopStyleColor();
    Dim("%s  ·  %s", S.names[i].c_str(), kAreaNames[t.area]);
    if (Fnv1a(S.names[i]) != t.nameHash)
        ImGui::TextColored(kAmber300, "This name doesn't match Sky 0.35.2: the offsets may be wrong for this game version.");

    // what it does
    ImGui::Spacing();
    Eyebrow("WHAT IT DOES");
    int described = 0;
    for (int k = 0; k < t.noteCount; k++) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", Unquote(GameString(NoteAddr(t.noteFirst + k))).c_str());
        described++;
    }
    for (uint32_t k = 0; k < t.settingCount; k++)
        if (Setting(t.settingFirst + k).descAddr) described++;
    if (!described)
        Dim("The game has no description for this. Going by its name: \"%s\". Try it and watch what happens.",
            S.titles[i].c_str());
    else if (!t.noteCount)
        Dim("The game describes some of its settings below.");

    // settings
    auto& vals = S.values[i];
    if (vals.size() != t.settingCount) {
        vals.clear();
        for (uint32_t k = 0; k < t.settingCount; k++) vals.push_back(DefaultValue(Setting(t.settingFirst + k)));
    }
    ImGui::Spacing();
    Eyebrow("SETTINGS");
    if (!t.settingCount) Dim("No settings: just fire it.");
    else Dim("Each starts at the game's default. Only settings you change are sent (marked with a dot).");
    bool anyChange = false;
    float w = ImGui::GetContentRegionAvail().x;
    for (uint32_t k = 0; k < t.settingCount; k++) {
        const LabSetting& st = Setting(t.settingFirst + k);
        std::string sname = GameString(st.nameAddr);
        ImGui::PushID((int)k);
        ImGui::Separator();
        {  // dot: filled when this setting will be sent
            ImVec2 p = ImGui::GetCursorScreenPos();
            float r = ImGui::GetTextLineHeight() * 0.22f, cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (vals[k].changed) dl->AddCircleFilled(ImVec2(p.x + r + 1, cy), r, ImGui::GetColorU32(kSky400));
            else dl->AddCircle(ImVec2(p.x + r + 1, cy), r, ImGui::GetColorU32(White(0.30f)));
            ImGui::Dummy(ImVec2(r * 2 + 4, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(Friendly(sname).c_str());
        ImGui::SameLine();
        std::string kind = KindLabel(st.kind);
        if (st.kind == kObject && st.objTypeAddr) kind += ": " + GameString(st.objTypeAddr);
        Dim("%s", kind.c_str());
        if (st.descAddr) Dim("%s", Unquote(GameString(st.descAddr)).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, st.hasRange == 1 ? kSky300 : st.hasRange == 2 ? kAmber300 : White(0.70f));
        ImGui::TextWrapped("Accepts: %s", Accepts(st).c_str());
        ImGui::PopStyleColor();
        if (SettingControl(st, sname, vals[k], w * 0.80f)) {
            vals[k].changed = true;
            anyChange = true;
        }
        if (vals[k].changed) {
            ImGui::SameLine();
            if (ImGui::SmallButton("reset")) vals[k] = DefaultValue(st);
        }
        ImGui::PopID();
    }
    ImGui::Separator();

    // actions
    ImGui::Spacing();
    bool blocked = t.area == kAreaServer && !S.serverTypes;
    if (blocked) {
        ImGui::TextColored(kAmber300, "Server type: turn on \"Server types\" at the top to fire it.");
        ImGui::BeginDisabled();
    }
    // only events can be fired: the game aborts when asked to fire anything else
    int ev = S.evStatus.empty() ? kEventUnknown : S.evStatus[i];
    if (ev == kNotEvent) {
        std::vector<size_t> anc;
        EventStatusOf(i, &anc);
        std::string chain;
        for (size_t k = 0; k < anc.size() && k < 4; k++) chain += (k ? " > " : "") + Friendly(S.names[anc[k]]);
        ImGui::TextColored(kRed300, "Not an event%s%s.", chain.empty() ? "" : ": it's a kind of ", chain.c_str());
        Dim("The game builds these itself. Asking it to fire one makes the game stop (abort), so Fire is off. "
            "You can still look at its settings and offsets.");
        if (ImGui::Button("Copy as Lua")) {
            snprintf(S.lua, sizeof S.lua, "%s", FireScript(i).c_str());
            S.openTab = 1;
        }
        goto offsets;
    }
    if (ev == kEventUnknown)
        ImGui::TextColored(kAmber300, "Can't tell whether this is an event. If it isn't, the game will stop (abort).");
    {
    // object settings left empty are the usual cause of the game crashing a little after firing
    std::vector<std::string> emptyObjects;
    for (uint32_t k = 0; k < t.settingCount; k++) {
        const LabSetting& st = Setting(t.settingFirst + k);
        if (st.kind == kObject && vals[k].text.empty()) emptyObjects.push_back(Friendly(GameString(st.nameAddr)));
    }
    if (IsCrashed(S.names[i]))
        ImGui::TextColored(kRed300, "You marked this as crashing the game.");
    if (!emptyObjects.empty()) {
        std::string list;
        for (size_t k = 0; k < emptyObjects.size() && k < 4; k++) list += (k ? ", " : "") + emptyObjects[k];
        if (emptyObjects.size() > 4) list += ", ...";
        ImGui::TextColored(kAmber300, "Empty object settings (%s): the game may crash using them. Fill them in, or fire anyway.",
                           list.c_str());
    }
    bool risky = !emptyObjects.empty() || IsCrashed(S.names[i]) || ev == kEventUnknown;
    if (risky) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.25f, 0.25f, 1.f));
        if (ImGui::Button("Fire anyway", ImVec2(Px(200), 0))) Fire(i);
        ImGui::PopStyleColor(2);
    } else if (AccentButton("Fire", ImVec2(Px(200), 0))) {
        Fire(i);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(Px(140), 0))) Stop(i);
    ImGui::SameLine();
    ImGui::Checkbox("Fire on change", &S.fireOnChange);
    if (S.fireOnChange && anyChange && !risky) Fire(i);
    if (blocked) ImGui::EndDisabled();
    if (ImGui::Button("Reset all")) S.values.erase(i);
    ImGui::SameLine();
    if (!IsCrashed(S.names[i])) {
        if (ImGui::Button("Mark as crashing")) {
            S.crashed.push_back(S.names[i]);
            SavePrefs();
        }
    } else if (ImGui::Button("Unmark crashing")) {
        S.crashed.erase(std::remove(S.crashed.begin(), S.crashed.end(), S.names[i]), S.crashed.end());
        SavePrefs();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy as Lua")) {
        snprintf(S.lua, sizeof S.lua, "%s", FireScript(i).c_str());
        S.openTab = 1;
    }
    if (!S.lastResult.empty()) ImGui::TextColored(S.lastOk ? kSky300 : kRed300, "%s", S.lastResult.c_str());
    }

offsets:
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Offsets")) {
        auto row = [](const char* k, uintptr_t v) {
            Dim("%s", k);
            ImGui::SameLine(160);
            ImGui::Text("%s", v ? Hex(v).c_str() : "-");
            if (v) {
                ImGui::SameLine();
                ImGui::PushID(k);
                if (ImGui::SmallButton("view memory")) OpenMemory(v);
                ImGui::PopID();
            }
        };
        row("Registered at", t.registerAt);
        row("Type object", t.typeObject);
        row("Name text", t.nameAddr);
        for (uint32_t k = 0; k < t.settingCount; k++) {
            const LabSetting& st = Setting(t.settingFirst + k);
            Dim("%s: at +0x%X in the event", GameString(st.nameAddr).c_str(), st.eventOffset);
        }
    }
}

void GameFunctionsTab() {
    LoadNames();
    if (!S.namesLoaded) {
        Dim("Waiting for the game to load...");
        return;
    }
    size_t n = 0;
    const LabType* t = Types(&n);

    ImGui::Checkbox("Server types", &S.serverTypes);
    ImGui::SameLine();
    Dim("(off at every start)");
    if (S.serverTypes)
        ImGui::TextColored(kAmber300,
            "Server types send requests to the server the game is connected to. Only use them on your private server.");

    // back / forward / recent
    bool canBack = S.historyPos > 0, canFwd = S.historyPos + 1 < (int)S.history.size();
    if (!canBack) ImGui::BeginDisabled();
    if (ImGui::Button("< Back")) Select(S.history[--S.historyPos], false);
    if (!canBack) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canFwd) ImGui::BeginDisabled();
    if (ImGui::Button("Forward >")) Select(S.history[++S.historyPos], false);
    if (!canFwd) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::BeginCombo("##recent", "Recent", ImGuiComboFlags_HeightLarge)) {
        for (int r : S.recent)
            if (ImGui::Selectable(S.titles[r].c_str(), r == S.selected)) {
                int keep = r;
                Select(keep);
            }
        if (S.recent.empty()) Dim("Nothing opened yet");
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    const char* areaLabel = S.area < 0 ? "All areas" : kAreaNames[S.area];
    if (ImGui::BeginCombo("##area", areaLabel, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("All areas", S.area < 0)) S.area = -1;
        for (int a = 0; a < kAreaCount; a++)
            if (ImGui::Selectable(kAreaNames[a], S.area == a)) S.area = a;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search", S.search, sizeof S.search);

    ImGui::Checkbox("Events only", &S.eventsOnly);
    ImGui::SameLine();
    Dim("(only events can be fired)");

    std::vector<int> rows;
    uintptr_t want = 0;
    bool hexSearch = S.search[0] == '0' && (S.search[1] == 'x' || S.search[1] == 'X') && ParseHex(S.search, want);
    for (size_t i = 0; i < n; i++) {
        if (S.area >= 0 && t[i].area != S.area) continue;
        if (S.eventsOnly && S.evStatus[i] != kIsEvent) continue;
        if (hexSearch ? (t[i].registerAt != want && t[i].typeObject != want)
                      : !(Contains(S.titles[i], S.search) || Contains(S.names[i], S.search)))
            continue;
        rows.push_back((int)i);
    }
    int nEvents = (int)std::count(S.evStatus.begin(), S.evStatus.end(), (int8_t)kIsEvent);
    Dim("%zu shown  ·  %d events of %zu types  ·  %d names checked against 0.35.2", rows.size(), nEvents, n, S.verified);

    // list: two-line rows (readable title, then the game's name and area)
    float listH = ImGui::GetContentRegionAvail().y * 0.38f;
    if (ImGui::BeginChild("##list", ImVec2(0, listH), ImGuiChildFlags_Border)) {
        float lineH = ImGui::GetTextLineHeight();
        ImGuiListClipper clip;
        clip.Begin((int)rows.size(), lineH * 2 + ImGui::GetStyle().ItemSpacing.y + 12);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                int i = rows[r];
                ImGui::PushID(i);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##row", S.selected == i, 0, ImVec2(0, lineH * 2 + 12))) Select(i);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                bool crashed = IsCrashed(S.names[i]);
                dl->AddText(ImVec2(p.x + 8, p.y + 5), ImGui::GetColorU32(crashed ? kRed300 : White(0.97f)), S.titles[i].c_str());
                const char* evLabel = S.evStatus[i] == kIsEvent ? "event" : S.evStatus[i] == kNotEvent ? "not an event" : "event?";
                std::string sub = S.names[i] + "  ·  " + evLabel + "  ·  " + kAreaNames[t[i].area] + (crashed ? "  ·  crashed before" : "");
                dl->AddText(ImVec2(p.x + 8, p.y + 5 + lineH), ImGui::GetColorU32(crashed ? kRed300 : t[i].area == kAreaServer ? kAmber300 : White(0.62f)),
                            sub.c_str());
                ImGui::PopID();
            }
    }
    TouchScroll();
    ImGui::EndChild();

    if (ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        if (S.selected >= 0) Details(S.selected);
        else Dim("Pick a game function to see what it does, set it up with toggles, sliders and choices, and fire it.");
    }
    TouchScroll();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Lua tab

void LuaTab() {
    Dim("Runs in the game's Lua, on the game's main thread at its next frame. Errors show in the Log tab.");
    ImGui::InputTextMultiline("##lua", S.lua, sizeof S.lua, ImVec2(-1, ImGui::GetContentRegionAvail().y * 0.55f));
    if (AccentButton("Run", ImVec2(Px(120), 0))) {
        int rc = RunLua(S.lua, "Lua tab");
        S.luaHistory.insert(S.luaHistory.begin(), std::string(rc == 0 ? "sent  " : rc == -1 ? "not ready  " : "error  ") +
                                                      std::string(S.lua).substr(0, 80));
        if (S.luaHistory.size() > 30) S.luaHistory.pop_back();
        Log("Lua: %s", rc == 0 ? "sent" : rc == -1 ? "game not ready" : "error");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) S.lua[0] = 0;
    ImGui::Spacing();
    Eyebrow("HISTORY");
    if (ImGui::BeginChild("##hist", ImVec2(0, 0), ImGuiChildFlags_Border))
        for (auto& h : S.luaHistory) ImGui::TextUnformatted(h.c_str());
    TouchScroll();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Memory tab

void MemoryTab() {
    uintptr_t base = GameBase();
    Dim("Read-only. Offsets are from the start of libBootloader.so (%s) unless Absolute is on.", Hex(base).c_str());
    ImGui::SetNextItemWidth(Px(200));
    ImGui::InputTextWithHint("##addr", "hex offset", S.memAddr, sizeof S.memAddr, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::Checkbox("Absolute", &S.memAbsolute);
    ImGui::SameLine();
    if (ImGui::Button("Game object")) {
        snprintf(S.memAddr, sizeof S.memAddr, "%" PRIXPTR, GameObject());
        S.memAbsolute = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Px(120));
    ImGui::SliderInt("rows", &S.memRows, 4, 64);

    uintptr_t a = 0;
    if (!ParseHex(S.memAddr, a)) return;
    if (!S.memAbsolute) a += base;
    std::vector<uint8_t> bytes(S.memRows * 16);
    std::vector<bool> ok(bytes.size(), false);
    for (int r = 0; r < S.memRows; r++)
        if (ReadMemory(a + r * 16, &bytes[r * 16], 16)) std::fill(ok.begin() + r * 16, ok.begin() + r * 16 + 16, true);

    uint64_t q = 0;
    if (ReadMemory(a, &q, 8)) {
        double dval;
        float fval;
        memcpy(&dval, &q, 8);
        memcpy(&fval, &q, 4);
        Dim("At %s:  u64 %" PRIu64 "  i32 %d  float %g  double %g  ptr %s", Hex(a).c_str(), q, (int32_t)q, fval, dval,
            Hex(q).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("follow pointer")) {
            snprintf(S.memAddr, sizeof S.memAddr, "%" PRIX64, q);
            S.memAbsolute = true;
        }
        std::string str = ReadString(a, 64);
        if (str.size() >= 2) Dim("As text: \"%s\"", str.c_str());
    } else {
        ImGui::TextColored(kRed300, "Not readable at %s", Hex(a).c_str());
    }

    if (ImGui::BeginChild("##hex", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (int r = 0; r < S.memRows; r++) {
            char line[160];
            int p = snprintf(line, sizeof line, "%010" PRIXPTR "  ", (a + r * 16) - (S.memAbsolute ? 0 : base));
            for (int c = 0; c < 16; c++)
                p += snprintf(line + p, sizeof line - p, ok[r * 16 + c] ? "%02X " : "?? ", bytes[r * 16 + c]);
            p += snprintf(line + p, sizeof line - p, " ");
            for (int c = 0; c < 16; c++) {
                uint8_t b = bytes[r * 16 + c];
                line[p++] = ok[r * 16 + c] && b >= 32 && b < 127 ? (char)b : '.';
            }
            line[p] = 0;
            ImGui::TextUnformatted(line);
        }
    }
    TouchScroll();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Native functions tab

void NativeTab() {
    const auto& fns = ExportedFunctions();
    auto watches = Watches();

    double now = Now();
    if (now - S.lastSample >= 1.0) {
        for (auto& w : watches) {
            auto& r = S.rate[w.offset];
            r.second = (w.calls - r.first) / (now - S.lastSample);
            r.first = w.calls;
        }
        S.lastSample = now;
    }

    Eyebrow("WATCHED");
    Dim("Counts every call and keeps the first four arguments. Watching a busy function can slow the game.");
    if (watches.empty()) Dim("Nothing watched yet: tap Watch on a function below.");
    for (auto& w : watches) {
        ImGui::PushID((int)w.offset);
        ImGui::TextColored(kSky300, "%s", w.label.c_str());
        Dim("%" PRIu64 " calls  ·  %.0f per second  ·  last arguments %" PRIX64 ", %" PRIX64 ", %" PRIX64 ", %" PRIX64,
            w.calls, S.rate[w.offset].second, w.args[0], w.args[1], w.args[2], w.args[3]);
        if (ImGui::SmallButton("Stop watching")) StopWatch(w.offset);
        ImGui::PopID();
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("Watch any offset")) {
        ImGui::SetNextItemWidth(Px(200));
        ImGui::InputTextWithHint("##manual", "hex offset", S.manualAddr, sizeof S.manualAddr,
                                 ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        if (ImGui::Button("Watch")) {
            uintptr_t off = 0;
            std::string err;
            if (ParseHex(S.manualAddr, off) && !StartWatch(off, "Offset " + Hex(off), err))
                Log("Watch %s: %s", Hex(off).c_str(), err.c_str());
        }
        Dim("Must be the start of a function, or the game will crash.");
    }

    ImGui::Spacing();
    Eyebrow("NATIVE FUNCTIONS");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##fs", "Search", S.fnSearch, sizeof S.fnSearch);
    std::vector<int> rows;
    for (size_t i = 0; i < fns.size(); i++)
        if (Contains(fns[i].name, S.fnSearch)) rows.push_back((int)i);
    Dim("%zu of %zu functions the game exports by name", rows.size(), fns.size());
    if (ImGui::BeginChild("##fns", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const auto& f = fns[rows[r]];
                ImGui::PushID(rows[r]);
                if (ImGui::SmallButton("Watch")) {
                    std::string err;
                    if (!StartWatch(f.offset, f.name.substr(0, 80), err)) Log("Watch %s: %s", f.name.c_str(), err.c_str());
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(f.name.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  ·  %zu bytes", Hex(f.offset).c_str(), f.size);
                ImGui::PopID();
            }
    }
    TouchScroll();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Recorder tab

constexpr size_t kRecMaxRows = 20000;
constexpr double kNoisyPerSecond = 4.0;  // a type started more often than this (over at least 3 s) is "noisy"

bool Noisy(int type) {
    if (type < 0 || type >= (int)S.recStats.size()) return false;
    const auto& st = S.recStats[type];
    return st.n >= 8 && st.n / std::max(3.0, st.last - st.first) > kNoisyPerSecond;
}

// Takes what the hook recorded; every frame, also while the panel is collapsed, so the hook's ring never fills up.
void RecorderPump() {
    static std::vector<RecordedEvent> got;
    got.clear();
    if (!RecorderDrain(got, 8192)) return;
    size_t n = 0;
    Types(&n);
    if (S.recStats.size() != n) S.recStats.assign(n, State::RecStat());
    for (const auto& e : got) {
        S.recTotal++;
        if (e.type >= 0 && e.type < (int)n) {
            auto& st = S.recStats[e.type];
            if (!st.n) st.first = e.time;
            st.n++;
            st.last = e.time;
        }
        std::string raw(reinterpret_cast<const char*>(e.raw), std::min<size_t>(e.rawLen, kRecRaw));
        if (!S.rec.empty()) {
            auto& b = S.rec.back();
            if (b.type == e.type && b.id == e.metaId && b.raw == raw) {  // the same again: count it
                b.count++;
                b.last = e.time;
                continue;
            }
        }
        S.rec.push_back({++S.recSerial, e.time, e.time, e.metaId, e.type, 1, std::move(raw)});
        if (S.rec.size() > kRecMaxRows) S.rec.pop_front();
    }
    S.recDirty = true;
}

std::string RecTitle(const State::RecRow& r) {
    if (r.type >= 0 && r.type < (int)S.titles.size()) return S.titles[r.type];
    char b[64];
    snprintf(b, sizeof b, r.id == 0xFFFFFFFF ? "Unknown event" : "Unknown event (meta class id %u)", r.id);
    return b;
}

std::string RecSummary(const std::vector<RecValue>& vals, size_t max) {
    std::string s;
    for (size_t k = 0; k < vals.size() && k < max; k++) s += (k ? ", " : "") + vals[k].name + " " + vals[k].text;
    if (vals.size() > max) s += ", ...";
    return s;
}

// Why a row can't be replayed ("" = it can).
std::string RecBlocked(const State::RecRow& r) {
    if (r.type < 0 || r.type >= (int)S.evStatus.size()) return "unknown type";
    if (S.evStatus[r.type] != kIsEvent) return "not known to be an event";
    size_t n = 0;
    if (Types(&n)[r.type].area == kAreaServer && !S.serverTypes) return "server type: turn on Server types";
    if (!GameObject()) return "enter the world first";
    return "";
}

void RecReplay(const State::RecRow& r) {
    std::string why = RecBlocked(r);
    if (!why.empty()) {
        S.recMsgOk = false;
        S.recMsg = "Not replayed: " + why;
        return;
    }
    std::vector<std::pair<std::string, std::string>> sets;
    if (S.recWithValues)
        for (auto& v : RecordedValues(r.type, reinterpret_cast<const uint8_t*>(r.raw.data()), r.raw.size()))
            sets.emplace_back(v.name, v.lua);
    WriteTextFile(DataPath("offsetlab_last_fired.txt"), S.names[r.type] + "\n");  // for the crash note
    bool ok = ReplayEvent((size_t)r.type, sets);
    S.recMsgOk = ok;
    S.recMsg = ok ? "Replayed " + S.titles[r.type] + (sets.empty() ? "" : " with " + std::to_string(sets.size()) + " recorded values")
                  : "Couldn't replay " + S.titles[r.type] + " (see Log)";
}

void RecOpen(const State::RecRow& r) {
    if (r.type < 0) return;
    Select(r.type);
    S.openTab = 0;
}

void RecSave() {
    std::string path = DataPath("offsetlab_recording.txt");
    if (path.empty()) {
        S.recMsgOk = false;
        S.recMsg = "Couldn't find the mod's folder to save in";
        return;
    }
    std::string t = "Offset Lab event recording (Sky 0.35.2)\n";
    char b[256];
    snprintf(b, sizeof b, "%zu rows, %" PRIu64 " starts%s\n", S.rec.size(), S.recTotal,
             RecorderDropped() ? " (some were missed: the game started events faster than they were read)" : "");
    t += b;
    t += "time (s since Start)  name (readable title)  x count\n    setting = value (as the game had it when it started)\n\n";
    for (const auto& r : S.rec) {
        std::string name = r.type >= 0 && r.type < (int)S.names.size() ? S.names[r.type] : "?";
        snprintf(b, sizeof b, "%10.3f  ", r.t - S.recStart);
        t += b + name + "  (" + RecTitle(r) + ")";
        if (r.count > 1) {
            snprintf(b, sizeof b, "  x%u (until %.3f)", r.count, r.last - S.recStart);
            t += b;
        }
        t += "\n";
        for (auto& v : RecordedValues(r.type, reinterpret_cast<const uint8_t*>(r.raw.data()), r.raw.size()))
            t += "    " + v.name + " = " + v.lua + "\n";
    }
    bool ok = WriteTextFile(path, t);
    S.recMsgOk = ok;
    S.recMsg = ok ? "Saved " + std::to_string(S.rec.size()) + " rows to " + path : "Couldn't write " + path;
    Log("Recorder: %s", S.recMsg.c_str());
}

void RecorderTab() {
    LoadNames();
    if (!S.namesLoaded) {
        Dim("Waiting for the game to load...");
        return;
    }
    size_t n = 0;
    const LabType* types = Types(&n);
    Dim("Notes every event the game starts while you play (its own and the mod's), with the time and the values of "
        "its plain settings (toggles, numbers, vectors, colours) as the game had them. Replay starts one again.");
    Dim("%s", RecorderStatus().c_str());

    bool on = RecorderOn();
    if (!on) {
        if (AccentButton("Start", ImVec2(Px(140), 0))) {
            std::string err;
            if (RecorderStart(err)) {
                if (S.rec.empty()) S.recStart = RecorderNow();
                S.recMsg.clear();
            } else {
                S.recMsgOk = false;
                S.recMsg = "Not started: " + err;
            }
        }
    } else if (ImGui::Button("Stop", ImVec2(Px(140), 0))) {
        RecorderStop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(Px(110), 0))) {
        S.rec.clear();
        S.recShown.clear();
        S.recStats.assign(n, State::RecStat());
        S.recTotal = 0;
        S.recSelected = 0;
        RecorderDiscard();
        S.recStart = RecorderNow();
        S.recDirty = true;
        S.recMsg.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(Px(110), 0))) RecSave();
    if (on) {
        ImGui::SameLine();
        ImGui::TextColored(kRed300, "Recording");
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##recfilter", "Filter", S.recFilter, sizeof S.recFilter);
    if (ImGui::Checkbox("Hide noisy", &S.recHideNoisy)) S.recDirty = true;
    ImGui::SameLine();
    ImGui::Checkbox("Replay with recorded values", &S.recWithValues);
    ImGui::Checkbox("Server types", &S.serverTypes);
    ImGui::SameLine();
    Dim("(same switch as Game functions; off at every start)");

    // filter: per type, recomputed when the text changes; rows: when anything changed
    if (S.recFilterUsed != S.recFilter || S.recMatch.size() != n) {
        S.recFilterUsed = S.recFilter;
        S.recMatch.assign(n, 0);
        for (size_t i = 0; i < n; i++)
            S.recMatch[i] = Contains(S.titles[i], S.recFilter) || Contains(S.names[i], S.recFilter);
        S.recDirty = true;
    }
    int noisyTypes = 0;
    for (size_t i = 0; i < S.recStats.size(); i++) noisyTypes += Noisy((int)i);
    if (S.recDirty) {
        S.recShown.clear();
        for (int r = 0; r < (int)S.rec.size(); r++) {
            const auto& row = S.rec[r];
            if (row.type >= 0 ? !S.recMatch[row.type] : S.recFilter[0] != 0) continue;
            if (S.recHideNoisy && Noisy(row.type)) continue;
            S.recShown.push_back(r);
        }
        S.recDirty = false;
    }
    Dim("%zu shown of %zu rows  ·  %" PRIu64 " starts  ·  %d noisy types (over %.0f starts a second)%s%s", S.recShown.size(),
        S.rec.size(), S.recTotal, noisyTypes, kNoisyPerSecond, S.recHideNoisy && noisyTypes ? " hidden" : "",
        RecorderDropped() ? "  ·  some missed" : "");
    if (!S.recMsg.empty()) ImGui::TextColored(S.recMsgOk ? kSky300 : kRed300, "%s", S.recMsg.c_str());

    const State::RecRow* selected = nullptr;
    for (const auto& r : S.rec)
        if (r.serial == S.recSelected) selected = &r;

    float listH = ImGui::GetContentRegionAvail().y * (selected ? 0.60f : 1.f);
    if (ImGui::BeginChild("##reclist", ImVec2(0, listH), ImGuiChildFlags_Border)) {
        if (S.recShown.empty())
            Dim(on ? "Nothing yet: play, and events the game starts show up here." : "Press Start, then play.");
        bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f;  // keep showing the newest
        ImGuiListClipper clip;
        clip.Begin((int)S.recShown.size());
        const State::RecRow* act = nullptr;
        int action = 0;
        while (clip.Step())
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                const auto& r = S.rec[S.recShown[k]];
                ImGui::PushID((int)(r.serial & 0x7FFFFFFF));
                ImGui::TextColored(kSky300, "%8.2f s", r.t - S.recStart);
                ImGui::SameLine();
                ImGui::TextUnformatted(RecTitle(r).c_str());
                if (r.count > 1) {
                    ImGui::SameLine();
                    ImGui::TextColored(kAmber300, "x%u", r.count);
                }
                auto vals = RecordedValues(r.type, reinterpret_cast<const uint8_t*>(r.raw.data()), r.raw.size());
                std::string sub = (r.type >= 0 ? S.names[r.type] : std::string("?")) +
                                  (vals.empty() ? "" : "  ·  " + RecSummary(vals, 3));
                bool server = r.type >= 0 && types[r.type].area == kAreaServer;
                ImGui::PushStyleColor(ImGuiCol_Text, server ? kAmber300 : White(0.62f));
                ImGui::TextUnformatted(sub.c_str());
                ImGui::PopStyleColor();
                bool can = RecBlocked(r).empty();
                if (!can) ImGui::BeginDisabled();
                if (AccentButton("Replay", ImVec2(Px(120), 0))) act = &r, action = 1;
                if (!can) ImGui::EndDisabled();
                ImGui::SameLine();
                if (r.type < 0) ImGui::BeginDisabled();
                if (ImGui::Button("Open in Game functions")) act = &r, action = 2;
                if (r.type < 0) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(r.serial == S.recSelected ? "Hide values" : "Values")) act = &r, action = 3;
                ImGui::Separator();
                ImGui::PopID();
            }
        if (act && action == 1) RecReplay(*act);
        if (act && action == 2) RecOpen(*act);
        if (act && action == 3) S.recSelected = act->serial == S.recSelected ? 0 : act->serial;
        if (follow && on) ImGui::SetScrollHereY(1.0f);
    }
    TouchScroll();
    ImGui::EndChild();

    if (selected) {
        if (ImGui::BeginChild("##recdetail", ImVec2(0, 0), ImGuiChildFlags_Border)) {
            const auto& r = *selected;
            ImGui::PushStyleColor(ImGuiCol_Text, kSky300);
            ImGui::TextWrapped("%s", RecTitle(r).c_str());
            ImGui::PopStyleColor();
            Dim("%s  ·  started at %.3f s%s", r.type >= 0 ? S.names[r.type].c_str() : "?", r.t - S.recStart,
                r.count > 1 ? ", again and again after that" : "");
            auto vals = RecordedValues(r.type, reinterpret_cast<const uint8_t*>(r.raw.data()), r.raw.size());
            ImGui::Spacing();
            Eyebrow("RECORDED VALUES");
            if (vals.empty())
                Dim("None: this event has no toggle, number, vector or colour settings whose place in the event is "
                    "known (text, choices and game objects aren't recorded).");
            for (auto& v : vals) {
                ImGui::TextUnformatted(Friendly(v.name).c_str());
                ImGui::SameLine();
                Dim("%s", v.text.c_str());
            }
            if (r.type >= 0) {
                const LabType& t = types[r.type];
                int kept = RecordableSettings(r.type);
                if ((uint32_t)kept < t.settingCount)
                    Dim("%u other settings aren't recorded; a replay gives them the game's defaults.",
                        t.settingCount - (uint32_t)kept);
            }
            std::string why = RecBlocked(r);
            if (!why.empty()) ImGui::TextColored(kAmber300, "Replay: %s", why.c_str());
            if (why.empty() && AccentButton("Replay", ImVec2(Px(140), 0))) RecReplay(r);
            if (why.empty()) ImGui::SameLine();
            if (r.type >= 0 && ImGui::Button("Open in Game functions")) RecOpen(r);
        }
        TouchScroll();
        ImGui::EndChild();
    }
}

// ---------------------------------------------------------------- Log tab

void LogTab() {
    if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        for (auto& l : LogLines()) ImGui::TextWrapped("%s", l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    TouchScroll();
    ImGui::EndChild();
}

}  // namespace

void OpenMemoryAt(uint32_t offset) { OpenMemory(offset); }
void AllEventsTab() { GameFunctionsTab(); }  // the Game functions tab, shown as a Features sub-tab

float Dp(float v) { return DP(v); }

// Px: sizes written for the old layout (text 17.5 px high); scaled to the current text size.
float Px(float v) { return v * gTextPx / 17.5f; }

// Drag the content to scroll (touch): a vertical drag that starts on a button scrolls instead of pressing it.
void TouchScroll() {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(0)) return;
    ImVec2 d = ImGui::GetMouseDragDelta(0, 0.f);
    static bool panning = false;
    if (!panning && std::fabs(d.y) > Px(14) && std::fabs(d.y) > std::fabs(d.x) * 1.5f) {
        panning = true;
        if (ImGui::IsAnyItemActive()) ImGui::ClearActiveID();  // don't press the button the drag started on
    }
    if (panning) ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    if (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0)) panning = false;
}

bool FeaturesTabsCollapsed() {
    LoadPrefs();
    return S.featuresTabsCollapsed;
}

void SetFeaturesTabsCollapsed(bool collapsed) {
    LoadPrefs();
    if (S.featuresTabsCollapsed == collapsed) return;
    S.featuresTabsCollapsed = collapsed;
    SavePrefs();
}

void Draw(bool* open) {
    (void)open;
    LoadPrefs();
    FeaturesTick();  // repeating features keep going while the panel is collapsed
    SpawnTick();     // results of the Spawn tab's game-thread calls
    RecorderPump();  // the recorder too
    ImGuiStyle saved = ImGui::GetStyle();  // the theme is only for Offset Lab's windows, not Canvas's
    ImGuiIO& io = ImGui::GetIO();
    // TSM's auto-scale
    const float density = 1.6667f;
    float fitW = (io.DisplaySize.x * 0.75f) / (430.f * density), fitH = (io.DisplaySize.y * 0.80f) / (545.f * density);
    float uiScale = std::max(0.6f, std::min(1.0f, std::min(fitW, fitH)));
    gDp = density * uiScale * S.uiScale;
    gTextPx = DP(15);  // body text 15 dp high
    // window font scale so text is gTextPx high, whatever font size Canvas loaded
    float fontScale = gTextPx / std::max(1.f, ImGui::GetFont()->FontSize * io.FontGlobalScale);
    gScrollbarPx = S.scrollbarPx;
    ApplyTheme();
    const float margin = DP(12);
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float panelW = std::min(DP(S.panelWidthDp), W - 2 * margin - DP(60));
    const float panelX = S.dockRight ? W - margin - panelW : margin;

    // the Lab handle (like TSM's slide handle): tap to open/collapse; drag it up/down along the edge, or across to
    // the other side to dock the panel there. When open it sits on the panel's inner edge.
    const float hw = DP(48), hh = DP(110);
    float hy = margin + S.handleY * (H - 2 * margin - hh);
    float hx = S.menuOpen ? (S.dockRight ? panelX - hw - DP(4) : panelX + panelW + DP(4))
                          : (S.dockRight ? W - hw - DP(4) : DP(4));
    ImGui::SetNextWindowPos(ImVec2(hx, hy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hw, hh), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##offsetlab_handle", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(fontScale);
    ImGui::InvisibleButton("##handle", ImVec2(hw, hh));
    bool held = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    if (held) {
        ImVec2 d = ImGui::GetMouseDragDelta(0, 0.f);
        if (!S.handleDragging && (std::fabs(d.x) > DP(10) || std::fabs(d.y) > DP(10))) S.handleDragging = true;
        if (S.handleDragging) {
            ImVec2 m = io.MousePos;
            S.handleY = std::max(0.f, std::min(1.f, (m.y - margin - hh * 0.5f) / std::max(1.f, H - 2 * margin - hh)));
            if (m.x < W * 0.25f) S.dockRight = false;
            else if (m.x > W * 0.75f) S.dockRight = true;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (!S.handleDragging) S.menuOpen = !S.menuOpen;
        S.handleDragging = false;
        SavePrefs();
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        ImU32 bg = ImGui::GetColorU32(held ? kSky300 : hovered ? Sky(0.95f) : Sky(0.85f));
        dl->AddRectFilled(p, q, bg, DP(10));
        const char* label = "Lab";
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImU32 fg = ImGui::GetColorU32(ImVec4(0.059f, 0.090f, 0.165f, 1.f));
        dl->AddText(ImVec2((p.x + q.x - ts.x) * 0.5f, (p.y + q.y) * 0.5f - ts.y - DP(2)), fg, label);
        const char* arrow = (S.menuOpen == S.dockRight) ? ">" : "<";
        ImVec2 as = ImGui::CalcTextSize(arrow);
        dl->AddText(ImVec2((p.x + q.x - as.x) * 0.5f, (p.y + q.y) * 0.5f + DP(2)), fg, arrow);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (!S.menuOpen) {
        ImGui::GetStyle() = saved;
        return;
    }

    // the panel: full height on the handle's side, TSM's width (500 dp; Width 360-720 dp)
    ImGui::SetNextWindowPos(ImVec2(panelX, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, H - 2 * margin), ImGuiCond_Always);
    ImGui::Begin("##offsetlab_panel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(fontScale);

    Eyebrow("SKY MESH LAB");
    ImGui::SameLine();
    Dim("Offset Lab · Sky 0.35.2 · experimental");
    uintptr_t base = GameBase(), game = GameObject();
    ImGui::TextColored(base ? kSky300 : kRed300, "%s", base ? "Game library loaded" : "Game library not found");
    ImGui::SameLine();
    Dim("%s", game ? "· in the world, ready to fire" : "· enter the world to fire game functions");
    if (ImGui::CollapsingHeader("Display")) {
        Dim("Drag the Lab handle up or down to move it, or across the screen to dock the panel on the other side.");
        float lw = ImGui::GetContentRegionAvail().x * 0.62f;
        ImGui::SetNextItemWidth(lw);
        ImGui::SliderFloat("Size", &S.uiScale, 0.7f, 1.5f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) SavePrefs();
        ImGui::SetNextItemWidth(lw);
        ImGui::SliderFloat("Width", &S.panelWidthDp, 360.f, 720.f, "%.0f dp");
        if (ImGui::IsItemDeactivatedAfterEdit()) SavePrefs();
        ImGui::SetNextItemWidth(lw);
        ImGui::SliderFloat("Scrollbar", &S.scrollbarPx, 20.f, 80.f, "%.0f px");
        if (ImGui::IsItemDeactivatedAfterEdit()) SavePrefs();
        if (ImGui::Button("Defaults")) {
            S.uiScale = 1.f, S.panelWidthDp = 500.f, S.scrollbarPx = 40.f, S.handleY = 0.08f, S.dockRight = true;
            SavePrefs();
        }
    }
    if (!S.lastFired.empty() && !S.lastFiredShown) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.20f, 0.05f, 0.6f));
        if (ImGui::BeginChild("##lastfired", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY)) {
            ImGui::TextColored(kAmber300, "Last session, the last game function fired was %s.", S.lastFired.c_str());
            Dim("If the game crashed then, this is the likely cause.");
            if (ImGui::Button("Mark it as crashing")) {
                if (!IsCrashed(S.lastFired)) S.crashed.push_back(S.lastFired);
                SavePrefs();
                S.lastFiredShown = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Dismiss")) S.lastFiredShown = true;
        }
        TouchScroll();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    static const char* kTabs[] = {"Game functions", "Lua", "Memory", "Native", "Log", "Features", "Other offsets",
                                  "Recorder",       "Spawn"};
    static const int kOrder[] = {5, 8, 7, 0, 1, 2, 3, 6, 4};  // shown order; S.tab / S.openTab keep the indexes above
    // 0.17.1: the main tab row is hidden (kept, not deleted): the panel shows the Features tab only. Flip
    // kShowMainTabs back to true to bring Spawn, Recorder, Game functions, Lua, Memory, Native, Other offsets and Log
    // back.
    constexpr bool kShowMainTabs = false;
    if (!kShowMainTabs) {
        S.tab = 5;
        FeaturesTab();
    } else if (ImGui::BeginTabBar("##tabs")) {
        for (int i : kOrder) {
            ImGuiTabItemFlags f = (S.openTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kTabs[i], nullptr, f)) {
                S.tab = i;
                switch (i) {
                    case 0: GameFunctionsTab(); break;
                    case 1: LuaTab(); break;
                    case 2: MemoryTab(); break;
                    case 3: NativeTab(); break;
                    case 4: LogTab(); break;
                    case 5: FeaturesTab(); break;
                    case 6: OtherOffsetsTab(); break;
                    case 7: RecorderTab(); break;
                    case 8: SpawnTab(); break;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    S.openTab = -1;
    TouchScroll();
    ImGui::End();
    ImGui::GetStyle() = saved;
}

}  // namespace lab
