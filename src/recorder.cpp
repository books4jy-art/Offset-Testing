// Event recorder: notes every game event the game starts, with the time and the values of its plain settings.
//
// Hook: the game's base Event start, 0x1717C8C in Sky 0.35.2. It's what the Lua binding's Event:Start calls (the
// method record at 0x3192508 points straight at it), what SetEnabled(true) tail-calls, and what the game's own code
// calls when its nodes fire events (1,192 direct calls and 114 tail calls in the library). Its first argument (x0) is
// the event object; the event's state flags are the 16 bits at +0x24 (Start sets bit 1).
//
// Which type: every game object keeps its meta class id (32 bits) at +8. The event barn checks it against the meta
// class's own id at +0x38 when it makes an event (0x1717EF4, "Invalid meta class id" otherwise). Each registered
// type object's +0x28 points at its runtime meta class (a copy the game makes at start-up, whose +0x28 points at
// itself); the ids are given out to those copies once, at start-up (0x29B50FC). So: type object +0x28 -> meta class
// -> +0x38 = id, read once per type, and the event's +8 picks the type.
//
// The hook's handler is small and never allocates, locks, logs or touches Lua: while recording is on it reads the
// event's header and its settings' bytes through ReadMemory (which can't crash) into a fixed ring of 4096 slots. The
// window drains the ring every frame. Installed the first time Start is pressed, never at load.
#include "lab.h"

#include "dobby.h"

#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lab {

namespace {

constexpr uintptr_t kEventStart = 0x1717C8C;
// its first four instructions: sub sp, sp, #0x30 / stp x29, x30, [sp, #0x10] / str x19, [sp, #0x20] /
// add x29, sp, #0x10 (Dobby rewrites the first 16 bytes; none of them depend on where they run)
constexpr uint32_t kEventStartCode[4] = {0xD100C3FF, 0xA9017BFD, 0xF90013F3, 0x910043FD};
constexpr uint32_t kHeaderSize = 0x28;   // vtable, meta class id (+8) ... state flags (+0x24)
constexpr uint32_t kFirstOwnByte = 0x26;  // settings below this are the base Event's own state: never read
constexpr size_t kMaxTypes = 4096, kMaxItems = 8192, kMaxIds = 4096, kRing = 4096;

// ---------------------------------------------------------------- read plan (built on the UI thread, while no
// handler can be running; read-only for the handler)

struct Item { uint32_t setting; uint16_t off; uint8_t size; uint8_t kind; };  // off: from the type's lo
struct Plan { uint32_t first; uint16_t count; uint16_t lo; uint16_t len; };

uint16_t gIdType[kMaxIds];  // meta class id -> type index (0xFFFF = none)
Plan gPlan[kMaxTypes];
Item gItems[kMaxItems];
size_t gTypeCount = 0;
int gMapped = 0, gPlanned = 0;
bool gBuilt = false;
std::vector<std::string> gSettingNames;  // UI thread only

// ---------------------------------------------------------------- ring (many writers, one reader)

struct Slot {
    std::atomic<uint64_t> seq{0};  // index + 1 once written; 0 while being written
    RecordedEvent ev;
};
Slot gRing[kRing];
std::atomic<uint64_t> gHead{0};
uint64_t gTail = 0;  // UI thread only
uint64_t gDropped = 0;
std::atomic<bool> gRecording{false};
std::atomic<int> gInFlight{0};
std::atomic<int> gHook{0};  // 0 = not tried, 1 = placed, -1 = refused

double MonoNow() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

uint8_t KindSize(uint8_t kind) {
    switch (kind) {
        case kBool: return 1;
        case kFloat: case kInt: return 4;
        case kVec2: return 8;
        case kVec3: case kColor3: return 12;
        case kVec4: return 16;
        default: return 0;  // text, choices, objects, unknown: not recorded
    }
}

bool PlainName(const std::string& n) {
    if (n.empty() || !std::isalpha((unsigned char)n[0])) return false;
    for (char c : n)
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

// Called only while recording is off and no handler is running.
void BuildPlan() {
    uintptr_t base = GameBase();
    if (!base) return;
    size_t n = 0;
    const LabType* t = Types(&n);
    gTypeCount = std::min(n, kMaxTypes);
    std::fill(std::begin(gIdType), std::end(gIdType), uint16_t(0xFFFF));
    // meta class id per type
    gMapped = 0;
    std::vector<uint32_t> ids(gTypeCount, 0xFFFFFFFF);
    std::vector<int> uses(kMaxIds, 0);
    for (size_t i = 0; i < gTypeCount; i++) {
        if (!t[i].typeObject) continue;
        uintptr_t meta = 0, self = 0;
        uint32_t id = 0xFFFFFFFF;
        if (!ReadMemory(base + t[i].typeObject + 0x28, &meta, sizeof meta) || !meta) continue;
        if (!ReadMemory(meta + 0x28, &self, sizeof self) || self != meta) continue;  // not a meta class
        if (!ReadMemory(meta + 0x38, &id, sizeof id) || id >= kMaxIds) continue;
        ids[i] = id;
        uses[id]++;
    }
    for (size_t i = 0; i < gTypeCount; i++)
        if (ids[i] != 0xFFFFFFFF && uses[ids[i]] == 1) {  // an id two types claim is left out
            gIdType[ids[i]] = (uint16_t)i;
            gMapped++;
        }
    // what to read per type
    if (gSettingNames.empty()) {
        size_t total = 0;
        for (size_t i = 0; i < n; i++) total = std::max<size_t>(total, t[i].settingFirst + t[i].settingCount);
        gSettingNames.resize(total);
        for (size_t s = 0; s < total; s++) gSettingNames[s] = GameString(Setting(s).nameAddr);
    }
    size_t used = 0;
    gPlanned = 0;
    for (size_t i = 0; i < gTypeCount; i++) {
        Plan& p = gPlan[i];
        p = Plan{(uint32_t)used, 0, 0, 0};
        struct Cand { uint32_t setting; uint16_t off; uint8_t size, kind; };
        std::vector<Cand> c;
        for (uint32_t k = 0; k < t[i].settingCount; k++) {
            uint32_t s = t[i].settingFirst + k;
            const LabSetting& st = Setting(s);
            uint8_t size = KindSize(st.kind);
            if (!size || st.hasRange == 2) continue;       // not recorded, or the type is only a guess
            if (st.eventOffset < kFirstOwnByte) continue;   // 0 = place unknown; below: the base Event's state
            if (st.kind != kBool && (st.eventOffset & 3)) continue;  // numbers are 4-byte aligned
            if (s >= gSettingNames.size() || !PlainName(gSettingNames[s])) continue;
            c.push_back({s, st.eventOffset, size, st.kind});
        }
        if (c.empty()) continue;
        std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.off < b.off; });
        uint16_t lo = c.front().off;
        for (const Cand& x : c) {
            if (x.off + x.size - lo > (int)kRecRaw || used >= kMaxItems) break;  // one read of at most kRecRaw bytes
            gItems[used++] = {x.setting, (uint16_t)(x.off - lo), x.size, x.kind};
            p.count++;
            p.len = std::max<uint16_t>(p.len, (uint16_t)(x.off + x.size - lo));
        }
        p.lo = lo;
        if (p.count) gPlanned++;
    }
    gBuilt = gMapped > 0;
}

