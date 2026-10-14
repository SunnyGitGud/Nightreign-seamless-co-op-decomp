#pragma once
// Wiring of the world group's protected-state members (docs/wiring_world.md). In the original the Hooks object `ctx` is
// filled by protected code; every field the world detours read is either a signature-table slot copied into ctx, a game
// global's address, or an nrsc object. wire() installs the equivalents into world::state().
//
// Call it after groups::world() (which builds the state's originals) and before the hooks are enabled. It never touches
// state().on_fatal.
#include <cstdint>
#include <functional>

#include "nrsc/config/settings.hpp"
#include "nrsc/hooks/world/world_state.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::hooks::world {

// Signature-table slots read by the wiring (docs/signatures.md section 2; the value of a "global" slot is the address of
// the game's global variable, the value of a "function" slot is the function's address).
namespace wiring_sig {
inline constexpr unsigned kSignInPrepareFn = 0x18;    // function 0x140decc10 (void f(int)); ctx[0x68]+0x18
inline constexpr unsigned kAllocatorForFn = 0xa8;     // function 0x14215cdb0 (allocator of a block); ctx[0xa8]+8
inline constexpr unsigned kFindChrFn = 0xc8;          // function 0x14070ca60 (mgr, &handle); ctx[0xb0]+8
inline constexpr unsigned kSignInWorldGlobal = 0x188; // global 0x143c0f0a8; ctx[0x70]+0x20 (object with +0x174e8)
inline constexpr unsigned kSessionManagerGlobal = 0x190;  // global 0x143c21ec8 (CSSessionManager); ctx[0x68]+0x10
inline constexpr unsigned kQuickMatchGlobal = 0x198;  // global 0x143c21fc8; ctx[0x60]+0x28
inline constexpr unsigned kGameDataManGlobal = 0x1b0; // global 0x143c078d0; ctx[0x78][0]
inline constexpr unsigned kChrManGlobal = 0x1d0;      // global 0x143c134f8; ctx[0xb0][0]
}  // namespace wiring_sig

// The party-size preference is the game event flag 0xa7f418, evaluated through EcTestEventFlagState's original
// evaluate with expected state 1 (FUN_180009c90 builds a fake EcTestEventFlagState condition on its stack).
inline constexpr std::uint32_t kExtendedPartyEventFlag = 0xa7f418;

// The u64 word at the start of nrsc's master state (`*ctx[0xe0]` = `**ctx[0x58]` = the mode word `**(ctx[0x90]+0x10)`):
// 0 while alone, 0x21 (bits 0 and 5) once a party exists, in every live capture (roundtable, matchmaking, expedition).
// Who sets and clears it is protected code (session layer, phase 3), so the wiring leaves it 0 and offers this setter.
inline constexpr std::uint32_t kPartySessionWord = 0x21;
void set_party_session_active(bool active);

// The same task objects hook #6 registers, from a pointer to one of them: is_registered = `*(obj+0x10) != 0`
// (0x180061050), do_register = `(*(obj+0x28))(obj, *(u32*)(obj+0x20))` (0x180061040). The objects are created by the
// session layer, which attaches them through state().auto_control_tasks[i].
AutoControlTask auto_control_task_from_object(void* object);

// Fills the accessors from a lookup of table offsets (tests pass a fake one). A 0 address leaves the member unset.
void wire(const std::function<std::uint64_t(unsigned table_offset)>& at);
void wire(const signatures::SignatureTable& table, const config::Settings& settings);

}  // namespace nrsc::hooks::world
