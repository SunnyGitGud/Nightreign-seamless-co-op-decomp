#pragma once
// State of the world group's detours (docs/hooks.md section 9). In the original every detour receives the Hooks
// object `ctx` as its first argument; here the detours are plain functions and reach this state through state().
//
// Fields that the original's protected code fills (ctx[0x58], [0x60], [0x68], [0x78], [0x90], [0xa0], [0xb0], [0xe0],
// [0xa8], [0x30]) are marked (U) and given a safe default: with the defaults every detour behaves like the vanilla
// game, or like the documented nrsc behaviour with an empty party.
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nrsc/cs/auto_control_api.hpp"
#include "nrsc/cs/bloodstain_man.hpp"



namespace nrsc::hooks::world {

// Bits of the byte at *ctx[0xe0] (bit meaning W, filled by protected code).
inline constexpr std::uint32_t kModeActiveBit = 1;  // P7 and P8 (docs/hooks.md section 9)

// Bits of the "mode word" W = **(ctx[0x90]+0x10) tested by P5 (same predicate as Set in param_repository.md 2.4).
inline constexpr std::uint32_t kModeWordBit0 = 1;
inline constexpr std::uint32_t kModeWordBit2 = 4;

// One of the four objects at ctx[0x30]+0x2f8..0x310 that hook #6 registers (vtable +0x28 = is_registered,
// +0x18 = register; task identity U, world_chr_man.md section 4).
struct AutoControlTask {
    std::function<bool()> is_registered;
    std::function<void()> do_register;
};

// What hook #6's gate reads (world_chr_man.md section 4): game = **(c+8 -> +0x20) (the WorldChrMan, W), its +0x174e8,
// the auto-control object s = *(c+0x20 -> [2]) and s+0x58.
struct SignInGate {
    bool game_present = false;
    bool game_field_174e8_set = false;
    bool sign_in_present = false;
    int sign_in_state = 0;
};

struct WorldState {
    // ------------------------------------------------------------------------------------------------------------
    // Originals. Filled through HookSpec::original (call target for CallSite, trampoline for Inline, old slot value
    // for VtableSlot). Null until the installer has run; detours that need one and find it null do nothing / return 0.
    struct Originals {
        void* (*p0_game_data_ctor)(void* mem) = nullptr;                                    // call target of 0x1401fa740
        std::uint32_t (*p5_battle_action)(void*, void*, std::int32_t, std::int32_t, std::uint8_t, std::uint8_t,
                                          std::uint8_t, std::uint64_t) = nullptr;           // 0x1405769d0 trampoline
        bool (*p8_session_contains)(void* mgr, void* key) = nullptr;                        // 0x140df0af0
        void (*p11_party_info_reset)(void* pmi) = nullptr;  // (U) ctx[0x80]+0x18, tail-called by P11; not set by any HookSpec
        void (*p14_chr_action)(void* obj) = nullptr;                                        // 0x1403df9b0
        std::uint64_t (*lua_register)(void*, void*, const char*, std::uint64_t) = nullptr;  // hook #1, 0x142152fe0
        std::uint64_t (*auto_control_register)(void*, void*, const char*, std::uint64_t) = nullptr;  // hook #17
        bool (*sign_in_trigger)(void*, void*) = nullptr;                                    // hook #6, 0x140df9420
        void (*lobby_update_state)(void* self) = nullptr;                                   // hook #20, 0x140b45530
        std::uint64_t (*ec_chr_dead)(void* cond, void* a2, void* a3) = nullptr;             // hook #24, 0x14059d0a0
        std::uint64_t (*ec_team_type)(void* cond, void* a2, void* a3) = nullptr;            // hook #25, 0x1405a9680
        std::uint64_t (*ec_event_flag_state)(void* cond, void* a2, void* a3) = nullptr;     // hook #26, 0x1405a0270
    } orig;

