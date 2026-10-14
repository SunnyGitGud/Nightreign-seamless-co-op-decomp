# Wiring the world group's protected state (worker "world", 2026-10-15)

Code: `src/nrsc/hooks/world/wiring.{hpp,cpp}` (`world::wire(table, settings)`, also `wire(at_fn)` for tests), tests
`tests/hooks/world_wiring_test.cpp`. The DLL init must call `hooks::world::wire(table, settings)` after `groups::world()`.
`state().on_fatal` is not touched. Grades C/S/W/U as in the brief. Nothing here depends on `devirt/out/v4`.

## Key finding: the Hooks object layout (live, C)

The probes show `ctx[...]` are pointers to nrsc sub-structs that are flat tables of **signature-table slot values and nrsc
data**, allocated back to back on the heap (`ctx[0x68]` at M-0x40, M = `*ctx[0xe0]`, `ctx[0xa0]` = M+0x1b8, `ctx[0x58]` =
M+0x238). Each ctx field is therefore either a game global's address (the detour dereferences it), a game function address, or M.

* `M` (`*ctx[0xe0]`, 3d5ed60 in the party captures; contains the "BABLU" SSO strings, the 0.7/1.0 floats, etc.) starts with one
  **u64 word W**. The same word is `**ctx[0x58]` (quick-match flags, hook #22 bit 6), `*ctx[0xe0]` (P7/P8 bit 0) and
  `**(ctx[0x90]+0x10)` (P5 mode word). Values: **0 in the solo captures** (title screen 012623, in-game 012952), **0x21 in all three
  party captures** (roundtable, matchmaking, expedition). So it is dynamic: bits 0 and 5 are set by protected session code when a
  party exists. M+0x190 is 0 solo and 2 with the friend (member count, W). M+0x194 and M+0x195 are 0 everywhere.
* `ctx[0xa0]` = E: `E[0]` = `ctx[0xe8]`; `E+0x10` = **0x142c240f8 = vtable of EcTestEventFlagState** (nightreign.exe, whose slot +8
  is 0x1405a0270 = hook #26's target); `E+0x18` vector A (id log), `E+0x30` vector B (one int32 in every capture), `E+0x50..0x68`
  originals of #24/#25/#26/#27.

## Members

| Member | Source in the original | Wiring | Grade |
|---|---|---|---|
| `extended_party_preference` | `FUN_180009c90(E+0x10)` is called with the **vtable address** of EcTestEventFlagState (E+0x10 = 0x142c240f8). It builds a fake condition on its stack (`+0` that vtable, `byte +0x28 = 1`, `dword +0x2c = 0xa7f418`) and calls `vtable[1]` = `EcTestEventFlagState::Evaluate` (0x1405a0270: `cond+0x18.bit0 = (cond.byte0x28 == EventFlagState(cond+0x2c))`), returning that bit. So the "preference" is **the state of game event flag 0xa7f418** (read through the flag manager `0x1405e7260`; the third argument is only stored). Nothing in readable nrsc writes 0xa7f418 (only xref: 0x1800090c2). Default in the original = whatever the flag is, normally 0 (cap 3). | Builds the same fake condition and calls `state().orig.ec_event_flag_state` (captured by hook #26, so it bypasses our detour). Unset or original missing: false. Table slot 0x260 (vtable). | mechanism C (disasm of both functions + live E+0x10), flag meaning U |
| `mod_flags`, `mode_word`, `quick_match_flags` | One u64 at M (above). | Defaults stay 0 (= solo, matches live). `set_party_session_active(bool)` sets/clears bits 0 and 5 (0x21) in all three; for the session layer to call when a party forms/dissolves. The WorldState keeps three fields; they are equivalent because only bit 0, bit 2 and bit 6 are read and writers touch disjoint bits. | value C (live), who flips it U (protected session code, phase 3). Note with 0x21, P5 (W&1 set, W&4 clear) never calls its original, as in the original. |
| `use_fixed_player_ids` | byte M+0x194 (`ctx[0x68][1]+0x194`) | false (0 in all 6 captures) | C |
| `fold_player_number_script_ids` (hook #23 vector B, `E+0x30`) | One int32 element live (begin 889f8c0, end 889f8c4); the heap page was never dumped, so the value is unknown. Filled by the startup VM `0x180040630`, not by the installer | left empty (no folding; irrelevant for 2 players) | count C, value U |
| `team_type_ids_seen` (vector A) | live: empty, then {0x4a0} during the expedition | filled by detour #25 itself | C |
| `event_flag_7f8_state` (#26) | `**E` byte +0x10 = `ctx[0xe8]+0x10` (R, which also holds the boss-list vectors for #12). Value **1** in solo and party captures | `true` | value C, meaning of flag 0x7f8 U |
| `session_manager` | `**(ctx[0x68]+0x10)`; `ctx[0x68]+0x10` = 0x143c21ec8 live = table slot **0x190** | `*(void**)slot` each call | C |
| `game_data_man` | `**ctx[0x78]`; `ctx[0x78][0]` = 0x143c078d0 = slot **0x1b0** | same | C |
| `member_vector_owner` | `ctx[0x60]->[0x28]->[0]`; `ctx[0x60]+0x28` = 0x143c21fc8 = slot **0x198**; the pointed object has `+0x30/+0x38` = vector of 2 x 0xf0 (probe `ctx_60_28_0`) | same | C |
| `chr_manager`, `find_chr_by_handle` | `ctx[0xb0]` = {0x143c134f8 (slot **0x1d0**), 0x14070ca60 (slot **0xc8**)}; the detour does `mgr = **ctx[0xb0]`, `call [ctx[0xb0]+8](mgr, &handle)` | `*(void**)slot`; calls the function | C |
| `bloodstain_allocator_for` | `ctx[0xa8]` = {0x143c2d9c0, 0x14215cdb0 (slot **0xa8**), ...}; P12 calls `ctx[0xa8][1](block)` -> allocator, then vtable +0x68(block), vtable +0x50(0x78, 0x10) | adapter around the game function and the two vtable slots (thread_local holder) | C |
| `player_flags` (P0/P13) | `ctx[0x78]+0x38..` all 0 in every capture; set by the unreconstructed sibling 0x180027370 | default 0 | C (value), writer U |
| `sign_in_gate` | `game = **(c+8 -> +0x20)`: `c+8` = ctx[0x70], +0x20 = 0x143c0f0a8 (slot **0x188**), then `game+0x174e8`; `s` = CSSessionManager (slot 0x190), `s+0x58` (disasm 0x180028cd4..0x180028d0c) | reads both globals each call | C |
| `sign_in_prepare` | `(c+0x20)[3](1)` = `ctx[0x68]+0x18` = 0x140decc10 (slot **0x18**) | calls it | C |
| `call_registered_method` | the method map is nrsc's own `ctx[0x40]` (filled by hook #17); the value (`*(entry+8)`) is called as `value(0)` (`call [node+0x30]`, `xor ecx,ecx`) | `reinterpret_cast<void(*)(uintptr_t)>(value)(arg)` | C |
| `auto_control_tasks[4]` | `c+0x2f8..0x310` (3d5fa98..) -> nrsc task objects with vtables 0x180147b00/b40/b80/bc0 whose slots are **shared and readable**: `+0x18` = 0x180061040 (`jmp [this+0x28]` with edx = `[this+0x20]`, i.e. the game's task-register 0x140febc80), `+0x28` = 0x180061050 (`this+0x10 != 0`); the run slots are the session layer (0x180063110, 0x180061140 VM, 0x180061060, 0x180061000) | **left unset**: the objects belong to the session layer (phase 3). `auto_control_task_from_object(ptr)` builds the two callbacks from such an object for whoever creates them | layout C, objects U |

Table-slot constants are in `wiring.hpp` (`wiring_sig`). Values in the table are VAs; the global slots hold the address of the game
global, so the wiring dereferences them lazily (never at wire time).

## Still unresolved

* What sets/clears W (0x21) and why (bit 5, M+0x190); the reconstructed session layer has to call `set_party_session_active`.
* The one int32 in hook #23's vector B (needs a dump of heap 0x889f8c0 or the startup VM 0x180040630).
* Meaning of event flag 0xa7f418 and who sets it (the original reads it; it may be set by game data or by the VM startup code), and of flag 0x7f8.
* Hook #14 reads `M+0x195` (0 live) – not a world-member, left to its current default.
* The four auto-control task objects (session layer).
* If another run shows W nonzero while solo or `R+0x10 == 0`, revisit the defaults.
