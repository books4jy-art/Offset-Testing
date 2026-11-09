// Custom song playback (0.21.0): plays Sky-music sheets (the common "songNotes" format: {"time": ms, "key": "1Key5"})
// on the instrument you're holding, note by note, through the game's own key functions — the same route TSM uses.
//
// How it works (Sky 0.35.2):
//  - PlayMusicKey(button, game) at 0x252B188 plays one instrument key; StopNote(button, game) at 0x253239C ends it.
//    Both first words are checked before anything is hooked or called.
//  - Learning: a Dobby pre-call hook on PlayMusicKey notes each key button the game passes while you press the
//    instrument's 15 keys once. Each button's note number is at +0x2D8 (PlayMusicKey reads it there); keys are
//    stored by that number (0..14), or in the order pressed if the number looks wrong.
//  - Playing: notes due are queued and played on the game's main thread (RunOnGameThread): the button's pressed byte
//    (+0x302) is set, PlayMusicKey is called, and StopNote follows after a short sustain.
#include "lab.h"

#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "dobby.h"
#include "imgui.h"
#include "json.h"

namespace lab {
namespace {

constexpr uint32_t kPlayKey = 0x252B188, kPlayKeyFirst = 0xD10343FF;
constexpr uint32_t kStopNote = 0x253239C, kStopNoteFirst = 0xD10283FF;
constexpr int kKeys = 15;

std::atomic<uintptr_t> gKey[kKeys];
std::atomic<bool> gLearning{false};
std::atomic<int> gLearnOrder{0};
bool gHooked = false;

void OnPlayKey(void*, DobbyRegisterContext* ctx) {
    if (!gLearning.load(std::memory_order_relaxed)) return;
    uintptr_t b = ctx->general.regs.x0;
    if (!b) return;
    for (auto& k : gKey) if (k.load() == b) return;
    uint32_t id = 0xFFFFFFFF;
    ReadMemory(b + 0x2D8, &id, 4);
    int slot = id < (uint32_t)kKeys ? (int)id : gLearnOrder.load();
    if (slot >= 0 && slot < kKeys) gKey[slot] = b;
    gLearnOrder++;
}

bool CheckFunctions() {
    uint32_t a = 0, b = 0;
    return GameBase() && ReadMemory(GameBase() + kPlayKey, &a, 4) && a == kPlayKeyFirst &&
           ReadMemory(GameBase() + kStopNote, &b, 4) && b == kStopNoteFirst;
}

bool StartLearning() {
    if (!CheckFunctions()) {
        Log("Custom songs: the game's key functions aren't what 0.35.2 has; not hooked");
        return false;
    }
    if (!gHooked) {
        if (DobbyInstrument(reinterpret_cast<void*>(GameBase() + kPlayKey), OnPlayKey) != 0) {
            Log("Custom songs: couldn't hook the key function");
            return false;
        }
        gHooked = true;
    }
    for (auto& k : gKey) k = 0;
    gLearnOrder = 0;
    gLearning = true;
    return true;
}

int LearnedKeys() {
    int n = 0;
    for (auto& k : gKey) n += k.load() != 0;
    return n;
}

// ---- notes played on the game thread
struct Pending { uintptr_t button; bool press; double due; };
std::mutex gNoteMutex;
std::vector<Pending> gPending;
double gNow = 0;  // UI clock, copied for the pump

void NotePump(uintptr_t) {
    std::vector<Pending> run;
    {
        std::lock_guard<std::mutex> lk(gNoteMutex);
        for (auto it = gPending.begin(); it != gPending.end();) {
            if (it->due <= gNow) run.push_back(*it), it = gPending.erase(it);
            else ++it;
        }
    }
    uintptr_t base = GameBase(), game = GameObject();
    if (!base || !game) return;
    using KeyFn = void (*)(int64_t, int64_t);
    auto play = reinterpret_cast<KeyFn>(base + kPlayKey);
    auto stop = reinterpret_cast<KeyFn>(base + kStopNote);
    for (auto& p : run) {
        uintptr_t vt = 0;
        if (!ReadMemory(p.button, &vt, 8) || !vt) continue;  // the instrument went away
        if (p.press) {
            *reinterpret_cast<volatile uint8_t*>(p.button + 0x302) = 1;  // the key shows pressed
            play((int64_t)p.button, (int64_t)game);
        } else {
            stop((int64_t)p.button, (int64_t)game);
        }
    }
}

// ---- songs
struct Note { double ms; int key; };
struct Song { std::string name, file; std::vector<Note> notes; };
std::vector<Song> gSongs;
int gSel = -1;
bool gPlaying = false, gLoop = false;
double gStart = 0;
size_t gNext = 0;
float gSpeed = 1.f, gSustain = 0.15f;

std::string SheetDir() { return DataPath("offsetlab_sheets"); }

bool ParseSong(const std::string& text, Song& out) {
    Json j = Json::Parse(text.c_str());
    const Json* s = &j;
    if (j.kind == Json::kArray && !j.a.empty()) s = &j.a[0];
    const Json* notes = s->get("songNotes");
    if (!notes || notes->kind != Json::kArray) return false;
    std::string nm = s->str("name");
    if (!nm.empty()) out.name = nm;
    for (const auto& n : notes->a) {
        std::string k = n.str("key");
        size_t p = k.find("Key");
        if (p == std::string::npos) continue;
        int key = atoi(k.c_str() + p + 3);
        if (key < 0 || key >= kKeys) continue;
        out.notes.push_back({n.num("time", 0), key});
    }
    std::sort(out.notes.begin(), out.notes.end(), [](const Note& a, const Note& b) { return a.ms < b.ms; });
    return !out.notes.empty();
}

void LoadSongs() {
    gSongs.clear();
    gSel = -1;
    std::string dir = SheetDir();
    if (dir.empty()) return;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string f = e->d_name;
            if (f.size() < 5 || f[0] == '.') continue;
            Song s;
            s.name = f;
            s.file = dir + "/" + f;
            std::string text = ReadTextFile(s.file);
            // Sky Studio files are often UTF-16; keep only the ASCII bytes
            if (text.size() > 2 && ((unsigned char)text[0] == 0xFF || (unsigned char)text[0] == 0xFE ||
                                    text[1] == 0)) {
                std::string a;
                for (char c : text) if (c > 0 && (unsigned char)c < 128) a += c;
                text.swap(a);
            }
            if (ParseSong(text, s)) gSongs.push_back(std::move(s));
        }
        closedir(d);
    }
    std::sort(gSongs.begin(), gSongs.end(), [](const Song& a, const Song& b) { return a.name < b.name; });
    Log("Custom songs: %zu sheets in %s", gSongs.size(), dir.c_str());
}

}  // namespace

