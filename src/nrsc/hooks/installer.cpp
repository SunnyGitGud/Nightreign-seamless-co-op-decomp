// hooks::Installer: functional reconstruction of the installer FUN_18001edb0 (docs/hooks.md sections 1 and 9).
// Original: per-site thunk arena (closure + `mov rax,closure; movq xmm5,rax; jmp invoker`) reached through a 16-byte near relay (S).
// Here: a 16-byte relay `jmp [rip+0]; dq detour` in an RWX arena within +-2 GiB of the site; no closures.
#include <cstring>
#include <cstdlib>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/hooks/hook.hpp"

namespace nrsc::hooks {
namespace {

constexpr std::size_t kRelaySize = 16;      // 14 used: FF 25 00 00 00 00 + u64 (C: relays are 16-byte slots)
constexpr std::size_t kArenaSize = 0x10000;
constexpr std::int64_t kReach = 0x7ff00000;  // rel32 reach with a safety margin

[[noreturn]] void fail(const HookSpec& spec, unsigned line) {
    throw FatalError("hook failed: " + spec.name, "nrsc\\hooks\\hooks.cpp", line);
}

std::uintptr_t as_addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

std::int64_t distance(std::uintptr_t a, std::uintptr_t b) {
    return a > b ? std::int64_t(a - b) : -std::int64_t(b - a);
}

std::uint64_t read_u64(platform::CodeMemory& m, std::uintptr_t a) {
    auto b = m.read(a, 8);
    std::uint64_t v;
    std::memcpy(&v, b.data(), 8);
    return v;
}

}  // namespace

Installer::Installer(platform::CodeMemory& memory, InlineBackend* inline_backend)
    : memory_(memory), inline_backend_(inline_backend) {}

std::size_t Installer::installed() const noexcept { return done_.size(); }

void Installer::enable_inline() {
    if (!inline_backend_) throw FatalError("hook failed: no inline backend", "nrsc\\hooks\\hooks.cpp", __LINE__);
    inline_backend_->enable_all();
}

void Installer::install(const HookSpec& spec) {
    // `expect` (S): bytes that must be at the site first; a mismatch means a wrong signature resolution.
    if (!spec.expect.empty() && memory_.read(spec.site, spec.expect.size()) != spec.expect) fail(spec, __LINE__);

    if (spec.kind == Kind::BytePatch) {
        if (spec.bytes.empty()) fail(spec, __LINE__);
        memory_.write(spec.site, spec.bytes);
        done_.push_back(spec);
        return;
    }
    if (spec.kind == Kind::Inline) {
        if (!inline_backend_ || !spec.detour) fail(spec, __LINE__);
        void* trampoline = inline_backend_->create(reinterpret_cast<void*>(spec.site), spec.detour);
        if (spec.original) *spec.original = trampoline;
        done_.push_back(spec);
        return;
    }
    if (!spec.detour) fail(spec, __LINE__);

    if (spec.kind == Kind::VtableSlot) {
        // C: old slot value saved, slot gets the relay. A plain detour pointer is stored directly (the relay only exists to be rel32-reachable).
        std::uint64_t old = read_u64(memory_, spec.site);
        if (spec.original) *spec.original = reinterpret_cast<void*>(old);
        std::uint64_t now = as_addr(spec.detour);
        memory_.write(spec.site, {reinterpret_cast<const std::uint8_t*>(&now), 8});
        done_.push_back(spec);
        return;
    }

    // Relay for the rel32 kinds.
    auto fits = [&](std::uintptr_t a) {
        return std::llabs(distance(a, spec.site)) < kReach;
    };
    if (!(arena_ && arena_cursor_ + kRelaySize <= arena_end_ && fits(arena_) && fits(arena_end_))) {
        try {
            arena_ = memory_.allocate_near(spec.site, kArenaSize);
        } catch (const std::exception&) {
            fail(spec, __LINE__);  // original: "out of memory" (hooks.cpp 0x2f6 / 0x367)
        }
        arena_cursor_ = arena_;
        arena_end_ = arena_ + kArenaSize;
        if (!fits(arena_) || !fits(arena_end_)) fail(spec, __LINE__);
    }
    std::uintptr_t relay = arena_cursor_;
    std::uint8_t code[kRelaySize] = {0xFF, 0x25, 0, 0, 0, 0};
    std::uint64_t target = as_addr(spec.detour);
    std::memcpy(code + 6, &target, 8);
    memory_.write(relay, code);
    arena_cursor_ += kRelaySize;

    auto rel32_to_relay = [&](std::uintptr_t from_end) {
        return std::int32_t(distance(relay, from_end));
    };

    if (spec.kind == Kind::JmpEntry) {
        // C: E9 rel32 at the function entry, no trampoline; *original stays null.
        std::uint8_t patch[5] = {0xE9};
        std::int32_t rel = rel32_to_relay(spec.site + 5);
        std::memcpy(patch + 1, &rel, 4);
        memory_.write(spec.site, patch);
    } else {
        // CallSite / JmpSite (C): verify the opcode, save the old absolute target, retarget the rel32.
        const std::uint8_t opcode = spec.kind == Kind::CallSite ? 0xE8 : 0xE9;
        auto old = memory_.read(spec.site, 5);
        if (old[0] != opcode) fail(spec, __LINE__);
        std::int32_t old_rel;
        std::memcpy(&old_rel, old.data() + 1, 4);
        if (spec.original) *spec.original = reinterpret_cast<void*>(spec.site + 5 + std::intptr_t(old_rel));
        std::int32_t rel = rel32_to_relay(spec.site + 5);
        memory_.write(spec.site + 1, {reinterpret_cast<const std::uint8_t*>(&rel), 4});
    }
    done_.push_back(spec);
}

}  // namespace nrsc::hooks
