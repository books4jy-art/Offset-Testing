// Spawn tab: creatures and effects placed next to you, and the developers' fish school switch.
//
// Creatures come from the game's own developer code: its fish school barn ("FishBarn") has a test school that its
// debug window ("Fish Schools": Create Test Birds / Mantas / ... / Clear Test School) creates in front of the camera.
// The tab calls that same function on the game's main thread, so birds, mantas, butterflies, jellyfish, crabs, sky
// kids and fish are made exactly as the developers' buttons make them. Shards and the darkstone rain are the game's
// own events started from Lua, with markers placed around you. Everything the tab makes is local to your game and is
// dropped by the game when the level changes.
#include "lab.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

namespace lab {

namespace {

const ImVec4 kAmber300(0.988f, 0.827f, 0.302f, 1.f);
const ImVec4 kSky300(0.490f, 0.827f, 0.988f, 1.f);

void Dim(const char* fmt, ...) IM_FMTARGS(1);
void Dim(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.70f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

void Section(const char* title, const char* what) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    if (what && *what) Dim("%s", what);
}

// A row that can't be offered: greyed out, with the reason.
void NotOffered(const char* icon, const char* label, const char* why) {
    float sz = ImGui::GetFrameHeight();
    if (!GameIcon(icon ? icon : "", sz)) ImGui::Dummy(ImVec2(sz, sz));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button(label, ImVec2(-1, 0));
    ImGui::EndDisabled();
    ImGui::TextColored(kAmber300, "Not offered.");
    Dim("%s", why);
}

int IntSlider(const char* id, int* v, int lo, int hi, const char* fmt) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt(id, v, lo, hi, fmt);
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    return ImGui::IsItemDeactivatedAfterEdit();
}

void FloatSlider(const char* id, float* v, float lo, float hi, const char* fmt) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat(id, v, lo, hi, fmt);
    if (!(*v >= lo)) *v = lo;
    if (*v > hi) *v = hi;
}

bool Sane(float v) { return std::isfinite(v) && std::fabs(v) < 100000.f; }

// ---------------------------------------------------------------- the fish school barn (Sky 0.35.2)
//
// Game +0x4C8 (Game reflection record "fishBarn", built at 0x17C58D8) -> its Lua object; +0x10 -> the FishBarn (every
// wrapper the game calls through it does ldr x0, [x0, #0x10] first: 0x173BC60, 0x173BCB8...).
constexpr uintptr_t kGameFishBarn = 0x4C8;
constexpr uintptr_t kWrapBarn = 0x10;
// FishBarn: the camera it spawns in front of (+0x50 -> +0x10, read at 0x173C338) and the test school (+0x19B58).
constexpr uintptr_t kBarnCamera = 0x50;
constexpr uintptr_t kBarnTestSchool = 0x19B58;
constexpr uintptr_t kSchoolType = 0x84;  // the school's FishSchoolType (byte; read at 0x174B8B8)
// CreateTestSchool(barn, FishSchoolType, ball) at 0x173C14C: releases the test school there is (0x17492FC), fills the
// school settings (count 10: mov w8, #0xa at 0x173C1B8; ball: 200), puts the centre 5 m in front of the camera
// (fmov v1.4s, #5.0 at 0x173C364), creates the school (0x174F9FC) and keeps it at +0x19B58. The debug window calls
// it for each "Create Test ..." button (0x173F940...0x1740C80). ReleaseSchool(barn, school) at 0x17492FC is what its
// "Clear Test School" calls (0x1740E2C), then it zeroes +0x19B58. On a level unload the barn releases every school and
// zeroes +0x19B58 itself (OnLevelUnload 0x173BCA4 -> 0x174A308, 0x174A3DC).
constexpr uint32_t kCreateTestSchool = 0x173C14C, kCreateTestSchoolFirst = 0xD10603FFu;  // sub sp, sp, #0x180
constexpr uint32_t kReleaseSchool = 0x17492FC, kReleaseSchoolFirst = 0xD10143FFu;        // sub sp, sp, #0x50
constexpr uint32_t kCountWord = 0x173C1B8, kCountOriginal = 0x52800148u;  // mov w8, #0xa
constexpr uint32_t kDistWord = 0x173C364, kDistOriginal = 0x4F00F681u;    // fmov v1.4s, #5.0
// The developers' "Fish Schools" debug window: the barn's update (0x17560D0) calls it (0x173F6E4, at 0x1756898) when
// this byte is set (kFishSchoolDebug in TheVoid's table; also read at 0x1754460 to draw each school's debug shapes).
constexpr uint32_t kFishSchoolDebug = 0x3197898;

