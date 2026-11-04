// Game access: finding libBootloader.so, crash-safe reads, running Lua, the type table.
#include "lab.h"

#include "dobby.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <time.h>
#include <vector>

namespace lab {

#include "generated/lab_data.inc"

const char* const kAreaNames[kAreaCount] = {
    "Avatar & movement", "Camera & photo", "Weather, sky & visuals", "Music & sound", "Menus & UI",
    "Social & multiplayer", "Levels, travel & teleport", "Spirits, NPCs & creatures", "Quests, events & progression",
    "Props, toys & minigames", "Other", "NPC behaviour (internal)", "Engine systems (internal)", "Server & account",
};

const LabType* Types(size_t* count) {
    *count = LAB_TYPE_COUNT;
    return kTypes;
}
const LabSetting& Setting(size_t i) { return kSettings[i]; }
const LabEnum& Enum(size_t i) { return kEnums[i]; }
uint32_t EnumOptionAddr(size_t i) { return kEnumOptions[i]; }
uint32_t NoteAddr(size_t i) { return kNotes[i]; }
uint32_t RegisterFunction() { return LAB_REGISTER_FN; }

// ---------------------------------------------------------------- class tree

static std::map<uint32_t, size_t> gByObject;  // type object -> type index

static uint32_t ParentOf(uint32_t object) {
    auto it = gByObject.find(object);
    if (it != gByObject.end() && kTypes[it->second].parentObject)
        return kTypes[it->second].parentObject;
    // not known from the file: the type object keeps its parent accessor at +0x30 (set when the game registers it)
    uintptr_t base = GameBase(), accessor = 0;
    if (!base || !ReadMemory(base + object + 0x30, &accessor, sizeof accessor))
        return 0;
    if (!accessor)
        return 0xFFFFFFFF;  // no parent: a root type
    uintptr_t off = accessor - base;
    for (const auto& a : kAccessorParents)
        if (a[0] && a[0] == off)
            return a[1];
    return 0;
}

EventStatus EventStatusOf(size_t index, std::vector<size_t>* ancestors) {
    if (gByObject.empty())
        for (size_t i = 0; i < LAB_TYPE_COUNT; i++)
            if (kTypes[i].typeObject) gByObject[kTypes[i].typeObject] = i;
    uint32_t object = kTypes[index].typeObject;
    if (!object)
        return kEventUnknown;
    if (object == LAB_EVENT_TYPE_OBJECT || object == LAB_ROOT_TYPE_OBJECT)
        return kNotEvent;  // the bases themselves
    for (int depth = 0; depth < 32; depth++) {
        uint32_t parent = ParentOf(object);
        if (parent == LAB_EVENT_TYPE_OBJECT)
            return kIsEvent;
        if (parent == LAB_ROOT_TYPE_OBJECT || parent == 0xFFFFFFFF)
            return kNotEvent;  // reached the root without passing Event
        if (!parent)
            return kEventUnknown;  // the chain can't be followed
        auto it = gByObject.find(parent);
        if (ancestors && it != gByObject.end()) ancestors->push_back(it->second);
        object = parent;
    }
    return kEventUnknown;
}

// Sky 0.35.2 addresses (offsets from the start of libBootloader.so)
namespace addr {
constexpr uintptr_t kGamePointer = 0x33A5E80;  // pointer to the game's main object
constexpr uintptr_t kGameLuaState = 0x20;      // lua_State* inside the game object
constexpr uintptr_t kRunLua = 0x29ADFA4;       // int (lua_State*, const char* script), 0 = ok
constexpr uintptr_t kFrameHook = 0x17BB458;    // called every frame on the game's main thread
constexpr uint32_t kFrameHookFirst = 0xA9BC7BFD;  // its first instruction: stp x29, x30, [sp, #-0x40]!
}  // namespace addr

// ---------------------------------------------------------------- base & memory

static uintptr_t gImageEnd = 0;  // end of libBootloader.so's loaded image
struct Segment { uintptr_t start, end; uint32_t flags; };
static std::vector<Segment> gSegments;  // loaded segments (PF_R/PF_W/PF_X)
static uintptr_t gRelroStart = 0, gRelroEnd = 0;  // read-only after loading

static int FindBase(dl_phdr_info* info, size_t, void* data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "libBootloader.so")) {
        *static_cast<uintptr_t*>(data) = info->dlpi_addr;
        for (int i = 0; i < info->dlpi_phnum; i++) {
            const auto& ph = info->dlpi_phdr[i];
            uintptr_t start = info->dlpi_addr + ph.p_vaddr, end = start + ph.p_memsz;
            if (ph.p_type == PT_LOAD) {
                if (end > gImageEnd) gImageEnd = end;
                gSegments.push_back({start, end, ph.p_flags});
            }
            if (ph.p_type == PT_GNU_RELRO) { gRelroStart = start; gRelroEnd = end; }
        }
        return 1;
    }
    return 0;
}

