# Wiring the params group (worker "params", 2026-10-15)

Entry point: `nrsc::hooks::params::wire(table, settings, dll_dir)` (`src/nrsc/hooks/params/params_group.hpp`). Call it after `groups::params()`
(which resets `ParamsState`). It never throws; unresolved items keep the vanilla default. Tests: `tests/hooks/params_test.cpp`
(`ParamsWire*`, plus `ParamsWireRealBinary` with `NRSC_GAME_EXE`; all slot addresses below were checked against v1.03.3).
Grades C/S/W/U. Live evidence = `evidence/runtime/20261015-020739/020849/020955*/snapshots/*-hooks-ctx/probes` (same values in all three phases).

## Object map recovered from the probes and asm (C)

| ctx field | live object | meaning |
|---|---|---|
| ctx[0x70] = H | 0x3d5eb60 | the "hooks" struct of #7: +0x10 regulation manager, +0x18 -> core object (mode word), +0x58..0x70 four game helpers |
| ctx[0x68]+0x10 | 0x143c21ec8 | game global (sig slot 0x190) holding the CSSessionManager*; #7 reads `*global` then +0x78/+0x80 |
| regulation manager (0x3d5f3e0) | +0x08 preferences, +0x10/+0x11 ready/built = 1/1, +0x14 next id, +0x18 target vector, +0x170 -> 0x143c28880 | |
| manager +0x170 | 0x143c28880 | game global (sig slot 0x1c0) holding the SoloParamRepository* (`**(this+0x170)` in FUN_180035630) |
| core object = `*(H+0x18)` = `*(ctx[0x90]+0x10)` | 0x3d5ed60 | first dword = mode word, +0x194 = shared-loot byte; the same object serves #7 and P3 |
| ctx[0x50] | 0x3d5fab8 | text module: +0 core, +8 message repository (0x3d5fb10), +0x18 trampoline, +0x20 handler map |

## Members

| Member | Source | Grade |
|---|---|---|
| `effects.remove_speffect / post_apply / refresh` | sig slots 0x70, 0x80, 0x88 = game fns 0x140504c40, 0x1403e0ab0, 0x140428bf0. Live H+0x58/+0x68/+0x70 equal them. | C |
| `effects.apply_speffect` | slot 0x78 = 0x1403c80a0 (live H+0x60). The detour calls it with 12 arguments (asm 0x1800291fd..0x18002928a): chr, id, chr, 1, ptr to 4 floats of 1.0 (.rdata 0x180145da0), ptr to a 0x40+ byte block (first 8 floats 1.0), five zero bytes, 0x10. `wire` installs a thunk that supplies them. Meaning of the blocks is U, values are copied from the original's frame. | C (call shape), W (blocks' meaning) |
| remove argument | asm 0x1800292b3 is `mov rcx,[rsi+0x190]`: the **pointer stored at chr+0x190**, not `chr+0x190`. The old detour passed the address. Fixed (and its test). | C |
| `member_vector_bytes` | `*(*(u64*)table.at(0x190) + 0x80) - *(...+0x78)`; 0 while the singleton is null (asm 0x180029165..0x18002918e) | C |
| `alternate_set` / mode word | `(W & 5) == 1`, W = first dword of the core object. Live W = 0x21 in roundtable, matchmaking and expedition. `wire` writes 0x21 into the shared settings block; `alternate_set` and P3 read that block. Bit meanings U, so the constant is the observed value, not derived. | S (value), U (semantics) |
| P3 `+0x194` byte | 0 live (default settings; +0x190 = 2 next to it). Kept 0. What sets it (a hidden option?) is U. | S |
| `row_source` | `find_row_in_repository(*(byte**)table.at(0x1c0), param, id).row`; null repo gives no rows (no fatal) | C |
| first generated row id | live manager +0x14 = 0x7fff0320 with an 80-entry target vector (begin 0x165ff710, end 0x165ff850); 10 ids per target gives start **0x7fff0000** (was 0x7f000000) | S |
| `regulation.target_ids` | 80 ids filled by protected startup code: **7700..7779** (read from the regulation manager in the real-image emulation, devirt.md 2.11). `wire` sets `target_source = scaling_target_ids()`, run once at #10 before the first build; #7/#10 are no longer held back. | C (emulation), in-game untested |
| preferences | live manager +0x08 object: +4 = 1, floats 1.0 x3 at +0x30..+0x38 with default ini: matches `Settings.preferences` | C |
| `messages` | `<dll dir>/locale/english.json` loaded with `load_locale`; null (vanilla) when missing or broken. Language choice and English-first order are U (localisation.md). Live: the repository exists, its locale pointer is set, and both its maps (fixed texts, keys) hold 2 categories. The (category, id) tables sit in an undumped heap region, so the key table stays empty and **nothing is overridden yet**. | S (location), U (table) |
| `text_handlers` | one entry: category 200 -> the override FUN_18001e1c0 (devirt.md 2.11; the 4 handlers of out/v4 were the network message map). The override's special value is the session's member count. | C (emulation) |

## Unresolved and risks

1. `target_ids` (above). With `scaling_wired` true and the guessed list the scaling works for every 0x4f row; the list's true size/content is unknown. #7 is a JmpEntry hook with no trampoline, so it replaces the game function even for ids not in the list (as the original does). If `ParamsState::scaling_wired` is false after `wire`, the DLL init should skip the "hook#7" spec (the spec list is built before `wire`, so this is the init's decision).
2. (category, id) -> key table and the fixed-text placement (banner/welcome/separate-save).
3. Mode word bit meanings; source of the +0x194 byte.
4. Language selection for the locale file.
5. `ParamsState::scaling_wired` and `list_row_ids` read live game memory; they are only exercised on fake memory here (Linux tests), never in the game.