struct SchoolKind { const char* label; const char* icon; uint8_t type; bool ball; const char* note; };
// In the debug window's order; types are the game's FishSchoolType values (registered at 0x173CE80...).
const SchoolKind kSchools[] = {
    {"Birds", "UiEmoteCallBird", 2, false, "a flock of birds"},
    {"Mantas", "UiEmoteCallManta", 3, false, "light mantas"},
    {"Ghost mantas", "UiEmoteCallGhostManta", 8, false, ""},
    {"Butterflies", "UiEmoteButterfly", 4, false, "light butterflies"},
    {"Jellyfish", "UiEmoteCallJellyfish", 5, false, ""},
    {"Sky kids flying", "UiOutfitPropSkykidPlushie", 6, false, "children of the light, flying in a flock"},
    {"Sky kids abstract", "UiOutfitPropSkykidPlushie", 7, false, ""},
    {"Serow", "UiOutfitMaskAP18Serow", 9, false, "the masked serow (Season of the Nine-Coloured Deer)"},
    {"Crabs", "UiEmoteCallCrab", 14, false, ""},
    {"Fish ball", "UiEmoteCallFish", 100, true, "a ball of 200 butcher-knife fish (the game's own count)"},
    {"Angel fish", "UiEmoteCallFish", 105, false, ""},
    {"Angler fish", "UiEmoteCallFish", 106, false, ""},
    {"Barracuda", "UiEmoteCallFish", 107, false, ""},
    {"Butterfly fish", "UiEmoteCallFish", 108, false, ""},
    {"Mola mola", "UiEmoteCallFish", 109, false, ""},
    {"Original fish", "UiEmoteCallFish", 110, false, ""},
    {"Sail fish", "UiEmoteCallFish", 111, false, ""},
    {"Tuna", "UiEmoteCallFish", 112, false, ""},
};

const char* SchoolName(int type) {
    for (const auto& k : kSchools)
        if (k.type == type) return k.label;
    return nullptr;
}

template <class V> bool Rd(uintptr_t a, V& v) { return a && ReadMemory(a, &v, sizeof v); }

uintptr_t FishBarn() {
    uintptr_t game = GameObject(), wrap = 0, barn = 0;
    if (!game || !Rd(game + kGameFishBarn, wrap) || !wrap || !Rd(wrap + kWrapBarn, barn)) return 0;
    return barn;
}

// The 8-bit immediate of an AArch64 FMOV (vector) for v, or -1 (VFPExpandImm: 1.efgh x 2^(NOT b:bbbbb:cd - 127)).
int FmovImm8(float v) {
    for (int imm = 0; imm < 256; imm++) {
        int a = imm >> 7 & 1, b = imm >> 6 & 1, cd = imm >> 4 & 3, mant = imm & 15;
        int exp = ((!b) << 7) | (b ? 0x7C : 0) | cd;
        float f = std::ldexp(1.f + mant / 16.f, exp - 127) * (a ? -1.f : 1.f);
        if (f == v) return imm;
    }
    return -1;
}

uint32_t FmovWord(float v) {  // fmov v1.4s, #v (the original's register and form), 0 if v can't be encoded
    int imm = FmovImm8(v);
    if (imm < 0) return 0;
    return 0x4F00F401u | (uint32_t)((imm >> 5) & 7) << 16 | (uint32_t)(imm & 31) << 5;
}

