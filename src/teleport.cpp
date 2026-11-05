// Features > Teleport: teleport to a level (the game's ChangeLevel event), to the level links of the current level
// (read natively from the game's LevelLinkBarn), to typed coordinates, and quick moves by a distance. Avatar moves go
// through the game's own AvatarSetPos event with a marker placed at the target; the avatar's position is only read
// (natively, through a pointer chain checked in the 0.35.2 code), never written.
#include "lab.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "canvas_api.h"
#include "imgui.h"

namespace lab {

namespace {

// ---------------------------------------------------------------- Sky 0.35.2 addresses (see README, Teleport)
// Game object (its pointer at 0x33A5E80; GameObject()):
constexpr uintptr_t kGameAvatarBarn = 0x318;     // Game reflection record "avatarBarn" (built at 0x17C52D8)
constexpr uintptr_t kGameLevelLinkBarn = 0x528;  // Game reflection record "levelLinkBarn" (built at 0x17C43D8)
constexpr uintptr_t kGameLevelName = 0x960;      // Game reflection record "levelName" (built at 0x17C2B00)
// AvatarBarn: the game's GetLocalAvatar(barn, true) at 0x11B2BC4 returns barn + 0x30 when the byte at +0xB8A0 is set
// and bit 3 of the halfword at +0x10A3C is set, else null.
constexpr uintptr_t kBarnActive = 0xB8A0;
constexpr uintptr_t kBarnFlags = 0x10A3C;
constexpr uintptr_t kBarnLocalAvatar = 0x30;
constexpr uintptr_t kAvatarPosPtr = 0x18;  // avatar +0x18 -> {x, y, z} (0xFB3934: printed as "pos" "%.2f,%.2f,%.2f")
constexpr uintptr_t kPosYaw = 0x20;        // float after it: TSM's avatar rotation (yaw); not checked in the code
// LevelLinkBarn (loops at 0x18DA178 / 0x18DA2F4): count u32 at +0x4E70, links inline from +0x70, 0xD0 bytes each.
constexpr uintptr_t kLinkCount = 0x4E70;
constexpr uintptr_t kLinkFirst = 0x70;
constexpr uintptr_t kLinkStride = 0xD0;
constexpr uint32_t kLinkMax = (kLinkCount - kLinkFirst) / kLinkStride;  // 96
// LevelLink (its setting records): transform 4x4 at +0x30, linkName +0x80, levelName +0x98 (std::string), enabled +0xCC
constexpr uintptr_t kLinkTransform = 0x30;
constexpr uintptr_t kLinkLinkName = 0x80;
constexpr uintptr_t kLinkLevelName = 0x98;
constexpr uintptr_t kLinkEnabled = 0xCC;

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

double Now() { return ImGui::GetTime(); }

bool IsId(const std::string& s) {  // a level id: letters, digits, '_' (also keeps anything else out of the Lua)
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

bool Sane(float v) { return std::isfinite(v) && std::fabs(v) < 100000.f; }

// ---------------------------------------------------------------- reading the game (never crashes: ReadMemory)

template <class V> bool Rd(uintptr_t a, V& v) { return ReadMemory(a, &v, sizeof v); }

// libc++ std::string (as the game reads LevelLink names at 0x18D8890): short when bit 0 of the first byte is clear
// (length = byte >> 1, text from +1), else length at +8 and text pointer at +0x10.
std::string ReadStdString(uintptr_t a) {
    uint8_t b0 = 0;
    if (!Rd(a, b0)) return {};
    char buf[257] = {};
    size_t len = 0;
    uintptr_t text = 0;
    if (!(b0 & 1)) {
        len = b0 >> 1;
        text = a + 1;
        if (len > 22) return {};
    } else {
        uint64_t n = 0;
        if (!Rd(a + 8, n) || !Rd(a + 0x10, text) || n > 256) return {};
        len = (size_t)n;
    }
    if (len && !ReadMemory(text, buf, len)) return {};
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)buf[i] < 32 || (unsigned char)buf[i] > 126) return {};
    return std::string(buf, len);
}

std::string CurrentLevel() {
    uintptr_t game = GameObject();
    if (!game) return {};
    std::string s = ReadString(game + kGameLevelName, 64);  // the level name kept in the game object
    if (IsId(s)) return s;
    s = ReadStdString(game + kGameLevelName);  // in case it's a std::string here
    return IsId(s) ? s : std::string();
}

// The local avatar's position (and TSM's yaw, when it looks like an angle). False when there's no local avatar.
bool AvatarPos(float out[3], float* yaw = nullptr) {
    uintptr_t game = GameObject(), barn = 0, p = 0;
    uint8_t active = 0;
    uint16_t flags = 0;
    if (!game || !Rd(game + kGameAvatarBarn, barn) || !barn) return false;
    if (!Rd(barn + kBarnActive, active) || !active) return false;
    if (!Rd(barn + kBarnFlags, flags) || !(flags & 8)) return false;
    if (!Rd(barn + kBarnLocalAvatar + kAvatarPosPtr, p) || !p) return false;
    float v[3];
    if (!ReadMemory(p, v, sizeof v) || !Sane(v[0]) || !Sane(v[1]) || !Sane(v[2])) return false;
    memcpy(out, v, sizeof v);
    if (yaw) {
        float r = NAN;
        *yaw = (Rd(p + kPosYaw, r) && std::isfinite(r) && std::fabs(r) < 100.f) ? r : NAN;
    }
    return true;
}

struct Link { float pos[3]; std::string level, name; bool enabled; };

// The level links of the current level, from the game's LevelLinkBarn.
bool ReadLinks(std::vector<Link>& out, std::string& why) {
    out.clear();
    uintptr_t game = GameObject(), barn = 0;
    uint32_t n = 0;
    if (!game) { why = "Enter the world first."; return false; }
    if (!Rd(game + kGameLevelLinkBarn, barn) || !barn) { why = "No level links (the level isn't loaded)."; return false; }
    if (!Rd(barn + kLinkCount, n) || n > kLinkMax) { why = "The level link list isn't readable."; return false; }
    for (uint32_t i = 0; i < n; i++) {
        uintptr_t L = barn + kLinkFirst + i * kLinkStride;
        float m[16];
        if (!ReadMemory(L + kLinkTransform, m, sizeof m)) continue;
        bool ok = true;
        for (float f : m) ok &= std::isfinite(f);
        if (!ok || std::fabs(m[15] - 1.f) > 0.01f) continue;  // not an affine transform: skip it
        Link k{};
        // translation in the last row (as the game's Lua transforms are written: {..., {x, y, z, 1}}); if that row
        // is empty and the last column isn't, it's in the last column
        bool rowT = std::fabs(m[3]) < 1e-4f && std::fabs(m[7]) < 1e-4f && std::fabs(m[11]) < 1e-4f;
        k.pos[0] = rowT ? m[12] : m[3];
        k.pos[1] = rowT ? m[13] : m[7];
        k.pos[2] = rowT ? m[14] : m[11];
        if (!Sane(k.pos[0]) || !Sane(k.pos[1]) || !Sane(k.pos[2])) continue;
        k.level = ReadStdString(L + kLinkLevelName);
        k.name = ReadStdString(L + kLinkLinkName);
        uint8_t en = 1;
        Rd(L + kLinkEnabled, en);
        k.enabled = en != 0;
        out.push_back(std::move(k));
    }
    if (out.empty()) why = n ? "The level links didn't look valid; none shown." : "This level has no level links.";
    return !out.empty();
}

// ---------------------------------------------------------------- the game's events, from Lua

// Only the game's events can be started (anything else aborts the game).
bool IsGameEvent(const char* type) {
    static std::map<std::string, int> cache;  // 1 = event, 0 = not / not found
    auto it = cache.find(type);
    if (it != cache.end()) return it->second == 1;
    if (!GameBase()) return false;
    size_t n = 0;
    const LabType* t = Types(&n);
    int r = 0;
    for (size_t i = 0; i < n; i++) {
        if (Fnv1a(type) != t[i].nameHash) continue;
        if (GameString(t[i].nameAddr) != type) continue;
        r = EventStatusOf(i) == kIsEvent ? 1 : 0;
        break;
    }
    cache[type] = r;
    if (!r) Log("Teleport: %s isn't an event in this game version; not used", type);
    return r == 1;
}

// Same as the features' prelude: saved handles are dropped when the level changes.
const char* kPrelude =
    "OffsetLab = OffsetLab or {}\n"
    "do local ok, lvl = pcall(function() return game:levelName() end)\n"
    "  if ok and OffsetLab._level ~= lvl then OffsetLab = {_level = lvl} end end\n";

bool Send(const std::string& what, const std::string& script, const char* key) {
    int r = RunLua(script, what, key);
    if (r == -1) Log("%s: enter the world first", what.c_str());
    else if (r == -3) Log("%s: the game isn't running frames right now", what.c_str());
    else if (r != 0) Log("%s: the game reported a script error (%d)", what.c_str(), r);
    else Log("%s", what.c_str());
    return r == 0;
}

struct State {
    // coordinates
    float xyz[3] = {0, 0, 0};
    bool haveBack = false;
    float back[3] = {0, 0, 0};
    // relative moves: the last target, so quick presses add up before the game has moved the avatar
    float lastTarget[3] = {0, 0, 0};
    double lastAt = -10;
    // quick movement
    float step = 5.f;
    bool facing = true, hold = true, ground = false;
    double holdNext = 0;
    bool repeated = false;  // the held button already repeated: its release isn't one more move
    // POI
    std::vector<Link> links;
    std::string linksWhy, linksLevel;
    double linksAt = -10;
    float poiUp = 1.f;
    // levels
    char filter[48] = "";
    bool unconfirmed = false;
    double gameListAt = 0;
} T;

bool Teleport(float x, float y, float z, const std::string& what) {
    if (!Sane(x) || !Sane(y) || !Sane(z)) {
        Log("Teleport: invalid coordinates (%g, %g, %g); not sent", x, y, z);
        return false;
    }
    if (!IsGameEvent("AvatarSetPos")) return false;
    float cur[3];
    if (AvatarPos(cur)) {  // where you were, for Back
        memcpy(T.back, cur, sizeof cur);
        T.haveBack = true;
    }
    char b[96];
    snprintf(b, sizeof b, "{%.4f, %.4f, %.4f}", x, y, z);
    std::string s = kPrelude;
    s += "local e = game:eventBarn():AddEventByMetaName(\"AvatarSetPos\")\n"
         "if not e then error(\"AvatarSetPos not found\") end\n"
         "OffsetLab.tpM = OffsetLab.tpM or game:markerBarn():CreateMarker(0)\n"
         "OffsetLab.tpM:pos(" + std::string(b) + ")\n"
         "e:marker(OffsetLab.tpM)\n"
         "pcall(function() e:relMarker(nil) end)\n"
         "pcall(function() e:bstGuid(0) end)\n"
         "pcall(function() e:onGround(" + std::string(T.ground ? "true" : "false") + ") end)\n"
         "pcall(function() e:randomGroundInMarker(false) end)\n"
         "pcall(function() e:setLevelStartPosition(false) end)\n"
         "pcall(function() e:breakHandHold(true) end)\n"
         "pcall(function() e:killMomentum(true) end)\n"
         "e:Start()\n";
    T.lastTarget[0] = x, T.lastTarget[1] = y, T.lastTarget[2] = z;
    T.lastAt = Now();
    char w[160];
    snprintf(w, sizeof w, "%s (%.1f, %.1f, %.1f)", what.c_str(), x, y, z);
    return Send(w, s, "teleport");
}

bool ChangeLevel(const std::string& level) {
    if (!IsId(level)) {
        Log("Teleport: \"%s\" isn't a level id; not sent", level.c_str());
        return false;
    }
    if (!IsGameEvent("ChangeLevel")) return false;
    std::string s = kPrelude;
    s += "local e = game:eventBarn():AddEventByMetaName(\"ChangeLevel\")\n"
         "if not e then error(\"ChangeLevel not found\") end\n"
         "e:levelName(\"" + level + "\")\n"
         "pcall(function() e:isLevelComplete(false) end)\n"
         "e:Start()\n";
    return Send("Change level to " + level, s, "changelevel");
}

// ---------------------------------------------------------------- level list

enum Realm { kHome, kDawn, kPrairie, kForest, kValley, kWasteland, kVault, kEden, kOther, kRealmCount };
const char* const kRealmNames[kRealmCount] = {
    "Home & Aviary", "Isle of Dawn", "Daylight Prairie", "Hidden Forest", "Valley of Triumph",
    "Golden Wasteland", "Vault of Knowledge", "Eye of Eden", "Events & other",
};

bool Starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

int RealmOf(const std::string& id) {
    if (Starts(id, "CandleSpace") || Starts(id, "MainStreet") || Starts(id, "Aviary") || Starts(id, "Nintendo") ||
        Starts(id, "VoidSharedSpace"))
        return kHome;
    if (Starts(id, "Dawn")) return kDawn;
    if (Starts(id, "Prairie") || Starts(id, "Day")) return kPrairie;
    if (Starts(id, "Rain")) return kForest;
    if (Starts(id, "Sunset")) return kValley;
    if (Starts(id, "Dusk")) return kWasteland;
    if (Starts(id, "Night")) return kVault;
    if (Starts(id, "Storm") || Starts(id, "Orbit")) return kEden;
    return kOther;
}

// A readable name made from the id: "Prairie_ButterflyFields" -> "Prairie: Butterfly Fields",
// "Night_IPHallway" -> "Night: IP Hallway", "SunsetEnd2" -> "Sunset End 2".
std::string Readable(const std::string& id) {
    std::string out;
    bool first = true;
    size_t start = 0;
    while (start <= id.size()) {
        size_t us = id.find('_', start);
        std::string part = id.substr(start, us == std::string::npos ? std::string::npos : us - start);
        std::string words;
        for (size_t i = 0; i < part.size(); i++) {
            char c = part[i], prev = i ? part[i - 1] : 0, next = i + 1 < part.size() ? part[i + 1] : 0;
            bool up = isupper((unsigned char)c), dig = isdigit((unsigned char)c);
            bool brk = i && ((up && (islower((unsigned char)prev) || isdigit((unsigned char)prev))) ||
                             (up && isupper((unsigned char)prev) && islower((unsigned char)next)) ||
                             (dig && isalpha((unsigned char)prev)));
            if (brk) words += ' ';
            words += c;
        }
        if (!words.empty()) {
            if (!first) out += out.size() && out.find(':') == std::string::npos ? ": " : " · ";
            out += words;
            first = false;
        }
        if (us == std::string::npos) break;
        start = us + 1;
    }
    return out.empty() ? id : out;
}

// Level ids. Known: levels other mods teleport to (TSM's level list for 0.35.1; TheVoid's names are encrypted).
// Unconfirmed: further level-like ids found in 0.35.2's strings; they may not be levels.
const char* const kKnown[] = {
    "CandleSpace", "CandleSpaceEnd", "MainStreet", "MainStreetFlyingIntro", "MainStreet_Cafe",
    "MainStreet_ConcertHall", "MainStreet_ShopOutfits", "MainStreet_ShopProps", "MainStreet_ShopSpells",
    "MainStreet_Soundbath", "Dawn", "DawnCave", "Dawn_TrialsWater", "Dawn_TrialsEarth", "Dawn_TrialsAir",
    "Dawn_TrialsFire", "Prairie_ButterflyFields", "Prairie_Village", "Prairie_Cave", "Prairie_Island",
    "Prairie_NestAndKeeper", "Prairie_WildLifePark", "DayHubCave", "DayEnd", "Rain", "RainForest", "RainShelter",
    "RainMid", "RainEnd", "Rain_BaseCamp", "Rain_Cave", "Sunset", "SunsetRace", "SunsetColosseum", "SunsetVillage",
    "SunsetVillage_MusicShop", "SunsetEnd", "SunsetEnd2", "Sunset_Citadel", "Sunset_FlyRace", "Sunset_Theater",
    "Sunset_YetiPark", "DuskStart", "Dusk", "DuskGraveyard", "DuskMid", "DuskOasis", "DuskEnd", "Dusk_CrabField",
    "Dusk_Triangle", "Dusk_TriangleEnd", "Night", "Night2", "NightArchive", "NightDesert", "NightDesert_Beach",
    "NightDesert_Planets", "NightEnd", "Night_IPHallway", "Night_InfiniteDesert", "Night_JarCave",
    "Night_PaintedWorld", "Night_Shelter", "StormStart", "Storm", "StormEnd", "OrbitMid", "OrbitEnd",
    "Event_Cinema", "Event_DaysOfMischief", "Skyway", "TGCOffice",
};
const char* const kUnconfirmed[] = {
    "Nintendo_CandleSpace", "VoidSharedSpace", "AviaryVillage", "Aviary_Carnival", "MainStreet_Apartment",
    "MainStreet_Apartment_02", "MainStreet_Cafe_Wonderland", "MainStreet_FunArmada", "MainStreet_StorySpace",
    "Prairie_ButterflyFields3", "Prairie_Village_Shrine", "Rain_BlueBirdTheater", "Rain_Town", "Dusk_HiddenTemple",
    "Dusk_Pearl", "Dusk_PearlIsland", "Dusk_PearlSea", "Dusk_PearlSeaDeep", "DuskEnd_Past", "DuskMid_Past",
    "DuskMid_PastMarket", "Night_Jumble", "Night_StoryBook", "Night_Valley", "Night_ValleyForest",
    "Night_ValleyHouse", "Night_ValleyHouseSky", "Night_Workshop", "Night_GO_City", "Night_GO_Farm",
    "Night_GO_Field", "Night_GO_Gallery", "Night_GO_Island", "Night_GO_Town", "Night_GO_Tree",
    "Event_DaysOfMischief_Escape", "Event_DaysOfMoonlight_HugeMoon", "Event_Personality", "Event_Arr_TheSeed",
    "Event_Arr_EyesOfAChild", "Event_Arr_Runaway", "Event_Arr_SoftInside", "Event_Arr_ExhaleInhale",
    "Event_Arr_Warrior",
};

enum : uint8_t { kSrcKnown = 1, kSrcUnconfirmed = 2, kSrcGame = 4 };
struct Level { std::string id, label; int realm; uint8_t src; };
std::vector<Level> gLevels;
std::map<std::string, size_t> gLevelIndex;

void AddLevel(const std::string& id, uint8_t src) {
    if (!IsId(id)) return;
    auto it = gLevelIndex.find(id);
    if (it != gLevelIndex.end()) {
        gLevels[it->second].src |= src;
        return;
    }
    gLevelIndex[id] = gLevels.size();
    gLevels.push_back({id, Readable(id), RealmOf(id), src});
}

// The game's level select script (the file TheVoid reads), if Canvas can read it: every level("Id", ...) call.
// Only ids are taken from it; nothing is kept but the list.
size_t ParseLevelSelect(const char* text) {
    size_t found = 0;
    for (const char* p = text; (p = strstr(p, "level")) != nullptr; p += 5) {
        if (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '.' || p[-1] == ':')) continue;
        const char* q = p + 5;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '(') continue;
        q++;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '"' && *q != '\'') continue;
        char quote = *q++;
        const char* e = strchr(q, quote);
        if (!e || e - q > 64) continue;
        std::string id(q, e - q);
        if (IsId(id)) {
            AddLevel(id, kSrcGame);
            found++;
        }
    }
    return found;
}

