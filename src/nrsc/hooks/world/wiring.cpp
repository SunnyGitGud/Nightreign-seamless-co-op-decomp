#include "nrsc/hooks/world/wiring.hpp"

#include <cstring>

#include "nrsc/hooks/world/game_layout.hpp"
#include "nrsc/hooks/world/world_specs.hpp"

namespace nrsc::hooks::world {

using namespace layout;

namespace {

// Pointer stored in a game global whose address is `slot` (0 slot = absent).
void* read_global(std::uint64_t slot) {
    return slot ? *reinterpret_cast<void* const*>(static_cast<std::uintptr_t>(slot)) : nullptr;
}

constexpr std::size_t kSignInWorldField = 0x174e8;  // world_chr_man.md section 4
constexpr std::size_t kSessionSignInState = 0x58;   // CSSessionManager +0x58 (== 0 gates the SignIn call)
constexpr std::size_t kAllocPrepareVSlot = 0x68;    // allocator vtable +0x68 (bloodstain_man.hpp)
constexpr std::size_t kAllocAllocateVSlot = 0x50;   // allocator vtable +0x50
constexpr std::size_t kFakeCondSize = 0x40;
constexpr std::size_t kCondVtable = 0x00;

}  // namespace

void set_party_session_active(bool active) { set_session_word(active ? kPartySessionWord : 0); }

void set_session_word(std::uint32_t value) {
    auto& s = state();
    // Only bits 0, 2 and 5 belong to the session state; bit 6 is the lobby flag mirrored by hook #22.
    const auto apply = [value](std::uint64_t word) {
        return (word & ~std::uint64_t{kSessionWordMask}) | (value & kSessionWordMask);
    };
    s.mod_flags.store(static_cast<std::uint32_t>(apply(s.mod_flags.load())));
    s.mode_word.store(static_cast<std::uint32_t>(apply(s.mode_word.load())));
    s.quick_match_flags = apply(s.quick_match_flags);
}

AutoControlTask auto_control_task_from_object(void* object) {
    AutoControlTask task;
    if (!object) return task;
    task.is_registered = [object] { return load_ptr(object, 0x10) != nullptr; };
    task.do_register = [object] {
        using Register = void (*)(void*, std::uint32_t);
        const auto fn = load<Register>(object, 0x28);
        if (fn) fn(object, load<std::uint32_t>(object, 0x20));
    };
    return task;
}

void wire(const std::function<std::uint64_t(unsigned)>& at) {
    auto& s = state();

    // CSSessionManager singleton: **(ctx[0x68]+0x10). C (live: ctx[0x68]+0x10 == 0x143c21ec8 == table slot 0x190).
    if (const auto slot = at(wiring_sig::kSessionManagerGlobal))
        s.session_manager = [slot] { return read_global(slot); };

    // The 0x458-byte global: **ctx[0x78]. C (live: ctx[0x78][0] == 0x143c078d0 == table slot 0x1b0).
    if (const auto slot = at(wiring_sig::kGameDataManGlobal))
        s.game_data_man = [slot] { return read_global(slot); };

    // Lobby member vector owner: ctx[0x60]->[0x28]->[0]. C (live: ctx[0x60]+0x28 == 0x143c21fc8 == table slot 0x198;
    // the object has +0x30/+0x38 = a 2 * 0xf0 byte vector).
    if (const auto slot = at(wiring_sig::kQuickMatchGlobal))
        s.member_vector_owner = [slot]() -> const void* { return read_global(slot); };

    // ChrMan: **ctx[0xb0] and ctx[0xb0]+8. C (live: ctx[0xb0] = {0x143c134f8, 0x14070ca60}; table slots 0x1d0 and 0xc8).
    if (const auto slot = at(wiring_sig::kChrManGlobal)) s.chr_manager = [slot] { return read_global(slot); };
    if (const auto fn = at(wiring_sig::kFindChrFn)) {
        s.find_chr_by_handle = [fn](void* manager, std::uint32_t* handle) -> void* {
            using Find = void* (*)(void*, std::uint32_t*);
            return reinterpret_cast<Find>(static_cast<std::uintptr_t>(fn))(manager, handle);
        };
    }

    // Bloodstain allocator: ctx[0xa8]+8 = the game function 0x14215cdb0(block) -> allocator (table slot 0xa8); the
    // allocator's vtable +0x68 (prepare) and +0x50 (allocate) are called by P12. C (bytes 0x180027090..0x1800270cf).
    if (const auto fn = at(wiring_sig::kAllocatorForFn)) {
        s.bloodstain_allocator_for = [fn](void* arg) -> const cs::BloodstainAllocator* {
            using AllocatorFor = void* (*)(void*);
            void* allocator = reinterpret_cast<AllocatorFor>(static_cast<std::uintptr_t>(fn))(arg);
            if (!allocator) return nullptr;
            thread_local cs::BloodstainAllocator holder;  // consumed immediately by P12
            holder.prepare = [allocator](void* block) {
                using Prepare = void (*)(void*, void*);
                const void* const* vtable = load<const void* const*>(allocator, 0);
                reinterpret_cast<Prepare>(const_cast<void*>(vtable[kAllocPrepareVSlot / 8]))(allocator, block);
            };
            holder.allocate = [allocator](std::size_t size, std::size_t alignment) -> std::byte* {
                using Allocate = void* (*)(void*, std::size_t, std::size_t);
                const void* const* vtable = load<const void* const*>(allocator, 0);
                return static_cast<std::byte*>(
                    reinterpret_cast<Allocate>(const_cast<void*>(vtable[kAllocAllocateVSlot / 8]))(allocator, size, alignment));
            };
            return &holder;
        };
    }

    // Hook #6: gate = {**(c+8 -> +0x20), its +0x174e8, session mgr, session +0x58}. C (bytes 0x180028cd4..0x180028d0c).
    {
        const auto world_slot = at(wiring_sig::kSignInWorldGlobal);
        const auto session_slot = at(wiring_sig::kSessionManagerGlobal);
        if (world_slot || session_slot) {
            s.sign_in_gate = [world_slot, session_slot] {
                SignInGate g;
                if (const void* game = read_global(world_slot)) {
                    g.game_present = true;
                    g.game_field_174e8_set = load<std::uint64_t>(game, kSignInWorldField) != 0;
                }
                if (const void* sess = read_global(session_slot)) {
                    g.sign_in_present = true;
                    g.sign_in_state = load<std::int32_t>(sess, kSessionSignInState);
                }
                return g;
            };
        }
    }
    // `(c+0x20)[3](1)`: ctx[0x68]+0x18 = game function 0x140decc10 (table slot 0x18). C.
    if (const auto fn = at(wiring_sig::kSignInPrepareFn)) {
        s.sign_in_prepare = [fn](int arg) {
            using Prepare = std::uint64_t (*)(int);
            reinterpret_cast<Prepare>(static_cast<std::uintptr_t>(fn))(arg);
        };
    }
    // The registered method: nrsc's map value is `*(entry+8)`, called as `value(0)` (`call [node+0x30]`, ecx = 0). C.
    s.call_registered_method = [](void* value, int arg) {
        using Method = void (*)(std::uintptr_t);
        if (value) reinterpret_cast<Method>(value)(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(arg)));
    };

