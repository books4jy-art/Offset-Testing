// Offset Lab window, in the Sky Mesh Lab theme (near-black #0b0d12, white text at several strengths, thin white
// borders, sky blue accents).
#include "lab.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace lab {

namespace {

// ---------------------------------------------------------------- theme

const ImVec4 kSky400(0.220f, 0.741f, 0.973f, 1.f);  // #38bdf8
const ImVec4 kSky300(0.490f, 0.827f, 0.988f, 1.f);  // #7dd3fc
const ImVec4 kRed300(0.988f, 0.647f, 0.647f, 1.f);  // #fca5a5
const ImVec4 kAmber300(0.988f, 0.827f, 0.302f, 1.f);
inline ImVec4 White(float a) { return ImVec4(1.f, 1.f, 1.f, a); }
inline ImVec4 Sky(float a) { return ImVec4(kSky400.x, kSky400.y, kSky400.z, a); }

void ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
    s.PopupRounding = 8.f;
    s.GrabRounding = 6.f;
    s.TabRounding = 6.f;
    s.ScrollbarRounding = 8.f;
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 1.f;
    s.ItemSpacing = ImVec2(8.f, 6.f);
    s.FramePadding = ImVec2(10.f, 6.f);
    s.WindowPadding = ImVec2(12.f, 12.f);
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.043f, 0.051f, 0.071f, 0.97f);  // #0b0d12
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = White(0.02f);
    c[ImGuiCol_PopupBg] = ImVec4(0.067f, 0.078f, 0.102f, 0.98f);
    c[ImGuiCol_Text] = White(0.90f);
    c[ImGuiCol_TextDisabled] = White(0.42f);
    c[ImGuiCol_Border] = White(0.10f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = White(0.05f);
    c[ImGuiCol_FrameBgHovered] = White(0.08f);
    c[ImGuiCol_FrameBgActive] = Sky(0.18f);
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
    c[ImGuiCol_Button] = White(0.06f);
    c[ImGuiCol_ButtonHovered] = Sky(0.25f);
    c[ImGuiCol_ButtonActive] = Sky(0.45f);
    c[ImGuiCol_Header] = Sky(0.16f);
    c[ImGuiCol_HeaderHovered] = Sky(0.24f);
    c[ImGuiCol_HeaderActive] = Sky(0.36f);
    c[ImGuiCol_Separator] = White(0.10f);
    c[ImGuiCol_SeparatorHovered] = Sky(0.40f);
    c[ImGuiCol_SeparatorActive] = kSky400;
    c[ImGuiCol_ResizeGrip] = White(0.08f);
    c[ImGuiCol_ResizeGripHovered] = Sky(0.40f);
    c[ImGuiCol_ResizeGripActive] = kSky400;
    c[ImGuiCol_Tab] = White(0.04f);
    c[ImGuiCol_TabHovered] = Sky(0.28f);
    c[ImGuiCol_TabActive] = Sky(0.30f);
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
    ImGui::PushStyleColor(ImGuiCol_Text, White(0.45f));
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

// ---------------------------------------------------------------- shared state

struct State {
    // types
    bool namesLoaded = false;
    std::vector<std::string> names;  // read from the game
    int verified = 0, unreadable = 0;
    int area = -1;
    char search[96] = "";
    int selected = -1;
    std::map<int, std::vector<std::string>> values;  // per type: one value per setting
    bool serverTypes = false;                       // off at every start
    std::string lastResult;
    bool lastOk = true;
    // memory
    char memAddr[32] = "0";
    bool memAbsolute = false;
    int memRows = 16;
    // lua
    char lua[4096] = "-- Lua runs inside the game. Example:\n"
                     "local e = game:eventBarn():AddEventByMetaName(\"SetCameraShake\")\n"
                     "e:force(1.0)\ne:duration(2.0)\ne:Start()\n";
    std::vector<std::string> luaHistory;
    // functions
    char fnSearch[96] = "";
    char manualAddr[32] = "";
    std::map<uintptr_t, std::pair<uint64_t, double>> rate;  // offset -> (calls at last sample, calls/s)
    double lastSample = 0;
    int tab = 0;         // current tab
    int openTab = -1;    // tab to switch to on the next frame
} S;

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void LoadNames() {
    if (S.namesLoaded || !GameBase()) return;
    size_t n = 0;
    const LabType* t = Types(&n);
    S.names.resize(n);
    for (size_t i = 0; i < n; i++) {
        std::string s = GameString(t[i].nameAddr);
        if (s.empty()) {
            S.unreadable++;
            char b[32];
            snprintf(b, sizeof b, "<unreadable %zu>", i);
            S.names[i] = b;
            continue;
        }
        if (Fnv1a(s) == t[i].nameHash) S.verified++;
        S.names[i] = s;
    }
    S.namesLoaded = true;
    Log("Read %d type names (%d match 0.35.2, %d unreadable)", (int)n, S.verified, S.unreadable);
}

void OpenMemory(uintptr_t offset) {
    snprintf(S.memAddr, sizeof S.memAddr, "%" PRIXPTR, offset);
    S.memAbsolute = false;
    S.openTab = 2;
}

// ---------------------------------------------------------------- Types tab

std::string LuaValue(const std::string& v) {
    if (v == "true" || v == "false" || v == "nil") return v;
    char* end = nullptr;
    strtod(v.c_str(), &end);
    if (end && *end == 0 && !v.empty()) return v;
    if (!v.empty() && (v[0] == '{' || v.rfind("game", 0) == 0 || v[0] == '=')) return v[0] == '=' ? v.substr(1) : v;
    std::string q = "\"";
    for (char ch : v) {
        if (ch == '"' || ch == '\\') q.push_back('\\');
        q.push_back(ch);
    }
    return q + "\"";
}

std::string FireScript(int i, const std::vector<std::string>& fields) {
    std::string s = "local e = game:eventBarn():AddEventByMetaName(\"" + S.names[i] + "\")\n";
    s += "if e == nil then error(\"not an event\") end\n";
    auto& vals = S.values[i];
    for (size_t k = 0; k < fields.size() && k < vals.size(); k++)
        if (!vals[k].empty()) s += "e:" + fields[k] + "(" + LuaValue(vals[k]) + ")\n";
    return s + "e:Start()\n";
}

void TypeDetails(int i) {
    size_t n = 0;
    const LabType& t = Types(&n)[i];
    ImGui::TextColored(kSky300, "%s", S.names[i].c_str());
    ImGui::SameLine();
    Dim("%s", kAreaNames[t.area]);
    if (GameString(t.nameAddr).empty() || Fnv1a(S.names[i]) != t.nameHash)
        ImGui::TextColored(kAmber300, "Name doesn't match Sky 0.35.2: these offsets may be wrong for this game version.");

    if (ImGui::BeginTable("##off", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto row = [](const char* k, uintptr_t v, bool link) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            Dim("%s", k);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", v ? Hex(v).c_str() : "-");
            if (link && v) {
                ImGui::SameLine();
                ImGui::PushID(k);
                if (ImGui::SmallButton("view")) OpenMemory(v);
                ImGui::PopID();
            }
        };
        row("Registered at", t.registerAt, true);
        row("Type object", t.typeObject, true);
        row("Name string", t.nameAddr, true);
        ImGui::EndTable();
    }

    if (t.noteCount) {
        ImGui::Spacing();
        Eyebrow("GAME NOTES");
        for (int k = 0; k < t.noteCount; k++) {
            std::string note = GameString(NoteAddr(t.noteFirst + k));
            if (note.size() >= 2 && note.front() == '"' && note.back() == '"') note = note.substr(1, note.size() - 2);
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", note.c_str());
        }
    }

    std::vector<std::string> fields;
    for (int k = 0; k < t.fieldCount; k++) fields.push_back(GameString(FieldNameAddr(t.fieldFirst + k)));
    auto& vals = S.values[i];
    vals.resize(fields.size());

    ImGui::Spacing();
    Eyebrow("SETTINGS");
    if (fields.empty()) Dim("No settings found next to this type.");
    Dim("Leave empty to skip. true / false, numbers, text, {tables}; start with = for raw Lua.");
    for (size_t k = 0; k < fields.size(); k++) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", vals[k].c_str());
        ImGui::PushID((int)k);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (ImGui::InputTextWithHint("##v", "value", buf, sizeof buf)) vals[k] = buf;
        ImGui::SameLine();
        ImGui::TextUnformatted(fields[k].c_str());
        ImGui::PopID();
    }

    ImGui::Spacing();
    bool server = t.area == kAreaServer;
    bool blocked = server && !S.serverTypes;
    if (blocked) {
        ImGui::TextColored(kAmber300, "Server type: turn on \"Server types\" above to fire it.");
        ImGui::BeginDisabled();
    }
    if (AccentButton("Fire event", ImVec2(160, 0))) {
        std::string script = FireScript(i, fields);
        int rc = RunLua(script);
        S.lastOk = rc == 0;
        S.lastResult = rc == 0 ? "Fired " + S.names[i]
                     : rc == -1 ? "The game isn't ready (no game object yet)"
                                : "The game refused it (code " + std::to_string(rc) + "): not an event, or a setting is wrong";
        Log("%s: %s", S.names[i].c_str(), S.lastResult.c_str());
    }
    if (blocked) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Copy as Lua")) {
        std::string script = FireScript(i, fields);
        snprintf(S.lua, sizeof S.lua, "%s", script.c_str());
        S.openTab = 1;
    }
    if (!S.lastResult.empty()) ImGui::TextColored(S.lastOk ? kSky300 : kRed300, "%s", S.lastResult.c_str());
}

