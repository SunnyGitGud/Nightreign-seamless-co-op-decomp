# Wiring: NETWORK block table and NetState (worker "net", 2026-10-15)

Grades: **C** confirmed (bytes or live memory), **S** strongly inferred, **W** weak, **U** unknown.
Sources: nightreign.exe v1.03.3 on disk, signature table, live 2-player probes `evidence/runtime/20261015-0207*/0208*/0209*`
(`snapshots/*-hooks-ctx/probes`, plus `game_image.bin`, a dump of the live game image). Nothing from `devirt/out/v4` is used.

## 1. NETWORK memory block

### What `*(ctx+0xb8)` is (C)
The probe `ctx_b8_3d5ec20.bin` starts with two pointers: `0x1439d9b50` and `0x1439d9c40`. Both are `.data` of nightreign.exe:

| Address | Content |
|---|---|
| `0x1439d9b50` | 30 x pointer to a UTF-16 name (`MAIN`, `GFX`, `SFX`, ... entry 17 = `NETWORK` at `0x142c1b2a0` ... `MO_WWISE_MO_G`). |
| `0x1439d9c40` (= names + 0xf0) | row 0 of a `[config][30]` matrix of u64 sizes (rows of 0xf0 bytes). |

So the "table" is not a heap object: it is two game globals (the engine heap-component configuration). The nrsc.dll value
`table = *(ctx+0xb8)` is a small nrsc object `{names, sizes}` holding these two addresses; the signature slots hold the same values
(ctx[0x90]+0x130/+0x138 = slots `0x1f0` / `0x1f8`).

Game code that uses them (C, disassembly): `0x140f65450` looks a name up in the names array (wide string compare, loop ends when the pointer reaches the
sizes array) and returns `sizes[cfg * 30 + index]`, `cfg` = dword `0x14442e024`, set by `0x140f654e0` (platform config name table `0x1439d9ad0`, <15 entries).
The caller is the engine heap creator `0x140f5fb60..0x140f60fd8` (5240 bytes; reached through a function pointer at `.rdata 0x142cc7820`): per block it fetches the size
by name (`call 0x140f65450`, for NETWORK at `0x140f609ee`), `VirtualAlloc`s it and builds an allocator.

Sizes: on disk `sizes[0][17]` = `0xf00000` (**15 MiB**); in the live process (original nrsc.dll loaded) it is `0x1e00000` (**30 MiB**), C from
`game_image.bin` (all three phases). Rows 1..14 are still `0xf00000`: only row 0 is patched, because the active config on PC is 0 (`[0x14442e024] = 0` live, C).
The reconstruction (`set_memory_block_size`, `sizes[i] = 0x1e00000`, `VirtualProtect(sizes, 0x1d)`) therefore matches what the original did.

### Locating it (C)
Signature slot `0x1f0` (pattern `4C 8D 15 ?? ?? ?? ?? 48 8B D9`, mode 2, **unique**) resolves exactly to `0x1439d9b50`. It was already in
`evidence/signatures/signatures.json` / `docs/signatures.md` (marked U); no new pattern is needed. `sizes = names + 30 * 8` (C: `lea rdi,[0x1439d9c40]` is the loop end
in `0x140f65467`). Slot `0x1f8` is the sizes address in the original's ctx but is not a lookup in the table (the ctx fill derives it).

Implemented: `src/nrsc/dantelion/block_table_locator.{hpp,cpp}`:
- `find_block_table(image, table)` returns `const MemoryBlockTable*` (static storage, addresses cast from the VAs) or `nullptr`.
- `locate_block_table` / `validate_block_table` are the testable parts: the names array must hold 30 pointers into the image, entry 0 must be `MAIN`, and `NETWORK` must be present.
- Tests: `tests/dantelion/block_table_locator_test.cpp` (synthetic image + real binary under `NRSC_GAME_EXE`).
DLL init (not mine) replaces `resize_network_block(nullptr)` by `resize_network_block(dantelion::find_block_table(image, table))`.
Note: the `ModuleImage` of the live DLL is a copy of the module, but the returned pointers are the live addresses, which is what `VirtualProtect` and the store need.

