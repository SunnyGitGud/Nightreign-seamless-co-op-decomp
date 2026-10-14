#pragma once
// Hook framework contract (docs/plan_gaps.md, phase 2). This is a functional reconstruction of the original's
// installer FUN_18001edb0 / 0x1800221e0 (docs/hooks.md sections 1 and 9), not a byte-identical one. The original routes every site through a
// near thunk arena (mov rax,closure; movq xmm5,rax; mov r11,invoker; jmp r11). Here each site is redirected to a
// near relay (jmp [rip+0]; dq detour), and the detour is a plain function that reaches its state through
// nrsc::hooks::state() and its original through the `original` pointer it registered.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nrsc::platform {

// Process code memory. Windows: VirtualAlloc/VirtualProtect/FlushInstructionCache. POSIX (tests): mmap/mprotect.
class CodeMemory {
public:
    virtual ~CodeMemory() = default;
    // Writes `bytes` at `address`, making the page writable for the write and restoring its protection afterwards,
    // then flushes the instruction cache. Throws std::runtime_error on failure.
    virtual void write(std::uintptr_t address, std::span<const std::uint8_t> bytes) = 0;
    // Reads `size` bytes at `address` (the caller guarantees the range is mapped).
    virtual std::vector<std::uint8_t> read(std::uintptr_t address, std::size_t size) = 0;
    // Executable RWX memory within +-2 GiB of `near`, so a rel32 can reach it. Throws if none is found.
    virtual std::uintptr_t allocate_near(std::uintptr_t near, std::size_t size) = 0;
};

CodeMemory& process_code_memory();  // the platform implementation for this process

}  // namespace nrsc::platform

namespace nrsc::hooks {

enum class Kind {
    CallSite,    // E8 rel32 at `site`: retarget the call; *original = old call target (docs/hooks.md "call-patch")
    JmpSite,     // E9 rel32 at `site`: retarget the jump; *original = old jump target ("jmp-patch")
    JmpEntry,    // write E9 rel32 at function entry `site` (5 bytes, no trampoline); *original stays null
    VtableSlot,  // `site` is a vtable slot (u64): *original = old value, slot = detour
    Inline,      // MinHook-style inline hook on function `site`; *original = trampoline to the relocated prologue
    BytePatch,   // overwrite `bytes` at `site` (no detour)
};

struct HookSpec {
    std::string name;                // e.g. "hook#20 CSQuickMatchLobbyContext+0x20"; used in errors and logs
    Kind kind;
    std::uintptr_t site;             // live address in the game (from the signature table)
    void* detour = nullptr;          // not used for BytePatch
    void** original = nullptr;       // receives the original; may be null when the detour never calls it
    std::vector<std::uint8_t> bytes; // BytePatch only
    std::vector<std::uint8_t> expect;  // optional: bytes that must be at `site` before patching (else FatalError)
};

// Back end for Kind::Inline (MinHook in the DLL build; tests can provide a fake).
class InlineBackend {
public:
    virtual ~InlineBackend() = default;
    virtual void* create(void* target, void* detour) = 0;  // returns the trampoline; throws on failure
    virtual void enable_all() = 0;
};

class Installer {
public:
    explicit Installer(platform::CodeMemory& memory, InlineBackend* inline_backend = nullptr);
    // Validates `expect`, builds a relay when the site needs a rel32 into it, patches, and records the hook.
    // Errors throw nrsc::FatalError("hook failed: <name>", "nrsc\\hooks\\hooks.cpp", <line>).
    void install(const HookSpec& spec);
    void enable_inline();  // enables every Kind::Inline hook (MinHook's MH_EnableHook(MH_ALL_HOOKS) step)
    std::size_t installed() const noexcept;

private:
    struct Impl;
    platform::CodeMemory& memory_;
    InlineBackend* inline_backend_;
    std::vector<HookSpec> done_;
    std::uintptr_t arena_ = 0, arena_cursor_ = 0, arena_end_ = 0;
};

}  // namespace nrsc::hooks
