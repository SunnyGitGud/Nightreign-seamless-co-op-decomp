// POSIX CodeMemory (tests on Linux): mmap/mprotect. The game build uses win_code_memory.cpp.
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "nrsc/hooks/hook.hpp"

namespace nrsc::platform {
namespace {

std::uintptr_t page_size() {
    static const std::uintptr_t p = std::uintptr_t(sysconf(_SC_PAGESIZE));
    return p;
}

// Current protection of the mapping containing `address`, from /proc/self/maps (PROT_READ if unknown).
int protection_at(std::uintptr_t address) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return PROT_READ;
    int prot = PROT_READ;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        unsigned long lo = 0, hi = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (address >= lo && address < hi) {
            prot = 0;
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    std::fclose(f);
    return prot;
}

class PosixCodeMemory final : public CodeMemory {
public:
    void write(std::uintptr_t address, std::span<const std::uint8_t> bytes) override {
        if (bytes.empty()) return;
        std::uintptr_t first = address & ~(page_size() - 1);
        std::uintptr_t last = (address + bytes.size() - 1) & ~(page_size() - 1);
        for (std::uintptr_t page = first; page <= last; page += page_size()) {
            int old = protection_at(page);
            bool changed = !(old & PROT_WRITE);
            if (changed && mprotect(reinterpret_cast<void*>(page), page_size(), old | PROT_WRITE) != 0)
                throw std::runtime_error("mprotect(write) failed");
            std::size_t lo = page == first ? address : page;
            std::uintptr_t end = address + bytes.size();
            std::uintptr_t hi = page + page_size() < end ? page + page_size() : end;
            std::memcpy(reinterpret_cast<void*>(lo), bytes.data() + (lo - address), hi - lo);
            if (changed) mprotect(reinterpret_cast<void*>(page), page_size(), old);
        }
        __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address + bytes.size()));
    }

    std::vector<std::uint8_t> read(std::uintptr_t address, std::size_t size) override {
        auto* p = reinterpret_cast<const std::uint8_t*>(address);
        return {p, p + size};
    }

    std::uintptr_t allocate_near(std::uintptr_t near, std::size_t size) override {
        constexpr std::uintptr_t kStep = 0x10000, kRange = 0x7ff00000;  // keep a margin below 2 GiB
        std::uintptr_t len = (size + page_size() - 1) & ~(page_size() - 1);
        std::uintptr_t base = near & ~(kStep - 1);
        for (std::uintptr_t d = 0; d < kRange; d += kStep) {
            for (int dir = 0; dir < 2; ++dir) {
                if (dir == 1 && d == 0) continue;
                std::uintptr_t want = dir == 0 ? base + d : base - d;
                if (dir == 1 && d > base) continue;
                void* p = mmap(reinterpret_cast<void*>(want), len, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
                if (p == MAP_FAILED) continue;
                if (reinterpret_cast<std::uintptr_t>(p) != want) { munmap(p, len); continue; }
                return want;
            }
        }
        throw std::runtime_error("no executable memory near target");
    }
};

}  // namespace

CodeMemory& process_code_memory() {
    static PosixCodeMemory instance;
    return instance;
}

}  // namespace nrsc::platform
