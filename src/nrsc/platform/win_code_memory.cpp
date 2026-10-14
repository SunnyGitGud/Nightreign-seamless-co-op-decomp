// Win32 CodeMemory. Original: protection changed through the VirtualProtect IAT slot 0x180161260, FlushInstructionCache after
// the write (docs/hooks.md section 1, C/S).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <stdexcept>

#include "nrsc/hooks/hook.hpp"

namespace nrsc::platform {
namespace {

class WinCodeMemory final : public CodeMemory {
public:
    void write(std::uintptr_t address, std::span<const std::uint8_t> bytes) override {
        if (bytes.empty()) return;
        void* p = reinterpret_cast<void*>(address);
        DWORD old = 0;
        if (!VirtualProtect(p, bytes.size(), PAGE_EXECUTE_READWRITE, &old))
            throw std::runtime_error("VirtualProtect failed");
        std::memcpy(p, bytes.data(), bytes.size());
        DWORD ignored = 0;
        VirtualProtect(p, bytes.size(), old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), p, bytes.size());
    }

    std::vector<std::uint8_t> read(std::uintptr_t address, std::size_t size) override {
        auto* p = reinterpret_cast<const std::uint8_t*>(address);
        return {p, p + size};
    }

    std::uintptr_t allocate_near(std::uintptr_t near_addr, std::size_t size) override {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const std::uintptr_t gran = si.dwAllocationGranularity;
        const std::uintptr_t range = 0x7ff00000;
        std::uintptr_t lo = near_addr > range ? near_addr - range : 0;
        std::uintptr_t hi = near_addr + range;
        std::uintptr_t min_addr = reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress);
        std::uintptr_t max_addr = reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress);
        if (lo < min_addr) lo = min_addr;
        if (hi > max_addr) hi = max_addr;

        // Walk free regions outward from `near_addr`: first below it (highest address first), then above it.
        std::uintptr_t addr = near_addr & ~(gran - 1);
        while (addr > lo) {  // downward
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi)) break;
            std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_FREE) {
                std::uintptr_t top = (base + mbi.RegionSize) & ~(gran - 1);
                if (top > addr) top = addr & ~(gran - 1);
                if (top >= size && top - size >= base && top - size >= lo)
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(top - size), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                        return reinterpret_cast<std::uintptr_t>(p);
            }
            if (base < gran) break;
            addr = base - gran;
        }
        addr = near_addr & ~(gran - 1);
        while (addr < hi) {  // upward
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof mbi)) break;
            std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            std::uintptr_t next = base + mbi.RegionSize;
            if (mbi.State == MEM_FREE) {
                std::uintptr_t start = (addr + gran - 1) & ~(gran - 1);
                if (start + size <= next && start + size <= hi)
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(start), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                        return reinterpret_cast<std::uintptr_t>(p);
            }
            addr = next;
        }
        throw std::runtime_error("no executable memory near_addr target");
    }
};

}  // namespace

CodeMemory& process_code_memory() {
    static WinCodeMemory instance;
    return instance;
}

}  // namespace nrsc::platform