uintptr_t GameBase() {
    static uintptr_t base = 0;
    if (!base)
        dl_iterate_phdr(FindBase, &base);
    return base;
}

// process_vm_readv as a direct system call. Not the libc function by name: Canvas's libciphered.so exports a
// variable called process_vm_readv (its Cheat Engine server), and a mod's lookup can land on it and crash.
static long RawProcessVmReadv(long pid, const iovec* local, long nlocal, const iovec* remote, long nremote) {
    register long x0 __asm__("x0") = pid;
    register long x1 __asm__("x1") = reinterpret_cast<long>(local);
    register long x2 __asm__("x2") = nlocal;
    register long x3 __asm__("x3") = reinterpret_cast<long>(remote);
    register long x4 __asm__("x4") = nremote;
    register long x5 __asm__("x5") = 0;
    register long x8 __asm__("x8") = 270;  // __NR_process_vm_readv on arm64
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory", "cc");
    return x0;  // bytes read, or -errno
}

bool ReadMemory(uintptr_t a, void* out, size_t len) {
    if (!a || !len)
        return false;
    static const long pid = getpid();
    iovec local{out, len}, remote{reinterpret_cast<void*>(a), len};
    long n = RawProcessVmReadv(pid, &local, 1, &remote, 1);
    if (n == static_cast<long>(len))
        return true;
    // Where the system doesn't allow process_vm_readv, read directly, but only inside libBootloader.so's image
    if (n == -ENOSYS || n == -EPERM || n == -EACCES) {
        uintptr_t base = GameBase();
        if (base && a >= base && a + len <= gImageEnd) {
            memcpy(out, reinterpret_cast<const void*>(a), len);
            return true;
        }
    }
    return false;
}

bool WriteGameMemory(uint32_t offset, const void* data, size_t len) {
    uintptr_t base = GameBase();
    if (!base || !len) return false;
    uintptr_t a = base + offset;
    for (const auto& seg : gSegments) {
        if (a < seg.start || a + len > seg.end) continue;
        if (seg.flags & PF_X) {
            // game code: make the page(s) writable for the moment, write, put the protection back
            uintptr_t page = a & ~uintptr_t(4095), pend = (a + len + 4095) & ~uintptr_t(4095);
            if (mprotect(reinterpret_cast<void*>(page), pend - page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
                Log("Write 0x%x: the game code can't be made writable (errno %d)", offset, errno);
                return false;
            }
            memcpy(reinterpret_cast<void*>(a), data, len);
            mprotect(reinterpret_cast<void*>(page), pend - page, PROT_READ | PROT_EXEC);
            __builtin___clear_cache(reinterpret_cast<char*>(a), reinterpret_cast<char*>(a + len));
            return true;
        }
        if ((seg.flags & PF_W) && !(a < gRelroEnd && a + len > gRelroStart)) {
            memcpy(reinterpret_cast<void*>(a), data, len);  // the game's own variables (.data / .bss)
            return true;
        }
        Log("Write 0x%x: not a writable part of the game", offset);
        return false;
    }
    Log("Write 0x%x: outside the game library", offset);
    return false;
}

std::string ReadString(uintptr_t a, size_t max) {
    std::string s;
    char buf[64];
    while (s.size() < max) {
        // stay inside the page, so a string near the end of mapped memory is still read
        size_t n = 64 - ((a + s.size()) & 63);
        if (!ReadMemory(a + s.size(), buf, n))
            break;
        for (size_t i = 0; i < n; i++) {
            if (!buf[i])
                return s;
            s.push_back(buf[i]);
        }
    }
    return s;
}

std::string GameString(uint32_t offset) {
    uintptr_t base = GameBase();
    return base && offset ? ReadString(base + offset) : std::string();
}

uint32_t Fnv1a(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s)
        h = (h ^ c) * 0x01000193u;
    return h;
}