void LoadLevels() {
    static bool done = false;
    if (done) return;
    done = true;
    for (const char* id : kKnown) AddLevel(id, kSrcKnown);
    for (const char* id : kUnconfirmed) AddLevel(id, kSrcUnconfirmed);
    if (!GameBase()) return;
    for (const char* path : {"Data/Scripts/LevelSelect.lua", "Data/Raw/Scripts/LevelSelect.lua"}) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s", path);
        const char* text = Cipher::read_asset(buf);
        if (!text) continue;
        size_t n = ParseLevelSelect(text);
        Log("Teleport: %s lists %zu levels", path, n);
        if (n) break;
    }
}

// Asks the game's Lua for its own level list (ArcLevels, the table the level select uses) and writes it to a file
// next to the mod; read back a moment later.
void AskGameLevels() {
    std::string path = DataPath("offsetlab_levels.txt");
    if (path.empty()) {
        Log("Teleport: no folder to write the level list to");
        return;
    }
    std::string q = "\"";
    for (char c : path) {
        if (c == '"' || c == '\\') q += '\\';
        q += c;
    }
    q += "\"";
    std::string s =
        "local out = {}\n"
        "local function add(t)\n"
        "  if type(t) ~= \"table\" then return end\n"
        "  for _, l in pairs(t) do\n"
        "    if type(l) == \"table\" and l.name then out[#out+1] = tostring(l.name)\n"
        "    elseif type(l) == \"string\" then out[#out+1] = l end\n"
        "  end\n"
        "end\n"
        "pcall(add, rawget(_G, \"ArcLevels\"))\n"
        "local f = io and io.open(" + q + ", \"w\")\n"
        "if not f then error(\"no file access from Lua\") end\n"
        "f:write(table.concat(out, \"\\n\")); f:close()\n";
    remove(path.c_str());
    if (Send("Asked the game for its level list", s, "levellist")) T.gameListAt = Now() + 1.0;
}

