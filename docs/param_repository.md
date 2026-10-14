# nrsc.dll param_repository

Source path: `nrsc\mod\param_repository\param_repository.cpp` (`.rdata 0x18014c674`, referenced from
`0x180035787` and `0x180036e65`). The code lives in `.text 0x180034fa0..0x18003795c`.
Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. What the module does

It builds the Nightreign **multiplayer scaling** rows. For each configured row of param `0x4f`, it takes two
param-`0xf` rows (a base row and a "full" row), uses their difference as a per-player increment, and generates five
copies of the base row for 2–6 players. The `[SCALING]` preferences multiply the increment. Hooks then serve
the generated rows in place of the game's own and apply the one that matches the current player count.

| Part | Function | Status |
|---|---|---|
| Param row lookup (`SoloParamRepository`) | `FUN_180035630` | **C**, `params/param_file.*` |
| Scaling builder (`YuiNexusRegulationManager`) | `FUN_1800364b0` | **C**, `params/regulation_manager.*` |
| Once-only setup after the regulation is loaded | `FUN_1800295e0` (hook detour) | **C** logic; the hook target is **U** |
| Override lookup | `FUN_1800357b0` | **C** |
| Param-0xf row hook: override first, then the original | `FUN_1800297c0` | **C** (logic described; the game-side tail-call is not reconstructed) |
| Effect choice by player count | `FUN_180029140` | **C** as a decision; the game calls are **U** |
| Fatal handler `FUN_180040470(name, file, line)` | readable: `"A fatal error has occurred and the process has been aborted:\n\n%s\n%s\n%u"` | **C** → `FatalError` |

Evidence: `ev_76b0077905c56f76…` (FUN_180035630, FUN_1800364b0), `ev_c22899fb677fbbfa…` (FUN_1800295e0),
`ev_864f112ff70f9fd5…` (FUN_1800357b0 and neighbours), `ev_f527d0c17587716e…` (FUN_180029140, FUN_1800297c0),
`ev_043b928e808a0ee5…` (FUN_180040470). Xrefs to the path string: `ev_fd27103f9c82d1b9…`.

## 2. Row lookup — `FUN_180035630` (C)

- `(this, out{id, row}, param_index, id)`. out = `{0xffffffff, null}`; a negative `id` returns at once.
- Repository = `**(this + 0x170)`. If it is null → fatal `("SoloParamRepository", param_repository.cpp, 6)`.
- Table entry `param_index * 0x48` from `+0x80`: `+0x80` must be non-null; file = `*(*(entry+0x88) + 0x80) + 0x80`.
- Pre-header: `file-0xc` = index count (must be ≥ 1), `file-0x10` = size; the index sits at `file + align16(size)`,
  with `{u32 id, i32 slot}` entries sorted for a binary search (`(lo+hi)/2`, unsigned compare). Slot `-1` → none.
- Jump table `0x18003579c` on `file[0x2d] & 0x7f` (checked against the disassembly):

| Format | 64-bit row offset read from |
|---|---|
| 2 | `slot*4 + 0x34` |
| 3 | `slot*4 + 0x44` |
| 4 | `file[0x2e] & 2` ? `slot*0x18 + 0x48` : `slot*4 + 0x44` |
| 5 | bit 7 of `file[0x2d]` set → as 4, otherwise as 3 |

  Row = `file + offset`. Format 4 with flag 2 is the 0x18-byte row table of current FromSoftware param files.
  The `slot*4` paths read 8 bytes with a 4-byte stride. That does not match a 0xC-byte row-table layout, so
  those paths are either never used or index something else (**U**). They are reproduced exactly as they are.

## 3. Scaling builder — `FUN_1800364b0(this, set)` (C)

Manager fields: `+0x08` → preferences (`+0x30/+0x34/+0x38`, see `baseline.md` §2), `+0x10` ready, `+0x11`
built, `+0x14` next row id, `+0x18` `vector<i32>` target ids, `+0x30/+0x70/+0xb0` `unordered_map<i32,float>` deltas,
`+0xf0` and `+0x130` the two override sets `unordered_map<i32 param, unordered_map<u32 id, void*>>` (FNV-1a,
MSVC layout 0x40 bytes; node max-size `0x2e8ba2e8ba2e8ba` matches 0x58-byte nodes).