// What the game-thread call is asked to do and what it found (read by SpawnTick on the window's thread).
std::atomic<int> gReqType{0}, gReqBall{0}, gReqCount{10}, gReqDist{5};
std::atomic<int> gResult{0};  // 0 none, 1 spawned, 2 cleared, 3 nothing to clear, <0 problems (see SpawnTick)

bool WordIs(uint32_t off, uint32_t want) {
    uint32_t w = 0;
    return ReadMemory(GameBase() + off, &w, 4) && w == want;
}

// Runs on the game's main thread (RunOnGameThread): op 1 = create the test school, 2 = clear it.
void SchoolCall(uintptr_t op) {
    uintptr_t base = GameBase(), barn = FishBarn();
    if (!base || !barn) { gResult = -1; return; }
    if (!WordIs(kCreateTestSchool, kCreateTestSchoolFirst) || !WordIs(kReleaseSchool, kReleaseSchoolFirst)) {
        gResult = -2;
        return;
    }
    if (op == 2) {
        uintptr_t school = 0;
        if (!Rd(barn + kBarnTestSchool, school)) { gResult = -1; return; }
        if (!school) { gResult = 3; return; }
        reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(base + kReleaseSchool)(barn, school);
        *reinterpret_cast<volatile uintptr_t*>(barn + kBarnTestSchool) = 0;  // as the game's Clear button does
        gResult = 2;
        return;
    }
    uintptr_t cam = 0, cam2 = 0;
    if (!Rd(barn + kBarnCamera, cam) || !cam || !Rd(cam + 0x10, cam2) || !cam2) { gResult = -3; return; }
    if (!WordIs(kCountWord, kCountOriginal) || !WordIs(kDistWord, kDistOriginal)) { gResult = -2; return; }
    int count = gReqCount, dist = gReqDist;
    uint32_t countWord = 0x52800008u | (uint32_t)(count & 0xFFFF) << 5;  // mov w8, #count
    uint32_t distWord = FmovWord((float)dist);
    bool patched = count != 10 || dist != 5;
    if (patched && (!distWord || !WriteGameMemory(kCountWord, &countWord, 4) || !WriteGameMemory(kDistWord, &distWord, 4))) {
        WriteGameMemory(kCountWord, &kCountOriginal, 4);
        WriteGameMemory(kDistWord, &kDistOriginal, 4);
        gResult = -4;
        return;
    }
    reinterpret_cast<void (*)(uintptr_t, uint32_t, uint32_t)>(base + kCreateTestSchool)(barn, (uint32_t)gReqType,
                                                                                         (uint32_t)gReqBall);
    if (patched) {  // put both words back right away: the game's own calls (the school following you) get its values
        WriteGameMemory(kCountWord, &kCountOriginal, 4);
        WriteGameMemory(kDistWord, &kDistOriginal, 4);
    }
    uintptr_t school = 0;
    gResult = Rd(barn + kBarnTestSchool, school) && school ? 1 : -5;
}

// ---------------------------------------------------------------- state

struct State {
    int school = 0;          // index into kSchools
    int count = 10;          // creatures in the school (1-60; the game's own is 10)
    int dist = 5;            // metres in front of the camera (2-31: what the instruction can hold; the game's is 5)
    std::string lastWhat;    // for the result line
    bool debugOn = false;    // the developers' window switch, as last set
    // shards
    int shardCount = 4;      // markers in the clump (1-8)
    float shardRadius = 12;  // metres around you
    bool shardOn = false;
    int starCount = 6;       // start positions (1-8)
    float starHeight = 60, starRadius = 20, starSpeed = 30;
    float starColor[3] = {0.1f, 0.7f, 1.0f};
    int starsThisLevel = 0;
    int shardsThisLevel = 0;  // StarFallShowers started (they end by themselves; their markers stay until level change)
    float rainRadius = 20, rainRate = 300;
    bool rainDrain = false;
    bool rainOn = false;
    std::string level;       // level the shard/rain flags belong to
} T;

constexpr int kMaxStarShowers = 6;  // per level: each keeps up to 8 markers until the level changes
constexpr int kMaxShardFalls = 8;   // per level, the same

