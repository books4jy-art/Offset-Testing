// Exported functions of libBootloader.so, read from the game's own dynamic symbol table at runtime, and the call
// watcher (counts calls and keeps the first four arguments, using Dobby instrumentation).
#include "lab.h"

#include <cxxabi.h>
#include <elf.h>
#include <link.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "dobby.h"

namespace lab {

namespace {

struct DynInfo {
    uintptr_t base = 0;
    const ElfW(Phdr)* phdr = nullptr;
    size_t phnum = 0;
};

int FindLib(dl_phdr_info* info, size_t, void* data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "libBootloader.so")) {
        auto* d = static_cast<DynInfo*>(data);
        d->base = info->dlpi_addr;
        d->phdr = info->dlpi_phdr;
        d->phnum = info->dlpi_phnum;
        return 1;
    }
    return 0;
}

template <typename T>
bool Read(uintptr_t a, T& out) { return ReadMemory(a, &out, sizeof(T)); }

// a dynamic-section pointer: bionic leaves them unrelocated (file addresses), glibc relocates them
uintptr_t Ptr(uintptr_t base, uintptr_t v) { return v < base ? base + v : v; }

std::string Demangle(const char* name) {
    int status = 0;
    char* out = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string s = (status == 0 && out) ? out : name;
    free(out);
    return s;
}

size_t SymbolCount(uintptr_t base, uintptr_t hash, uintptr_t gnuHash) {
    if (hash) {  // DT_HASH: nchain = number of symbols
        uint32_t h[2];
        if (ReadMemory(hash, h, sizeof h))
            return h[1];
    }
    if (!gnuHash)
        return 0;
    uint32_t hdr[4];  // nbuckets, symoffset, bloom size, bloom shift
    if (!ReadMemory(gnuHash, hdr, sizeof hdr))
        return 0;
    uintptr_t buckets = gnuHash + 16 + hdr[2] * sizeof(uintptr_t);
    uintptr_t chains = buckets + hdr[0] * 4;
    uint32_t last = 0;
    for (uint32_t i = 0; i < hdr[0]; i++) {
        uint32_t b = 0;
        if (Read(buckets + i * 4, b) && b > last)
            last = b;
    }
    if (last < hdr[1])
        return hdr[1];
    for (;;) {  // walk the last chain to its end
        uint32_t c = 0;
        if (!Read(chains + (last - hdr[1]) * 4, c))
            break;
        if (c & 1)
            break;
        last++;
    }
    return last + 1;
}

}  // namespace

const std::vector<ExportedFn>& ExportedFunctions() {
    static std::vector<ExportedFn> fns;
    static bool done = false;
    if (done)
        return fns;
    DynInfo info;
    dl_iterate_phdr(FindLib, &info);
    if (!info.base)
        return fns;
    done = true;
    uintptr_t dyn = 0;
    for (size_t i = 0; i < info.phnum; i++)
        if (info.phdr[i].p_type == PT_DYNAMIC)
            dyn = info.base + info.phdr[i].p_vaddr;
    uintptr_t symtab = 0, strtab = 0, hash = 0, gnuHash = 0;
    for (ElfW(Dyn) e; dyn && Read(dyn, e) && e.d_tag != DT_NULL; dyn += sizeof e) {
        if (e.d_tag == DT_SYMTAB) symtab = Ptr(info.base, e.d_un.d_ptr);
        if (e.d_tag == DT_STRTAB) strtab = Ptr(info.base, e.d_un.d_ptr);
        if (e.d_tag == DT_HASH) hash = Ptr(info.base, e.d_un.d_ptr);
        if (e.d_tag == DT_GNU_HASH) gnuHash = Ptr(info.base, e.d_un.d_ptr);
    }
    size_t n = SymbolCount(info.base, hash, gnuHash);
    for (size_t i = 0; symtab && strtab && i < n; i++) {
        ElfW(Sym) s;
        if (!Read(symtab + i * sizeof s, s))
            break;
        if (ELF64_ST_TYPE(s.st_info) != STT_FUNC || s.st_shndx == SHN_UNDEF || !s.st_value)
            continue;
        std::string raw = ReadString(strtab + s.st_name, 1024);
        if (raw.empty())
            continue;
        fns.push_back({static_cast<uintptr_t>(s.st_value), static_cast<size_t>(s.st_size), Demangle(raw.c_str())});
    }
    std::sort(fns.begin(), fns.end(), [](const ExportedFn& a, const ExportedFn& b) { return a.offset < b.offset; });
    Log("Read %zu exported functions from libBootloader.so", fns.size());
    return fns;
}

// ---------------------------------------------------------------- call watcher

namespace {

constexpr int kMaxWatches = 48;
struct Slot {
    std::atomic<uintptr_t> address{0};  // absolute address, 0 = free
    std::atomic<uint64_t> calls{0};
    uint64_t args[4]{};
    uintptr_t offset = 0;
    std::string label;
    bool active = false;
};
Slot gSlots[kMaxWatches];
std::mutex gWatchMutex;

void OnCall(void* address, DobbyRegisterContext* ctx) {
    auto a = reinterpret_cast<uintptr_t>(address);
    for (auto& s : gSlots) {
        if (s.address.load(std::memory_order_relaxed) == a) {
            s.calls.fetch_add(1, std::memory_order_relaxed);
            s.args[0] = ctx->general.regs.x0;
            s.args[1] = ctx->general.regs.x1;
            s.args[2] = ctx->general.regs.x2;
            s.args[3] = ctx->general.regs.x3;
            return;
        }
    }
}

}  // namespace

bool StartWatch(uintptr_t offset, const std::string& label, std::string& error) {
    std::lock_guard<std::mutex> lk(gWatchMutex);
    uintptr_t base = GameBase();
    if (!base) {
        error = "libBootloader.so isn't loaded";
        return false;
    }
    uintptr_t a = base + offset;
    Slot* free = nullptr;
    for (auto& s : gSlots) {
        if (s.offset == offset && s.active) {
            error = "already watched";
            return false;
        }
        if (!s.active && !free)
            free = &s;
    }
    if (!free) {
        error = "too many watches (48); stop one first";
        return false;
    }
    uint32_t insn = 0;
    if (!ReadMemory(a, &insn, sizeof insn)) {
        error = "address isn't readable";
        return false;
    }
    free->offset = offset;
    free->label = label;
    free->calls.store(0);
    for (auto& v : free->args) v = 0;
    free->address.store(a);
    if (DobbyInstrument(reinterpret_cast<void*>(a), OnCall) != 0) {
        free->address.store(0);
        error = "Dobby couldn't instrument this address";
        return false;
    }
    free->active = true;
    Log("Watching 0x%zx (%s)", (size_t)offset, label.c_str());
    return true;
}

void StopWatch(uintptr_t offset) {
    std::lock_guard<std::mutex> lk(gWatchMutex);
    for (auto& s : gSlots) {
        if (s.active && s.offset == offset) {
            DobbyDestroy(reinterpret_cast<void*>(s.address.load()));
            s.active = false;
            s.address.store(0);
            Log("Stopped watching 0x%zx", (size_t)offset);
        }
    }
}

std::vector<Watch> Watches() {
    std::lock_guard<std::mutex> lk(gWatchMutex);
    std::vector<Watch> out;
    for (auto& s : gSlots)
        if (s.active)
            out.push_back({s.offset, s.label, s.calls.load(), {s.args[0], s.args[1], s.args[2], s.args[3]}, true});
    return out;
}

}  // namespace lab