1. Set 0 must hold a param-`0xf` map, otherwise fatal `("YuiNexusRegulationManager", …, 0xb5)`.
2. For each target id `t` in `+0x18`: `S = row(0x4f, t)`, `A = row(0xf, S[+4])`, `B = row(0xf, S[+8])`. If any is
   missing, skip `t`.
3. `dH = B[0x10]-A[0x10]` (exactly 0 → **0.15**, `.rdata 0x180145dc0`, `cmpeqss` + mask), `dD = B[0x38]-A[0x38]`,
   `dP = B[0x25c]-A[0x25c]`; stored with `operator[]` (overwrite) in the three delta maps.
4. For `n = 1..5`: copy A (0x410 bytes) into a new allocation; id = `next++`; write
   - health `((h*dH)*n)+1` at `0x10, 0x108..0x114, 0x2b4, 0x340, 0x344`
   - damage `((d*dD)*n)+1` at `0x38..0x44, 0x1e0`
   - posture `max(0.1, ((p*dP)*n)+1)` at `0x25c` (`maxss` with 0.1 first, so a NaN result is kept)

   then emplace it into set 0 / param `0xf`. The evaluation order was checked in the disassembly at `0x180036803..`.
5. Record (0x18 bytes) `{u8 kind = S[0x10], i32 ids[5]}` is emplaced into a local map by `t` (the first one wins), and
   at the end the local map is emplaced into `set`/param `0x4f` (`FUN_180035a50` returns the existing node without
   assigning: checked at `0x180035b0e`).

`FUN_1800295e0` (detour): it calls the original (`hooks+0x88 → [1]`). It then sets `+0x10` once, and
once only (`+0x11`) it emplaces an empty param-`0xf` map in set 0 and calls `build(0)` and then `build(1)`. Both passes
put their rows into **set 0**; only the `0x4f` records go to different sets. Ten ids are used per target.

## 4. Consumers (C)

- `FUN_1800297c0` (param-0xf getter hook): `find_override(0, 0xf, id)`. On a hit it returns
  `{row, id, 2}`; otherwise it tail-calls the original (`hooks[2]`).
- `FUN_180029140`: player count = `(u32)((end-begin) >> 4) * 0xeeeeeeef` (exact `/0xf0`; Ghidra's pseudocode
  shows it as `* -0x11111111`). Set = `(**(hooks+0x18) & 5) == 1`. The lookup is `find_override(set, 0x4f, *param_3)`.
  - count < 2 or no hit → nothing.
  - record null, count > 6, or `ids[count-2] == -1` → `hooks+0x58(target+400, 0x11a)`, `target+0x238 = 0`.
  - otherwise `hooks+0x60(target, ids[count-2], target, 1)`, `+0x68(target, 0)`, `+0x70(**(target+0x1b8), 1)`;
    if `kind == 2`, `target+0x238 = count`.

## 5. Open (U)

| Item | Note |
|---|---|
| Who fills `+0x18` (target ids) and sets the initial `+0x14` | no readable writer found in this module; probably the protected init |
| Names of params `0xf` / `0x4f` and of the row fields | `0xf` is very likely SpEffectParam (0x410-byte rows with rate floats, W); `0x4f` unknown |
| Meaning of tag `2` in `{id, 2, row}` and of argument `0x11a` | – |
| Hook targets of `FUN_1800295e0`, `FUN_1800297c0`, `FUN_180029140` | in `hooks.cpp` / `signatures.cpp` |
| The `slot*4` param-format paths | see §2 |

## 6. Reconstruction

`src/nrsc/params/param_file.*` and `src/nrsc/params/regulation_manager.*`; tests are in `tests/params/` (23). The game
side is injected: rows come from a `RowSource`, and the effect choice returns an `EffectDecision`. The original
leaks the generated rows and records; the reconstruction owns them, so the pointers stay valid for the manager's
lifetime.