bool Queue(const char* what, uintptr_t op) {
    if (!GameObject()) { Log("%s: enter the world first", what); return false; }
    if (!RunOnGameThread(SchoolCall, op)) {
        Log("%s: the game's frame hook isn't placed (see the Log); nothing done", what);
        return false;
    }
    T.lastWhat = what;
    return true;
}

void SpawnSchool(const SchoolKind& k) {
    gReqType = k.type;
    gReqBall = k.ball ? 1 : 0;
    gReqCount = T.count;
    gReqDist = T.dist;
    Queue(k.label, 1);
}

bool SetDebugSwitch(bool on) {
    uint8_t cur = 0;
    if (!GameBase() || !ReadMemory(GameBase() + kFishSchoolDebug, &cur, 1) || cur > 1) {
        Log("Fish school debug: the switch at 0x%X isn't 0 or 1 (%u); not changed", kFishSchoolDebug, cur);
        return false;
    }
    uint8_t v = on ? 1 : 0;
    if (cur != v && !WriteGameMemory(kFishSchoolDebug, &v, 1)) return false;
    Log("Fish school debug window %s", on ? "on" : "off");
    return true;
}

// ---------------------------------------------------------------- Lua: markers, clumps, events

std::string F3(const float p[3]) {
    char b[96];
    snprintf(b, sizeof b, "{%.3f, %.3f, %.3f}", p[0], p[1], p[2]);
    return b;
}

// Points on a ring around you (n == 1: one point in front, or +z when your facing isn't known).
bool RingAround(int n, float radius, float dy, std::vector<std::vector<float>>& out) {
    float me[3], yaw = 0;
    if (!LocalAvatarPos(me, &yaw)) return false;
    if (!std::isfinite(yaw) || std::fabs(yaw) > 100.f) yaw = 0;
    out.clear();
    for (int i = 0; i < n; i++) {
        float a = n == 1 ? yaw : yaw + 6.2831853f * i / n;
        std::vector<float> p = {me[0] + radius * std::sin(a), me[1] + dy, me[2] + radius * std::cos(a)};
        if (!Sane(p[0]) || !Sane(p[1]) || !Sane(p[2])) return false;
        out.push_back(p);
    }
    return true;
}

// Lua that makes markers at the points and a clump holding them, kept in OffsetLab[slot] = {c = clump, m = {...}}.
// Clump: CreateClumpWithSize(n) (ClumpBarn, 0x14C06A0); SetAt(index, object) takes 0..Size()-1 (0x14BD1B0 aborts the
// game on an index outside that, so the loop runs to c:Size() - 1 only).
std::string ClumpLua(const char* slot, const std::vector<std::vector<float>>& pts) {
    std::string s = "local mb, cb = game:markerBarn(), game:clumpBarn()\n"
                    "local c = cb:CreateClumpWithSize(" + std::to_string(pts.size()) + ")\n"
                    "if not c then error(\"no clump\") end\n"
                    "local t = {c = c, m = {}}\nOffsetLab[\"" + std::string(slot) + "\"] = t\n"
                    "local P = {";
    for (size_t i = 0; i < pts.size(); i++) {
        float p[3] = {pts[i][0], pts[i][1], pts[i][2]};
        s += (i ? ", " : "") + F3(p);
    }
    s += "}\n"
         "local n = c:Size()\n"
         "if n > #P then n = #P end\n"
         "if n < 1 then error(\"empty clump\") end\n"
         "for i = 0, n - 1 do local m = mb:CreateMarker(0); m:pos(P[i + 1]); t.m[#t.m + 1] = m; c:SetAt(i, m) end\n";
    return s;
}

bool Run(const std::string& what, const std::string& script, const char* key) {
    int r = RunLua(script, what, key);
    if (r == -1) Log("%s: enter the world first", what.c_str());
    else if (r == -3) Log("%s: the game isn't running frames right now", what.c_str());
    else if (r != 0) Log("%s: the game reported a script error (%d)", what.c_str(), r);
    else Log("%s", what.c_str());
    return r == 0;
}