    // ------------------------------------------------------------------------------------------------------------
    // Accessors for objects owned by the game or filled by protected code (all U, default = absent).
    // CSSessionManager singleton (**(ctx[0x68]+0x10)); null = no session.
    std::function<void*()> session_manager;
    // The 0x458-byte global at 0x143c078d0 (**ctx[0x78]); null = absent.
    std::function<void*()> game_data_man;
    // Object whose +0x30/+0x38 is the lobby member vector (0xf0-byte records) (ctx[0x60]->[0x28]->[0]); null = absent.
    std::function<const void*()> member_vector_owner;
    // The preference read by FUN_180009c90 = game event flag 0xa7f418 evaluated through EcTestEventFlagState (C, see
    // wiring.cpp / docs/wiring_world.md); what the flag means and who sets it is U. Unset or flag off: the party size seen by
    // event scripts stays capped at 3.
    std::function<bool()> extended_party_preference;
    // ChrMan access for P6: manager() yields **ctx[0xb0]; find(mgr, &handle) is ctx[0xb0]+8 (game 0x14070ca60 on
    // the global 0x143c134f8, C from group1.md section 11).
    std::function<void*()> chr_manager;
    std::function<void*(void* manager, std::uint32_t* handle)> find_chr_by_handle;
    // hooks+0xa8 -> [1] for P12 (bloodstain_man). Empty = null allocator, which is fatal as in the original.
    cs::AllocatorFor bloodstain_allocator_for;
    // Hook #6 (auto_control_api): the four task objects, the gate, `(c+0x20)[3](1)` and the call of a registered
    // method. All U; with the defaults the detour only forwards to the original.
    std::array<AutoControlTask, 4> auto_control_tasks;
    std::function<SignInGate()> sign_in_gate;
    std::function<void(int)> sign_in_prepare;                     // (c+0x20)[3](1)
    std::function<void(void* entry, int arg)> call_registered_method;  // calls an entry captured by hook #17
    // Receives a FatalError raised by the detour (the DLL's fatal box). Empty = the exception propagates.
    std::function<void(const std::string& message, const char* file, unsigned line)> on_fatal;

    // ------------------------------------------------------------------------------------------------------------
    // Detour state (named members of the original's ctx structures).
    // `*ctx[0xe0]`: mode flags (kModeActiveBit). Default 0 = vanilla behaviour in P7 and P8. Live: the same u64 as mode_word and
    // quick_match_flags; 0 alone, 0x21 in a party (wiring.hpp set_party_session_active).
    std::atomic<std::uint32_t> mod_flags{0};
    // Mode word W of P5. Default 0.
    std::atomic<std::uint32_t> mode_word{0};
    // byte at ctx[0x68][1]+0x194 read by P14: when set the repeated ids are {1..6} instead of the session players.
    bool use_fixed_player_ids = false;
    // P5's lock (ctx[0x90] sub-struct) and the "current target" published while the original runs.
    std::recursive_mutex p5_lock;
    std::atomic<std::int32_t> current_target{-1};    // g_18017c360 and ctx[0x90]+0x38 (readers: sibling hooks, U)
    std::atomic<std::int32_t> secondary_target{-1};  // g_18017c358 and ctx[0x90]+0x2c
    // P13's flag array (ctx[0x78]+0x38..0x3c, 5 bytes), reset by P0, set by the unreconstructed sibling 0x180027370.
    std::array<std::uint8_t, 5> player_flags{};
    // P10 stores the vtable of PartyMemberInfo (RTTI result, table 0x208) in P[1] and the new object in P[2].
    std::uintptr_t party_member_info_vtable = 0;
    void* party_member_info = nullptr;
    // Hook #17: name -> entry (`*(a2+8)`) of every method the game registers on CSAutoControlAPI (ctx[0x40]).
    std::unordered_map<std::string, void*> registered_methods;
    // Hooks #22: bit 6 mirrors the lobby flag (**ctx[0x58]); consumer U.
    std::uint64_t quick_match_flags = 0;
    // Hooks #23-#26 (ctx[0xa0]): script ids whose player number is folded mod 3 (E+0x30, filler U), ids seen by
    // EcTestTeamType with mode 2 (E+0x18), and the nrsc state answering event flag 0x7f8 (**E byte +0x10, U).
    std::vector<std::int32_t> fold_player_number_script_ids;
    std::vector<std::int32_t> team_type_ids_seen;
    bool event_flag_7f8_state = false;
};

WorldState& state();        // process-wide instance
void reset_state();         // replaces it with a default-constructed one (tests; DLL init)

}  // namespace nrsc::hooks::world