void PollGameLevels() {
    if (T.gameListAt <= 0 || Now() < T.gameListAt) return;
    T.gameListAt = 0;
    std::string text = ReadTextFile(DataPath("offsetlab_levels.txt"));
    size_t n = 0, start = 0;
    while (start < text.size()) {
        size_t e = text.find('\n', start);
        std::string id = text.substr(start, e == std::string::npos ? std::string::npos : e - start);
        if (!id.empty() && id.back() == '\r') id.pop_back();
        if (IsId(id)) AddLevel(id, kSrcGame), n++;
        if (e == std::string::npos) break;
        start = e + 1;
    }
    if (n) Log("Teleport: the game lists %zu levels", n);
    else Log("Teleport: the game gave no level list");
}

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------- sections

void RealmSection(const std::string& here) {
    Section("TELEPORT TO REALM",
            "Changes level with the game's own ChangeLevel event (what the game's level links use). Only for you; "
            "on a server you're moved to that level's room.");
    LoadLevels();
    PollGameLevels();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##lvlfilter", "Search levels", T.filter, sizeof T.filter);
    ImGui::Checkbox("Unconfirmed ids too", &T.unconfirmed);
    ImGui::SameLine();
    if (ImGui::Button("Ask the game")) AskGameLevels();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds the game's own level list (ArcLevels), if it has one");
    if (T.unconfirmed)
        ImGui::TextColored(kAmber300, "Unconfirmed: ids found in the game's text that may not be levels; changing "
                                      "to one that isn't may hang or crash the game.");
    std::string f = Lower(T.filter);
    if (ImGui::BeginChild("##levels", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 10.5f), ImGuiChildFlags_Border)) {
        size_t shown = 0;
        for (int r = 0; r < kRealmCount; r++) {
            bool header = false;
            for (size_t i = 0; i < gLevels.size(); i++) {
                const Level& l = gLevels[i];
                if (l.realm != r) continue;
                if (l.src == kSrcUnconfirmed && !T.unconfirmed) continue;
                if (!f.empty() && Lower(l.id).find(f) == std::string::npos && Lower(l.label).find(f) == std::string::npos)
                    continue;
                if (!header) {
                    ImGui::TextColored(kSky300, "%s", kRealmNames[r]);
                    header = true;
                }
                ImGui::PushID(l.id.c_str());
                std::string label = l.label;
                if (l.id == here) label += "  (here)";
                if (l.src == kSrcUnconfirmed) label += "  ?";
                if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) ChangeLevel(l.id);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s%s", l.id.c_str(), (l.src & kSrcGame) ? "  (in the game's list)" : "");
                ImGui::PopID();
                shown++;
            }
        }
        if (!shown) Dim("No level matches.");
    }
    TouchScroll();
    ImGui::EndChild();
    if (!here.empty() && ImGui::Button("Reload this level", ImVec2(-1, 0))) ChangeLevel(here);
}