// Shard fall: ManualShardFall (its spawnPositions: a clump of markers). On start (0x19C963C) it reads each marker's
// position, makes one ShardFall event with that many shards (count at +0x60), keeps it at +0x30 and starts it; switched
// off it stops that ShardFall (0x19CADF8). Neither stops by itself, so it's kept as "spShard" and switched off by Remove.
// The clump and markers aren't released: the switched-off event still points at the clump (+0x28). They're few
// (at most kMaxShardFalls x 8 markers per level) and the game deletes them with the level.
void ShardFall() {
    if (T.shardsThisLevel >= kMaxShardFalls) {
        Log("Shard fall: %d in this level already (their markers stay until the level changes)", kMaxShardFalls);
        return;
    }
    std::vector<std::vector<float>> pts;
    if (!RingAround(T.shardCount, T.shardRadius, 0.f, pts)) { Log("Shard fall: your position couldn't be read"); return; }
    std::string ev = FeatureEventLua("spShard", "ManualShardFall", {{"spawnPositions", "c"}});
    if (ev.empty()) return;
    std::string slot = "spShardObj" + std::to_string(T.shardsThisLevel);
    std::string s = std::string(FeatureLuaPrelude()) + "stop(\"spShard\")\n" + "do\n" + ClumpLua(slot.c_str(), pts) +
                    ev + "end\n";
    if (Run("Shard fall: " + std::to_string(pts.size()) + " shards around you", s, "spShard")) {
        T.shardOn = true;
        T.shardsThisLevel++;
    }
}

void ShardFallOff(bool quiet = false) {
    std::string s = std::string(FeatureLuaPrelude()) + "stop(\"spShard\")\n";
    int r = RunLua(s, "Shard fall off", "spShard");
    if (!quiet && r == 0) Log("Shard fall removed");
    T.shardOn = false;
}

// Star fall shower: StarFallShower with its startPositions clump above you, rail nil (its start checks: 0x205DBDC).
// It stops itself once every start position's star has landed (0x205E5AC), so it's a one-shot: never kept or switched
// off, and its markers (which it reads every frame) are never released by the mod; the level change drops them.
void StarShower() {
    if (T.starsThisLevel >= kMaxStarShowers) {
        Log("Star fall: %d showers in this level already (their markers stay until the level changes)", kMaxStarShowers);
        return;
    }
    std::vector<std::vector<float>> pts;
    if (!RingAround(T.starCount, T.starRadius, T.starHeight, pts)) { Log("Star fall: your position couldn't be read"); return; }
    char b[64];
    auto num = [&](float v) { snprintf(b, sizeof b, "%.4f", v); return std::string(b); };
    float killY = pts[0][1] - T.starHeight - 60.f;
    std::string col = "{" + num(T.starColor[0]) + ", " + num(T.starColor[1]) + ", " + num(T.starColor[2]) + "}";
    std::string ev = FeatureEventLua("", "StarFallShower",
                                     {{"startPositions", "c"}, {"cooldown", "0.4"}, {"cooldownRandRange", "0.4"},
                                      {"killY", num(killY)}, {"speed", num(T.starSpeed)}, {"speedRandRange", "5"},
                                      {"dirRandRange", "0.2"}, {"rail", "nil"}, {"intensity", "2"},
                                      {"lightIntensityMult", "1"}, {"color", col}, {"flareSize", "2"},
                                      {"randomSeed", std::to_string(7 + T.starsThisLevel)}, {"onFinalImpact", "nil"},
                                      {"killOnGroundCollision", "true"}});
    if (ev.empty()) return;
    std::string slot = "spStar" + std::to_string(T.starsThisLevel);
    std::string s = std::string(FeatureLuaPrelude()) + "do\n" + ClumpLua(slot.c_str(), pts) + ev + "end\n";
    if (Run("Star fall: " + std::to_string(pts.size()) + " stars above you", s, "spStar")) T.starsThisLevel++;
}