void TypesTab() {
    LoadNames();
    if (!S.namesLoaded) {
        Dim("Waiting for libBootloader.so...");
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

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    const char* areaLabel = S.area < 0 ? "All areas" : kAreaNames[S.area];
    if (ImGui::BeginCombo("##area", areaLabel)) {
        if (ImGui::Selectable("All areas", S.area < 0)) S.area = -1;
        for (int a = 0; a < kAreaCount; a++)
            if (ImGui::Selectable(kAreaNames[a], S.area == a)) S.area = a;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search names or offsets", S.search, sizeof S.search);

    std::vector<int> rows;
    uintptr_t want = 0;
    bool hexSearch = S.search[0] == '0' && (S.search[1] == 'x' || S.search[1] == 'X') && ParseHex(S.search + 2, want);
    for (size_t i = 0; i < n; i++) {
        if (S.area >= 0 && t[i].area != S.area) continue;
        if (hexSearch ? (t[i].registerAt != want && t[i].typeObject != want) : !Contains(S.names[i], S.search)) continue;
        rows.push_back((int)i);
    }
    Dim("%zu of %zu types  ·  names checked: %d match 0.35.2, %d unreadable", rows.size(), n, S.verified, S.unreadable);

    float listH = ImGui::GetContentRegionAvail().y * 0.42f;
    if (ImGui::BeginChild("##list", ImVec2(0, listH), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                int i = rows[r];
                ImGui::PushID(i);
                if (ImGui::Selectable(S.names[i].c_str(), S.selected == i)) {
                    S.selected = i;
                    S.lastResult.clear();
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.62f + ImGui::GetCursorStartPos().x);
                ImGui::TextColored(t[i].area == kAreaServer ? kAmber300 : White(0.35f), "%s", kAreaNames[t[i].area]);
                ImGui::PopID();
            }
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        if (S.selected >= 0) TypeDetails(S.selected);
        else Dim("Pick a type to see its offsets, the game's notes and its settings, and to fire it.");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Lua tab

void LuaTab() {
    Dim("Runs in the game's Lua. The result is the game's code: 0 = ran without an error.");
    ImGui::InputTextMultiline("##lua", S.lua, sizeof S.lua, ImVec2(-1, ImGui::GetContentRegionAvail().y * 0.55f));
    if (AccentButton("Run", ImVec2(120, 0))) {
        int rc = RunLua(S.lua);
        S.luaHistory.insert(S.luaHistory.begin(), std::string(rc == 0 ? "ok  " : rc == -1 ? "not ready  " : "error  ") +
                                                      std::string(S.lua).substr(0, 80));
        if (S.luaHistory.size() > 30) S.luaHistory.pop_back();
        Log("Lua: %s", rc == 0 ? "ok" : rc == -1 ? "game not ready" : "error");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) S.lua[0] = 0;
    ImGui::Spacing();
    Eyebrow("HISTORY");
    if (ImGui::BeginChild("##hist", ImVec2(0, 0), ImGuiChildFlags_Border))
        for (auto& h : S.luaHistory) ImGui::TextUnformatted(h.c_str());
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Memory tab

void MemoryTab() {
    uintptr_t base = GameBase();
    Dim("Read-only. Offsets are from the start of libBootloader.so (%s) unless Absolute is on.", Hex(base).c_str());
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##addr", "hex offset", S.memAddr, sizeof S.memAddr, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::Checkbox("Absolute", &S.memAbsolute);
    ImGui::SameLine();
    if (ImGui::Button("Game object")) {
        uintptr_t g = GameObject();
        snprintf(S.memAddr, sizeof S.memAddr, "%" PRIXPTR, g);
        S.memAbsolute = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("rows", &S.memRows, 4, 64);

    uintptr_t a = 0;
    if (!ParseHex(S.memAddr, a)) return;
    if (!S.memAbsolute) a += base;
    std::vector<uint8_t> bytes(S.memRows * 16);
    std::vector<bool> ok(bytes.size(), false);
    for (int r = 0; r < S.memRows; r++)
        if (ReadMemory(a + r * 16, &bytes[r * 16], 16)) std::fill(ok.begin() + r * 16, ok.begin() + r * 16 + 16, true);

    uint64_t q = 0;
    double dval = 0;
    float fval = 0;
    if (ReadMemory(a, &q, 8)) {
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
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Functions tab

void FunctionsTab() {
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
    Dim("Counts every call and keeps the first four arguments (x0-x3). Watching a busy function can slow the game.");
    if (ImGui::BeginTable("##w", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 160))) {
        ImGui::TableSetupColumn("Function");
        ImGui::TableSetupColumn("Calls");
        ImGui::TableSetupColumn("/s");
        ImGui::TableSetupColumn("x0 / x1 / x2 / x3");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (auto& w : watches) {
            ImGui::TableNextRow();
            ImGui::PushID((int)w.offset);
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s %s", Hex(w.offset).c_str(), w.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%" PRIu64, w.calls);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.0f", S.rate[w.offset].second);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%" PRIX64 " %" PRIX64 " %" PRIX64 " %" PRIX64, w.args[0], w.args[1], w.args[2], w.args[3]);
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("stop")) StopWatch(w.offset);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##manual", "any hex offset", S.manualAddr, sizeof S.manualAddr,
                             ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Watch offset")) {
        uintptr_t off = 0;
        std::string err;
        if (ParseHex(S.manualAddr, off) && !StartWatch(off, "(manual)", err)) Log("Watch %s: %s", Hex(off).c_str(), err.c_str());
    }
    ImGui::SameLine();
    Dim("Must be the start of a function, or the game will crash.");

    ImGui::Spacing();
    Eyebrow("EXPORTED FUNCTIONS");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##fs", "Search exported functions", S.fnSearch, sizeof S.fnSearch);
    std::vector<int> rows;
    for (size_t i = 0; i < fns.size(); i++)
        if (Contains(fns[i].name, S.fnSearch)) rows.push_back((int)i);
    Dim("%zu of %zu functions (read from the game's symbol table)", rows.size(), fns.size());
    if (ImGui::BeginChild("##fns", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const auto& f = fns[rows[r]];
                ImGui::PushID(rows[r]);
                if (ImGui::SmallButton("watch")) {
                    std::string err;
                    if (!StartWatch(f.offset, f.name.substr(0, 60), err)) Log("Watch %s: %s", Hex(f.offset).c_str(), err.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("view")) OpenMemory(f.offset);
                ImGui::SameLine();
                ImGui::TextColored(kSky300, "%s", Hex(f.offset).c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(f.name.c_str());
                ImGui::PopID();
            }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Log tab

void LogTab() {
    if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Border)) {
        for (auto& l : LogLines()) ImGui::TextWrapped("%s", l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

}  // namespace

void Draw(bool* open) {
    (void)open;
    ApplyTheme();
    ImGui::SetWindowSize(ImVec2(900, 1100), ImGuiCond_FirstUseEver);

    Eyebrow("SKY MESH LAB");
    ImGui::SameLine();
    Dim("Offset Lab · Sky 0.35.2 · experimental");
    uintptr_t base = GameBase(), game = GameObject();
    ImGui::TextColored(base ? kSky300 : kRed300, "%s", base ? "libBootloader.so loaded" : "libBootloader.so not found");
    ImGui::SameLine();
    Dim("at %s  ·  game object %s", Hex(base).c_str(), game ? Hex(game).c_str() : "not ready");
    ImGui::Separator();

    static const char* kTabs[] = {"Types", "Lua", "Memory", "Functions", "Log"};
    if (ImGui::BeginTabBar("##tabs")) {
        for (int i = 0; i < 5; i++) {
            ImGuiTabItemFlags f = (S.openTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kTabs[i], nullptr, f)) {
                S.tab = i;
                switch (i) {
                    case 0: TypesTab(); break;
                    case 1: LuaTab(); break;
                    case 2: MemoryTab(); break;
                    case 3: FunctionsTab(); break;
                    case 4: LogTab(); break;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    S.openTab = -1;
}

}  // namespace lab