void PoiSection(const std::string& here, const float* pos) {
    Section("POI MENU",
            "The level links of this level (the doorways the game uses to change level), read from the game. Go "
            "moves you onto one (you may go through it); Enter changes to the level it leads to.");
    if (Now() - T.linksAt > 1.0 || T.linksLevel != here) {  // re-read every second while shown
        T.linksAt = Now();
        T.linksLevel = here;
        ReadLinks(T.links, T.linksWhy);
    }
    ImGui::TextUnformatted("Height above the link");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##poiup", &T.poiUp, 0.f, 10.f, "%.1f");
    if (T.links.empty()) {
        Dim("%s", T.linksWhy.c_str());
        return;
    }
    if (ImGui::BeginChild("##links", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * std::min(8.5f, T.links.size() + 0.5f)),
                          ImGuiChildFlags_Border)) {
        for (size_t i = 0; i < T.links.size(); i++) {
            const Link& k = T.links[i];
            ImGui::PushID((int)i);
            std::string label = k.level.empty() ? "Link " + std::to_string(i + 1) : "To " + Readable(k.level);
            if (!k.name.empty() && k.name != k.level) label += " (" + k.name + ")";
            if (pos) {
                float dx = k.pos[0] - pos[0], dy = k.pos[1] - pos[1], dz = k.pos[2] - pos[2];
                char d[32];
                snprintf(d, sizeof d, "  %.0f m", std::sqrt(dx * dx + dy * dy + dz * dz));
                label += d;
            }
            if (!k.enabled) label += "  (off)";
            bool canEnter = IsId(k.level);
            float enterW = canEnter ? Px(150) : 0;
            if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - enterW - (canEnter ? Px(8) : 0), 0)))
                Teleport(k.pos[0], k.pos[1] + T.poiUp, k.pos[2], "Teleport to link " + (k.level.empty() ? std::to_string(i + 1) : k.level));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s / %s\n(%.1f, %.1f, %.1f)", k.level.c_str(), k.name.c_str(), k.pos[0], k.pos[1], k.pos[2]);
            if (canEnter) {
                ImGui::SameLine();
                if (ImGui::Button("Enter", ImVec2(enterW, 0))) ChangeLevel(k.level);
            }
            ImGui::PopID();
        }
    }
    TouchScroll();
    ImGui::EndChild();
}

