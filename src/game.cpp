// Game access: finding libBootloader.so, crash-safe reads, running Lua, the type table.
#include "lab.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

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

// Sky 0.35.2 addresses (offsets from the start of libBootloader.so)
namespace addr {
constexpr uintptr_t kGamePointer = 0x33A5E80;  // pointer to the game's main object
constexpr uintptr_t kGameLuaState = 0x20;      // lua_State* inside the game object
constexpr uintptr_t kRunLua = 0x29ADFA4;       // int (lua_State*, const char* script), 0 = ok
}  // namespace addr

// ---------------------------------------------------------------- base & memory

static uintptr_t gImageEnd = 0;  // end of libBootloader.so's loaded image

static int FindBase(dl_phdr_info* info, size_t, void* data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "libBootloader.so")) {
        *static_cast<uintptr_t*>(data) = info->dlpi_addr;
        for (int i = 0; i < info->dlpi_phnum; i++)
            if (info->dlpi_phdr[i].p_type == PT_LOAD) {
                uintptr_t end = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
                if (end > gImageEnd) gImageEnd = end;
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

int RunLua(const std::string& script) {
    uintptr_t base = GameBase(), game = GameObject(), L = 0;
    if (!base || !game || !ReadMemory(game + addr::kGameLuaState, &L, sizeof L) || !L)
        return -1;
    using RunFn = int (*)(void*, const char*);
    return reinterpret_cast<RunFn>(base + addr::kRunLua)(reinterpret_cast<void*>(L), script.c_str());
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