### When the original resizes it (S)
- The consumer (`0x140f5fb60`, heap creation) reads the size when the engine heaps are created, very early in game start-up, before any game logic.
- The original performs the resize in the tail of the hook installer `FUN_18001edb0`, on the thread started by `modengine_ext_init`; that export
  **blocks until the ready flag** (set after the installer) and ME3 loads nrsc.dll early (`me3_load_early = 1`), so it runs before the game's own start-up.
- Live proof of the ordering: NETWORK is 30 MiB in the dump, and the block was allocated with the size read at heap creation (S; if the write came later it would only change the table, and the heap would be 15 MiB).
- Consequence for the reconstruction: the resize must happen inside the init thread, before `modengine_ext_init` returns, with the table located from the **on-disk-state** image
  (the names array is static `.data`, so it is valid immediately). The Arxan-protected `.text` is irrelevant: slot `0x1f0` is found in the first, plain `.text`.
  Do the resize **before** installing hooks (as the original does it last in the installer; either order is before the heap creation).

## 2. NetState (`wire()`)
`void nrsc::hooks::net::wire(const signatures::SignatureTable&, const config::Settings&)` in `src/nrsc/hooks/net/net_group.{hpp,cpp}`;
call it after `groups::net()` (which resets the state) and before the hooks go live. `wire_from_slots(slot_fn, state)` is the testable core.

How the ctx works (C from the probes): the sub-structs `ctx[0x60]`, `[0x68]`, `[0x78]`, `[0x90]`, `[0x98]` are consecutive nrsc heap structs
whose fields are copies of signature-table slots, in the order of the slots (e.g. `ctx[0x98]+0x18` = slot `0x28`, `+0x28` = slot `0x218`).
`ctx[0xe0]` and `ctx[0xe8]` are nrsc-owned state objects (not game memory), following ctx[0x68] in the same heap block.