// Darkstone rain: DarkstoneRainRegion with its region marker on you (scaled to the radius). Its update (0x15CEAA8)
// checks the region and your avatar and never stops itself: kept as "spRain", switched off by Remove. The one marker
// is kept and moved for the next rain (a switched-off event may still point at it); the level change deletes it.
void Rain() {
    float me[3];
    if (!LocalAvatarPos(me)) { Log("Darkstone rain: your position couldn't be read"); return; }
    char b[160];
    snprintf(b, sizeof b, "{%.3f, %.3f, %.3f}", T.rainRadius, T.rainRadius, T.rainRadius);
    std::string scale = b;
    snprintf(b, sizeof b, "%.1f", T.rainRate);
    std::string ev = FeatureEventLua("spRain", "DarkstoneRainRegion",
                                     {{"region", "OffsetLab.spRainM"}, {"dropRatePerSecond", b},
                                      {"playerEnergyDrain", T.rainDrain ? "0.05" : "0"},
                                      {"dropCollectibles", "false"}});
    if (ev.empty()) return;
    std::string s = std::string(FeatureLuaPrelude()) + "stop(\"spRain\")\n" +
                    "OffsetLab.spRainM = OffsetLab.spRainM or game:markerBarn():CreateMarker(0)\n" +
                    "OffsetLab.spRainM:pos(" + F3(me) + ")\n" + "pcall(function() OffsetLab.spRainM:scale(" + scale +
                    ") end)\n" + ev;
    if (Run("Darkstone rain around you", s, "spRain")) T.rainOn = true;
}

void RainOff(bool quiet = false) {
    std::string s = std::string(FeatureLuaPrelude()) + "stop(\"spRain\")\n";
    int r = RunLua(s, "Darkstone rain off", "spRain");
    if (!quiet && r == 0) Log("Darkstone rain removed");
    T.rainOn = false;
}

}  // namespace

void SpawnTick() {
    int r = gResult.exchange(0);
    if (r) {
        const char* w = T.lastWhat.c_str();
        switch (r) {
            case 1: Log("Spawn: %s (the game's test school)", w); break;
            case 2: Log("Spawn: test school removed"); break;
            case 3: Log("Spawn: no test school to remove"); break;
            case -1: Log("Spawn %s: the fish school barn isn't there (enter the world first)", w); break;
            case -2: Log("Spawn %s: the game code isn't what 0.35.2 has; nothing called", w); break;
            case -3: Log("Spawn %s: the barn has no camera yet; try again in the world", w); break;
            case -4: Log("Spawn %s: couldn't set the count / distance; nothing spawned", w); break;
            case -5: Log("Spawn %s: the game made no school (it may not load that kind here)", w); break;
        }
    }
    // the game drops level objects on a level change: forget what was kept for the old level
    std::string lvl = CurrentLevelId();
    if (!lvl.empty() && lvl != T.level) {
        T.level = lvl;
        T.shardOn = T.rainOn = false;
        T.starsThisLevel = T.shardsThisLevel = 0;
    }
}