// ---------------------------------------------------------------- the hook

void Capture(uintptr_t ev) {
    uint64_t idx = gHead.fetch_add(1, std::memory_order_relaxed);
    Slot& s = gRing[idx % kRing];
    s.seq.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    RecordedEvent& r = s.ev;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    r.time = ts.tv_sec + ts.tv_nsec * 1e-9;
    r.event = ev;
    r.metaId = 0xFFFFFFFF;
    r.type = -1;
    r.flags = 0;
    r.rawLen = 0;
    uint8_t hdr[kHeaderSize];
    if (ReadMemory(ev, hdr, sizeof hdr)) {
        memcpy(&r.metaId, hdr + 8, 4);
        memcpy(&r.flags, hdr + 0x24, 2);
        if (r.metaId < kMaxIds && gIdType[r.metaId] != 0xFFFF) {
            r.type = gIdType[r.metaId];
            const Plan& p = gPlan[r.type];
            if (p.count && p.len <= kRecRaw && ReadMemory(ev + p.lo, r.raw, p.len)) r.rawLen = p.len;
        }
    }
    s.seq.store(idx + 1, std::memory_order_release);
}

void OnEventStart(void*, DobbyRegisterContext* ctx) {
    if (!gRecording.load(std::memory_order_acquire)) return;
    gInFlight.fetch_add(1, std::memory_order_seq_cst);
    if (gRecording.load(std::memory_order_seq_cst)) Capture(static_cast<uintptr_t>(ctx->general.regs.x0));
    gInFlight.fetch_sub(1, std::memory_order_release);
}

bool InstallHook(std::string& error) {
    if (gHook == 1) return true;
    if (gHook == -1) {
        error = "the game's event start isn't what 0.35.2 has (see Log)";
        return false;
    }
    uintptr_t base = GameBase();
    uint32_t code[4] = {};
    if (!base || !ReadMemory(base + kEventStart, code, sizeof code) || memcmp(code, kEventStartCode, sizeof code) != 0) {
        Log("Recorder: the event start at 0x%zX isn't what 0.35.2 has (%08X %08X %08X %08X); not hooked",
            (size_t)kEventStart, code[0], code[1], code[2], code[3]);
        gHook = -1;
        error = "the game's event start isn't what 0.35.2 has (see Log)";
        return false;
    }
    if (DobbyInstrument(reinterpret_cast<void*>(base + kEventStart), OnEventStart) != 0) {
        Log("Recorder: couldn't hook the event start at 0x%zX", (size_t)kEventStart);
        gHook = -1;
        error = "couldn't hook the game's event start (see Log)";
        return false;
    }
    gHook = 1;
    Log("Recorder: hooked the game's event start (0x%zX)", (size_t)kEventStart);
    return true;
}

}  // namespace