| Member | Source | Grade | Wired value |
|---|---|---|---|
| `game_data_slot` | `ctx[0x78]+0x00` = slot `0x1b0` = `0x143c078d0`; live `***ctx[0x78]` chain reaches the object (`ctx_78_0_0` probe) | C | `reinterpret_cast<void* const*>(slot 0x1b0)` |
| `force_flag` | `ctx[0xe0]+0x195`, a byte of nrsc's own state object; **0 in all three phases** | C (source), W (meaning) | pointer to byte 0x195 of `NetState::state_blob` (zero) |
| `mode_word` | `*ctx[0x90]+0x10` = `ctx[0xe0]` object, dword at +0: **0x21 in all three phases** | C | pointer to dword 0 of `state_blob` (0 = vanilla; see open points) |
| `jobs.game_data` / `rate.game_data` | `ctx[0x98]+0x10` and `ctx[0xe8]+0x08` both equal `ctx[0xe0]` (live) | C | `state_blob` (the object whose +0x159/+0x15a flags the job callbacks set) |
| `jobs.alloc_object` | `ctx[0x98]+0x18` = slot `0x28` = `0x14080b1d0` = `mov rax,[0x143c2da78]; ret`: a **getter** returning an allocator (nrsc: `a = f(); a->vt[0x50](a, size, align)`) | C | thunk `getter()->vtbl[0x50](a, size, align)` |
| `jobs.alloc_data` | `ctx[0x98]+0x20` = slot `0x30` = `0x141fc3480` = `mov rax,[global]; ret`, then `a->vt[1](a, size)` | C | thunk `getter()->vtbl[1](a, size)` |
| `jobs.decrease_vtbl` | `ctx[0x98]+0x28` = slot `0x218`, RTTI `CS::CSChaosMatchRateDecreaseJob` (`0x142c717d8`) | C | slot value |
| `jobs.update_vtbl` | `ctx[0x98]+0x30` = slot `0x220`, RTTI `CS::CSChaosMatchRateUpdateJob` (`0x142c71858`) | C | slot value |
| `jobs.wrap_callback` | `ctx[0x98]+0x48` = slot `0xf0` = `0x14053e2b0`, called `(rcx=&scratch, rdx=callback)` | S (role from the call shape) | slot value |
| `jobs.attach_callback` | `ctx[0x98]+0x50` = slot `0xf8` = `0x14080e3f0` (an Arxan `jmp` thunk), called `(rcx=job, rdx=wrapped)` | S | slot value |
| `team_send` (P2) | `ctx[0x60]+0x30` = slot `0x98` = `0x140dfe250`, call `(team, rec, type=0x94, data, len)` | C | slot value |
| `player_send` (P9) | `ctx[0x68]+0x20` = slot `0xa0` = `0x140df2270` | C | slot value |
| `unicast_id_a/b` | `ctx[0x90]+0x28/+0x2c`; **-1 in all three phases** | C | stay -1 |
| `boss_list` (#12) | `ctx[0xe8]` nrsc object: byte vector `+0x18..+0x20` = **8 bytes**, u32 `+0x48` = **5**, identical in all phases; the 8 bytes live in nrsc's heap, which no snapshot reached | U (contents) | stays `nullopt` (vanilla list) |
| `rate.current_rate / delta / selector` | `ctx[0xe8]+0x50/+0x54/+0x58` (same object as the boss list) | C (position), U (writer) | stay as written by #13/#16 |

The `ChaosRateState` and `BossListSource` of the header are two views of the same nrsc object `ctx[0xe8]`; `ctx[0xe0]` (the `state_blob`) also holds the world group's
`*ctx[0xe0]` mode flags (live 0x21), so the world group's `mod_flags` / `mode_word` and net's `mode_word` are one word in the original. Here they are separate
atomics/blob words: the session layer must keep them equal (see below).

Fallbacks stay safe: every detour still requires `attached()`; with no session `wire()` changes nothing observable, and a slot that is 0 leaves its member null so the detour calls the game's original.

### What a session layer (phase 3) must provide
1. `NetState::attach(MessageBus&, PeerList&)`; per-peer receive queues via `enqueue_received(peer, channel, payload)` (channels 5, 6 for session types 1/0x18; #19).
2. The mode word: write `0x21` to `state_blob[0..4]` (and the same value into world `mod_flags` / `mode_word`) when the mod's session is active. 0x21 was seen at roundtable, matchmaking and expedition; the meaning of the bits (bit 0 "active", bit 5 ?) is W. `(0x21 & 5) == 1`, so with it P4 forwards MapItem requests to ready peers.
3. `unicast_id_a/b` writers (member ids, `-1` = broadcast; always -1 in the captures) and `current_request` (P4 sets it).
4. The boss list (`boss_list`: 8 id bytes + forced u32, observed 5) if the boss-select override is wanted; contents unknown.
5. Whoever consumes the job-done bytes `state_blob[0x159]` (rate update) and `[0x15a]` (rate decrease): in the original they are read by nrsc's session code, not by the game.
6. The force flag `state_blob[0x195]` (0 in every capture) and the rate state `rate.current_rate` / `selector` semantics if used.
7. `server::MessageBus`/`PeerList` with `send(steam_id, bytes, channel)` and the peer flag bits (`kPeerActive`, `kPeerHost`, mask 0x21 for P4).

### Still unresolved
- Contents of the boss list (the 8 bytes) and the meaning of u32 5; meaning of mode-word bits and of `+0x195`; who writes the unicast ids.
- Exact roles of `0x14053e2b0` / `0x14080e3f0` (wrap/attach) are inferred from call shapes (S); `0x14080e3f0` lies in the Arxan-protected area, so the live address is a thunk.
- `ChaosRateState.current_rate` (+0x50) is zero in the captures; its writer is in protected code.