uintptr_t GameObject() {
    uintptr_t base = GameBase(), game = 0;
    if (!base || !ReadMemory(base + addr::kGamePointer, &game, sizeof game))
        return 0;
    return game;
}

// Runs Lua right now, on whatever thread calls it.
static int RunLuaHere(const std::string& script) {
    uintptr_t base = GameBase(), game = GameObject(), L = 0;
    if (!base || !game || !ReadMemory(game + addr::kGameLuaState, &L, sizeof L) || !L)
        return -1;
    using RunFn = int (*)(void*, const char*);
    return reinterpret_cast<RunFn>(base + addr::kRunLua)(reinterpret_cast<void*>(L), script.c_str());
}

// Lua isn't thread-safe: the game runs its own Lua on its main thread, so scripts from the window are queued and run
// at the start of a function the game calls every frame on that thread (TheVoid's Lua update hook, 0x17BB310 in
// 0.35.1). If the hook can't be placed, scripts run directly as before.
namespace {
// Pressing buttons fast queued many scripts that all ran in one frame (start, switch off, start again...), which
// could crash the game. Now: a newer job for the same key replaces a waiting one (only the latest press counts), at
// most 4 jobs run per frame, and jobs for the same key run at least 0.15 s apart.
struct Job { std::string script, what, key; };
std::mutex gJobMutex;
std::deque<Job> gJobs;
std::map<std::string, double> gLastRun;  // key -> when its last job ran
std::atomic<int> gHook{0};  // 0 = not tried, 1 = active, -1 = failed (run directly)

double NowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Native calls into the game (RunOnGameThread): a few fixed slots, run in the frame hook like Lua.
struct NativeCall { void (*fn)(uintptr_t); uintptr_t arg; };
NativeCall gNative[8];
size_t gNativeCount = 0;

void OnFrame(void*, DobbyRegisterContext*) {
    NativeCall calls[8];
    size_t ncalls = 0;
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        ncalls = gNativeCount;
        for (size_t i = 0; i < ncalls; i++) calls[i] = gNative[i];
        gNativeCount = 0;
    }
    for (size_t i = 0; i < ncalls; i++) calls[i].fn(calls[i].arg);
    std::deque<Job> jobs;
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        double now = NowSeconds();
        for (auto it = gJobs.begin(); it != gJobs.end() && jobs.size() < 4;) {
            if (!it->key.empty()) {
                auto last = gLastRun.find(it->key);
                if (last != gLastRun.end() && now - last->second < 0.15) {
                    ++it;  // too soon after the last one for this key: next frame
                    continue;
                }
                gLastRun[it->key] = now;
            }
            jobs.push_back(std::move(*it));
            it = gJobs.erase(it);
        }
    }
    for (auto& j : jobs) {
        int r = RunLuaHere(j.script);
        if (r == -1) Log("%s: enter the world first", j.what.empty() ? "Lua" : j.what.c_str());
        else if (r != 0) Log("%s: the game reported a script error (%d)", j.what.empty() ? "Lua" : j.what.c_str(), r);
    }
}