void MusicTick() {
    double t = ImGui::GetTime();
    if (gLearning && LearnedKeys() >= kKeys) gLearning = false, Log("Custom songs: all 15 keys learned");
    bool pump = false;
    if (gPlaying && gSel >= 0 && gSel < (int)gSongs.size()) {
        const Song& s = gSongs[gSel];
        double songMs = (t - gStart) * 1000.0 * gSpeed;
        std::lock_guard<std::mutex> lk(gNoteMutex);
        while (gNext < s.notes.size() && s.notes[gNext].ms <= songMs) {
            uintptr_t b = gKey[s.notes[gNext].key].load();
            if (b && gPending.size() < 256) {
                gPending.push_back({b, true, t});
                gPending.push_back({b, false, t + gSustain});
            }
            gNext++;
        }
        if (gNext >= s.notes.size()) {
            if (gLoop) gStart = t + 1.0, gNext = 0;
            else gPlaying = false, Log("Custom songs: %s finished", s.name.c_str());
        }
    }
    {
        std::lock_guard<std::mutex> lk(gNoteMutex);
        gNow = t;
        for (auto& p : gPending) pump |= p.due <= t;
    }
    if (pump) RunOnGameThread(NotePump, 0);
}

void CustomSongsSection() {
    ImGui::SeparatorText("CUSTOM SONGS");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.7f));
    ImGui::TextWrapped("Plays Sky-music sheets (the \"songNotes\" format) on the instrument you're holding. 1) Take "
                       "out an instrument and open it. 2) Press Learn keys, then press each of its 15 keys once. "
                       "3) Put sheet files in the offsetlab_sheets folder next to the mod and press Reload. Others "
                       "hear it like normal playing.");
    ImGui::PopStyleColor();
    int learned = LearnedKeys();
    if (ImGui::Button(gLearning ? "Learning... (press the 15 keys)" : "Learn keys")) {
        if (gLearning) gLearning = false;
        else StartLearning();
    }
    ImGui::SameLine();
    ImGui::Text("%d / 15 keys", learned);
    if (ImGui::Button("Reload sheets")) LoadSongs();
    ImGui::SameLine();
    ImGui::Text("%zu sheets", gSongs.size());
    if (ImGui::BeginChild("##songs", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 6.5f), ImGuiChildFlags_Border)) {
        for (int i = 0; i < (int)gSongs.size(); i++) {
            ImGui::PushID(i);
            char label[160];
            snprintf(label, sizeof label, "%s  (%zu notes)", gSongs[i].name.c_str(), gSongs[i].notes.size());
            if (ImGui::Selectable(label, gSel == i)) gSel = i;
            ImGui::PopID();
        }
    }
    TouchScroll();
    ImGui::EndChild();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::SliderFloat("Speed", &gSpeed, 0.25f, 2.f, "%.2fx");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::SliderFloat("Note length", &gSustain, 0.05f, 1.f, "%.2f s");
    ImGui::Checkbox("Loop", &gLoop);
    ImGui::BeginDisabled(gSel < 0 || learned == 0);
    if (ImGui::Button(gPlaying ? "Restart" : "Play")) {
        if (CheckFunctions()) gPlaying = true, gStart = ImGui::GetTime(), gNext = 0;
        else Log("Custom songs: the game's key functions aren't what 0.35.2 has");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) gPlaying = false;
    if (learned > 0 && learned < kKeys)
        ImGui::TextColored(ImVec4(0.99f, 0.83f, 0.3f, 1), "Only %d keys learned: notes on the others are skipped.",
                           learned);
}

}  // namespace lab