void SpawnTab() {
    bool inWorld = GameObject() != 0;
    if (!inWorld) ImGui::TextColored(kAmber300, "Enter the world to use these.");
    Dim("Creatures and effects next to you, local to your game. The game removes them all when the level changes. "
        "Results show in the Log tab.");
    if (ImGui::Button("Remove all spawned", ImVec2(-1, Dp(44)))) {
        Queue("Remove all", 2);
        ShardFallOff(true);
        RainOff(true);
        Log("Spawn: removing everything that can be removed (star showers end by themselves)");
    }
    if (ImGui::BeginChild("##spawn", ImVec2(0, 0), ImGuiChildFlags_None)) {
        // ---- creatures (the developers' test school)
        Section("CREATURES",
                "The game's own developer spawner (its fish school barn's test school, the \"Create Test ...\" buttons "
                "of its Fish Schools debug window). One school at a time: a new one replaces the last. It appears in "
                "front of the camera and moves on with you (the game re-makes it, with its own count, after you've "
                "gone 30 m).");
        {
            uintptr_t barn = FishBarn(), school = 0;
            uint8_t type = 0;
            if (barn && Rd(barn + kBarnTestSchool, school) && school && Rd(school + kSchoolType, type)) {
                const char* n = SchoolName(type);
                ImGui::TextColored(kSky300, "Now: %s", n ? n : "a school");
            } else {
                Dim("Now: none");
            }
        }
        ImGui::TextUnformatted("Creatures in the school");
        IntSlider("##cnt", &T.count, 1, 60, "%d");
        ImGui::TextUnformatted("Distance in front of the camera");
        IntSlider("##dst", &T.dist, 2, 31, "%d m");
        for (int i = 0; i < (int)IM_ARRAYSIZE(kSchools); i++) {
            const SchoolKind& k = kSchools[i];
            float sz = ImGui::GetFrameHeight();
            ImGui::PushID(i);
            if (!GameIcon(k.icon, sz)) ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            if (ImGui::Button(k.label, ImVec2(-1, 0))) SpawnSchool(k);
            if (*k.note && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k.note);
            ImGui::PopID();
        }
        if (ImGui::Button("Remove creatures", ImVec2(-1, 0))) Queue("Remove creatures", 2);
        Dim("Fish ball always has the game's 200 fish. Count and distance are two instructions of the spawner, set "
            "for that one call and put straight back.");

        // ---- shards
        Section("SHARDS", "The game's falling dark shards, a star shower and darkstone rain, started as local events (none "
                          "is a server request type; pickups are switched off).");
        float sz = ImGui::GetFrameHeight();
        if (!GameIcon("UiMapStar", sz)) ImGui::Dummy(ImVec2(sz, sz));
        ImGui::SameLine();
        ImGui::TextUnformatted("Shard fall (experimental)");
        ImGui::TextUnformatted("Shards");
        IntSlider("##shn", &T.shardCount, 1, 8, "%d");
        ImGui::TextUnformatted("Around you");
        FloatSlider("##shr", &T.shardRadius, 3.f, 60.f, "%.0f m");
        if (ImGui::Button("Drop shards##sf", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 0))) ShardFall();
        ImGui::SameLine();
        ImGui::BeginDisabled(!T.shardOn);
        if (ImGui::Button("Remove##sf", ImVec2(-1, 0))) ShardFallOff();
        ImGui::EndDisabled();
        Dim("ManualShardFall with a clump of markers on a ring around you; it makes one ShardFall (the eruption's "
            "falling shards) at those points. They land with the game's own impact effects (shake, rumble); ShardFall also checks where your avatar is "
            "(0x11B2BC4), and what it does near you isn't verified. Remove "
            "switches it off (its markers stay until the level changes). At most %d per level (%d this level).",
            kMaxShardFalls, T.shardsThisLevel);

        ImGui::Spacing();
        if (!GameIcon("UiHudStarSplit0", sz)) ImGui::Dummy(ImVec2(sz, sz));
        ImGui::SameLine();
        ImGui::TextUnformatted("Star fall (experimental)");
        ImGui::TextUnformatted("Stars");
        IntSlider("##stn", &T.starCount, 1, 8, "%d");
        ImGui::TextUnformatted("Height above you");
        FloatSlider("##sth", &T.starHeight, 10.f, 150.f, "%.0f m");
        ImGui::TextUnformatted("Spread");
        FloatSlider("##str", &T.starRadius, 0.f, 60.f, "%.0f m");
        ImGui::TextUnformatted("Speed");
        FloatSlider("##sts", &T.starSpeed, 5.f, 80.f, "%.0f");
        ImGui::SetNextItemWidth(-1);
        ImGui::ColorEdit3("##stc", T.starColor);
        if (ImGui::Button("Star shower", ImVec2(-1, 0))) StarShower();
        Dim("StarFallShower from points above you (shooting stars that fall and burst). It ends by itself once each "
            "star has landed, so there's no Remove: the game deletes it, and switching off a deleted event crashes. "
            "At most %d per level (%d this level); their markers stay until the level changes.",
            kMaxStarShowers, T.starsThisLevel);

        ImGui::Spacing();
        if (!GameIcon("UiMapRain", sz)) ImGui::Dummy(ImVec2(sz, sz));
        ImGui::SameLine();
        ImGui::TextUnformatted("Darkstone rain");
        ImGui::TextUnformatted("Radius");
        FloatSlider("##rnr", &T.rainRadius, 5.f, 80.f, "%.0f m");
        ImGui::TextUnformatted("Drops per second");
        FloatSlider("##rnd", &T.rainRate, 50.f, 750.f, "%.0f");
        ImGui::Checkbox("Drains your light like the real one##rn", &T.rainDrain);
        if (ImGui::Button("Rain##rn", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 0))) Rain();
        ImGui::SameLine();
        ImGui::BeginDisabled(!T.rainOn);
        if (ImGui::Button("Remove##rn", ImVec2(-1, 0))) RainOff();
        ImGui::EndDisabled();
        Dim("DarkstoneRainRegion with its region marker on you. Drops that would leave pickups are off "
            "(dropCollectibles false); light drain is off unless ticked (the game's 0.05).");

        // ---- not offered
        Section("NOT OFFERED", "Kinds without a safe way to make them from the mod (what the game would do instead).");
        NotOffered("UiMapStar", "Shard tornado (Shardnado)",
                   "An event, but its start reads its ShardnadoParams object without checking it (0x1DF5C94: params "
                   "+0x10). Those objects only come from level files; the mod can't make one, and nil would crash.");
        NotOffered("UiHudStarSplit0", "ShardFall / MeteorFall on their own",
                   "No settings: the shard count (+0x60) and meshes are filled by the eruption system (or by "
                   "ManualShardFall, above). Made from Lua the count is 0 and they do nothing (0x19CEA94).");
        NotOffered("UiEmoteCallCrab", "Crab burrow (SpawnCrabs)",
                   "Its start reads its CrabParams object unchecked (0x1553CD8: params +0xE5); only level files make "
                   "those. Crabs are offered above, through the test school.");
        NotOffered("UiEmoteButterfly", "Light motes (SpawnMotes, SpawnLiftMotes)",
                   "Each spawn passes its MoteParams object (+0x70) to the mote barn, which reads it unchecked "
                   "(0x1A125A4). Butterflies are offered above.");
        NotOffered("UiMapSpirit", "NPCs / spirits (NpcDefs)",
                   "NpcBarn:CreateNpc exists, but an NPC is set up only by the barn's OnLevelLoadLate (0x1AA9C68 -> "
                   "0x1AAC670) while the level loads; one made later isn't, and what its update then does isn't "
                   "verified. Npc_SetDef/Npc_Teleport need an NPC the level already has.");
        NotOffered("UiMenuBuffKrillCurse", "Krill / dark creatures, dark dragons (Lurker)",
                   "They would hunt you: their attacks knock off winged light, which the server keeps.");
        NotOffered("UiEmoteCallFish", "Collectible fish (CollectibleFishClump)",
                   "Collectible fish are pickups; collecting them may change what your account holds.");

        // ---- developer
        Section("DEVELOPER", "");
        bool d = T.debugOn;
        if (ImGui::Checkbox("Fish school debug window", &d)) {
            if (SetDebugSwitch(d)) T.debugOn = d;
        }
        ImGui::Indent(ImGui::GetFrameHeight() + 8);
        Dim("The game's kFishSchoolDebug switch (byte 0x%X, 0 normally; TheVoid's table): the fish school barn then "
            "opens the developers' \"Fish Schools\" window each frame (0x173F6E4: Create Test Birds ... Clear Test "
            "School, the same calls as above) and draws each school's debug shapes. Whether that window shows "
            "depends on the game drawing its debug UI in this build. Off puts 0 back.",
            kFishSchoolDebug);
        ImGui::Unindent(ImGui::GetFrameHeight() + 8);
    }
    TouchScroll();
    ImGui::EndChild();
}

}  // namespace lab