void CoordSection(const float* pos) {
    Section("COORDINATES", "Type a position (or drag), then Teleport. The game's AvatarSetPos moves you there.");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputFloat3("##xyz", T.xyz, "%.2f");
    const char* axis[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        ImGui::TextUnformatted(axis[i]);
        ImGui::SameLine(Px(40));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::DragFloat("##d", &T.xyz[i], 0.5f, -100000.f, 100000.f, "%.2f");
        ImGui::PopID();
    }
    float w = (ImGui::GetContentRegionAvail().x - Px(16)) / 3;
    if (ImGui::Button("Use my position", ImVec2(w, 0))) {
        if (pos) memcpy(T.xyz, pos, sizeof T.xyz);
        else Log("Teleport: your position can't be read right now");
    }
    ImGui::SameLine();
    if (ImGui::Button("Teleport", ImVec2(w, 0))) Teleport(T.xyz[0], T.xyz[1], T.xyz[2], "Teleport");
    ImGui::SameLine();
    ImGui::BeginDisabled(!T.haveBack);
    if (ImGui::Button("Back", ImVec2(w, 0))) {
        float b[3];
        memcpy(b, T.back, sizeof b);
        Teleport(b[0], b[1], b[2], "Teleport back");
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("Snap to the ground", &T.ground);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("AvatarSetPos's onGround");
}

void Move(float fwd, float right, float up) {
    float base[3], yaw = NAN;
    bool live = AvatarPos(base, &yaw);
    if (Now() - T.lastAt < 0.6) memcpy(base, T.lastTarget, sizeof base);  // the last move may not have run yet
    else if (!live) {
        Log("Quick move: your position can't be read right now (enter a level)");
        return;
    }
    float d = T.step;
    float dx, dz;
    if (T.facing && std::isfinite(yaw)) {  // TSM's convention: forward = (sin yaw, cos yaw)
        float s = std::sin(yaw), c = std::cos(yaw);
        dx = fwd * s - right * c;
        dz = fwd * c + right * s;
    } else {  // world axes: forward = +z, right = +x
        dx = right;
        dz = fwd;
    }
    Teleport(base[0] + dx * d, base[1] + up * d, base[2] + dz * d, "Quick move");
}

void QuickSection() {
    Section("QUICK MOVEMENT", "Moves you by the distance from where you are (your position is read from the game).");
    ImGui::TextUnformatted("Distance");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##step", &T.step, 0.5f, 100.f, "%.1f m");
    ImGui::Checkbox("Relative to facing (experimental)", &T.facing);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Uses the avatar's turn angle (TSM's rotation value); off: world axes (forward = +z)");
    ImGui::Checkbox("Hold to repeat", &T.hold);
    float w = (ImGui::GetContentRegionAvail().x - Px(16)) / 3, h = Dp(44);
    struct B { const char* label; float f, r, u; };
    const B rows[2][3] = {{{"Up", 0, 0, 1}, {"Forward", 1, 0, 0}, {"Down", 0, 0, -1}},
                          {{"Left", 0, -1, 0}, {"Back##m", -1, 0, 0}, {"Right", 0, 1, 0}}};
    for (const auto& row : rows) {
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            const B& b = row[i];
            bool click = ImGui::Button(b.label, ImVec2(w, h));
            if (ImGui::IsItemActivated()) T.holdNext = Now() + 0.45, T.repeated = false;  // first repeat after a pause
            if (click) {
                if (!T.repeated) Move(b.f, b.r, b.u);
                T.repeated = false;
            } else if (T.hold && ImGui::IsItemActive() && Now() >= T.holdNext) {
                T.holdNext = Now() + 0.2;
                T.repeated = true;
                Move(b.f, b.r, b.u);
            }
        }
    }
}

}  // namespace