double RecorderNow() { return MonoNow(); }
bool RecorderOn() { return gRecording.load(); }

bool RecorderStart(std::string& error) {
    if (gRecording) return true;
    if (!GameBase()) {
        error = "the game library isn't loaded";
        return false;
    }
    // the plan may only change while no handler is using it
    for (int i = 0; i < 200 && gInFlight.load() != 0; i++) usleep(500);
    if (gInFlight.load() != 0) {
        error = "the game is busy; try again";
        return false;
    }
    BuildPlan();
    if (!gBuilt) {
        error = "the game's type ids aren't readable yet (enter the world first)";
        return false;
    }
    {  // reads of the game's heap must work, or nothing can be told apart
        volatile uint64_t probe = 0x0FF5E7AB;
        uint64_t got = 0;
        if (!ReadMemory(reinterpret_cast<uintptr_t>(&probe), &got, sizeof got) || got != probe)
            Log("Recorder: this device doesn't allow safe reads outside the game library: types and values won't be "
                "known");
    }
    if (!InstallHook(error)) return false;
    gRecording = true;
    Log("Recorder on: %d types known by id, %d with values to record", gMapped, gPlanned);
    return true;
}

void RecorderStop() {
    if (gRecording.exchange(false)) Log("Recorder off");
}

size_t RecorderDrain(std::vector<RecordedEvent>& out, size_t max) {
    uint64_t head = gHead.load(std::memory_order_acquire);
    if (head - gTail > kRing) {  // the game wrote faster than the window read: the oldest are gone
        gDropped += head - kRing - gTail;
        gTail = head - kRing;
    }
    size_t n = 0;
    while (gTail < head && n < max) {
        Slot& s = gRing[gTail % kRing];
        uint64_t a = s.seq.load(std::memory_order_acquire);
        if (a == 0 || a < gTail + 1) break;  // still being written: next frame
        if (a > gTail + 1) {                  // written over already
            gDropped++;
            gTail++;
            continue;
        }
        RecordedEvent copy;
        memcpy(&copy, &s.ev, sizeof copy);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != a) {
            gDropped++;
        } else {
            out.push_back(copy);
            n++;
        }
        gTail++;
    }
    return n;
}

void RecorderDiscard() {
    gTail = gHead.load(std::memory_order_acquire);
    gDropped = 0;
}

uint64_t RecorderDropped() { return gDropped; }

std::string RecorderStatus() {
    char b[200];
    if (gHook == -1) snprintf(b, sizeof b, "Not available: the game's event start isn't what 0.35.2 has");
    else if (!gBuilt) snprintf(b, sizeof b, "Hook at 0x%zX, placed when you press Start", (size_t)kEventStart);
    else snprintf(b, sizeof b, "Hook at 0x%zX  ·  %d types known by id  ·  %d with values", (size_t)kEventStart,
                  gMapped, gPlanned);
    return b;
}

static std::string Fmt(float v) {
    char b[48];
    snprintf(b, sizeof b, "%.7g", v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::vector<RecValue> RecordedValues(int type, const uint8_t* raw, size_t len) {
    std::vector<RecValue> out;
    if (type < 0 || (size_t)type >= gTypeCount || !gBuilt) return out;
    const Plan& p = gPlan[type];
    for (uint16_t k = 0; k < p.count; k++) {
        const Item& it = gItems[p.first + k];
        if (it.off + it.size > len) break;
        const LabSetting& st = Setting(it.setting);
        const uint8_t* v = raw + it.off;
        RecValue r;
        r.setting = it.setting;
        r.name = gSettingNames[it.setting];
        bool ok = true;
        if (it.kind == kBool) {
            if (v[0] > 1) ok = false;  // not a bool after all
            r.lua = v[0] ? "true" : "false";
            r.text = v[0] ? "on" : "off";
        } else if (it.kind == kInt) {
            int32_t x;
            memcpy(&x, v, 4);
            if (st.hasRange == 1 && (x < st.minValue || x > st.maxValue)) ok = false;
            r.lua = r.text = std::to_string(x);
        } else {
            int count = it.size / 4;
            float f[4];
            memcpy(f, v, it.size);
            std::string l = count > 1 ? "{" : "", t = count > 1 ? "(" : "";
            for (int j = 0; j < count; j++) {
                if (!std::isfinite(f[j]) || std::fabs(f[j]) > 1e9f) ok = false;
                if (count == 1 && st.hasRange == 1 && (f[j] < st.minValue || f[j] > st.maxValue)) ok = false;
                char b[32];
                snprintf(b, sizeof b, "%.4g", f[j]);
                l += (j ? ", " : "") + Fmt(f[j]);
                t += std::string(j ? ", " : "") + b;
            }
            r.lua = l + (count > 1 ? "}" : "");
            r.text = t + (count > 1 ? ")" : "");
        }
        if (ok) out.push_back(std::move(r));  // values that don't look like their type are left at the default
    }
    return out;
}

int RecordableSettings(int type) {
    if (type < 0 || (size_t)type >= gTypeCount || !gBuilt) return 0;
    return gPlan[type].count;
}

}  // namespace lab