void InstallFrameHook() {
    uintptr_t base = GameBase();
    if (!base) return;
    uint32_t first = 0;
    if (!ReadMemory(base + addr::kFrameHook, &first, 4) || first != addr::kFrameHookFirst) {
        Log("Lua: the frame function isn't what 0.35.2 has (0x%08X); running Lua directly", first);
        gHook = -1;
        return;
    }
    if (DobbyInstrument(reinterpret_cast<void*>(base + addr::kFrameHook), OnFrame) != 0) {
        Log("Lua: couldn't hook the frame function; running Lua directly");
        gHook = -1;
        return;
    }
    gHook = 1;
    Log("Lua runs on the game's main thread (frame hook at 0x%zX)", (size_t)addr::kFrameHook);
}
}  // namespace

std::string LuaStatus() {
    std::lock_guard<std::mutex> lk(gJobMutex);
    char b[160];
    if (gHook == 1) snprintf(b, sizeof b, "Lua: game main thread, %zu waiting", gJobs.size());
    else if (gHook == -1) snprintf(b, sizeof b, "Lua: direct (frame hook not placed; see Log)");
    else snprintf(b, sizeof b, "Lua: not used yet");
    return b;
}

bool RunOnGameThread(void (*fn)(uintptr_t), uintptr_t arg) {
    if (gHook == 0) InstallFrameHook();
    if (gHook != 1 || !GameObject()) return false;  // never from this thread: only in the game's frame
    std::lock_guard<std::mutex> lk(gJobMutex);
    for (size_t i = 0; i < gNativeCount; i++)
        if (gNative[i].fn == fn) {  // still waiting: replaced, one call per function per frame
            gNative[i].arg = arg;
            return true;
        }
    if (gNativeCount >= 8) return false;
    gNative[gNativeCount++] = {fn, arg};
    return true;
}

int RunLua(const std::string& script, const std::string& what, const std::string& key) {
    if (gHook == 0) InstallFrameHook();
    if (gHook != 1) return RunLuaHere(script);
    if (!GameObject()) return -1;
    std::lock_guard<std::mutex> lk(gJobMutex);
    if (!key.empty())
        for (auto& j : gJobs)
            if (j.key == key) {  // still waiting: the newer press replaces it
                j.script = script;
                j.what = what;
                return 0;
            }
    if (gJobs.size() > 256) return -3;  // the game isn't running frames
    gJobs.push_back({script, what, key});
    return 0;
}

// ---------------------------------------------------------------- files next to the mod

std::string DataPath(const char* name) {
    // the mod lives in <files>/mods/libOffsetLab.so; keep its files in <files>
    static std::string dir;
    if (dir.empty()) {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&DataPath), &info) && info.dli_fname) {
            std::string p = info.dli_fname;
            size_t slash = p.rfind('/');
            if (slash != std::string::npos) p.resize(slash);           // .../files/mods
            size_t up = p.rfind('/');
            if (up != std::string::npos && p.substr(up + 1) == "mods") p.resize(up);  // .../files
            dir = p;
        }
    }
    return dir.empty() ? std::string() : dir + "/" + name;
}

std::string ReadTextFile(const std::string& path) {
    std::string out;
    if (path.empty()) return out;
    if (FILE* f = fopen(path.c_str(), "r")) {
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        fclose(f);
    }
    return out;
}

bool WriteTextFile(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fflush(f);
    fsync(fileno(f));  // so it survives the game crashing right after
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- log

static std::mutex gLogMutex;
static std::deque<std::string> gLog;

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "OffsetLab", "%s", buf);
    std::lock_guard<std::mutex> lk(gLogMutex);
    gLog.emplace_back(buf);
    while (gLog.size() > 400)
        gLog.pop_front();
}

std::vector<std::string> LogLines() {
    std::lock_guard<std::mutex> lk(gLogMutex);
    return {gLog.begin(), gLog.end()};
}

}  // namespace lab