    // The party-size preference (FUN_180009c90): game event flag 0xa7f418 through the original EcTestEventFlagState
    // evaluate with expected state 1. C for the mechanism; the flag's meaning and who sets it are U. Needs the
    // original (captured by hook #26) and the class's vtable address (table slot 0x260); absent = off.
    if (const auto vtable = at(sig::kEcEventFlagState)) {
        s.extended_party_preference = [vtable]() -> bool {
            const auto original = state().orig.ec_event_flag_state;
            if (!original) return false;
            alignas(16) std::byte cond[kFakeCondSize]{};
            store<std::uint64_t>(cond, kCondVtable, vtable);
            store<std::uint8_t>(cond, kCondValue, 1);              // expected state byte at +0x28
            store<std::uint32_t>(cond, kCondExpect, kExtendedPartyEventFlag);  // flag id at +0x2c
            original(cond, nullptr, nullptr);  // a2/a3 are not used on this path (0x1405a0270 stores a3 only)
            return (load<std::uint8_t>(cond, kCondResult) & 1) != 0;
        };
    }

    // C (live, four captures incl. solo): ctx[0xe8]+0x10 byte == 1, which hook #26 answers for event flag 0x7f8.
    s.event_flag_7f8_state = true;
    // C (live): M+0x194 == 0 in every capture. Fold ids (hook #23's vector B, one int32 live), P0/P13 flags: see docs.
    s.use_fixed_player_ids = false;
}

void wire(const signatures::SignatureTable& table, const config::Settings& /*settings*/) {
    wire([&table](unsigned offset) { return table.at(offset); });
}

}  // namespace nrsc::hooks::world