bool LocalAvatarPos(float out[3], float* yaw) { return AvatarPos(out, yaw); }

// Freeze in place: the same AvatarSetPos move as Teleport, to where you were, with momentum killed and hand-holds
// kept; nothing logged (it repeats) and Back isn't touched.
bool HoldAvatarAt(const float pos[3]) {
    if (!Sane(pos[0]) || !Sane(pos[1]) || !Sane(pos[2]) || !IsGameEvent("AvatarSetPos")) return false;
    char b[96];
    snprintf(b, sizeof b, "{%.4f, %.4f, %.4f}", pos[0], pos[1], pos[2]);
    std::string s = kPrelude;
    s += "local e = game:eventBarn():AddEventByMetaName(\"AvatarSetPos\")\n"
         "if not e then error(\"AvatarSetPos not found\") end\n"
         "OffsetLab.frzM = OffsetLab.frzM or game:markerBarn():CreateMarker(0)\n"
         "OffsetLab.frzM:pos(" + std::string(b) + ")\n"
         "e:marker(OffsetLab.frzM)\n"
         "pcall(function() e:relMarker(nil) end)\n"
         "pcall(function() e:bstGuid(0) end)\n"
         "pcall(function() e:onGround(false) end)\n"
         "pcall(function() e:randomGroundInMarker(false) end)\n"
         "pcall(function() e:setLevelStartPosition(false) end)\n"
         "pcall(function() e:breakHandHold(false) end)\n"
         "pcall(function() e:killMomentum(true) end)\n"
         "e:Start()\n";
    return RunLua(s, "Freeze in place", "freeze") == 0;
}

void TeleportSection() {
    std::string here = CurrentLevel();
    float pos[3];
    bool havePos = AvatarPos(pos);
    if (!GameObject()) ImGui::TextColored(kAmber300, "Enter the world to use these.");
    Dim("Level: %s", here.empty() ? "unknown" : here.c_str());
    if (havePos) Dim("Position: %.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);
    else Dim("Position: not readable right now");
    RealmSection(here);
    PoiSection(here, havePos ? pos : nullptr);
    CoordSection(havePos ? pos : nullptr);
    QuickSection();
}

}  // namespace lab
