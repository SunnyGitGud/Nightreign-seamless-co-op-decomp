# nrsc.dll signature subsystem (`nrsc\signatures\signatures.cpp`)

Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown. Static analysis only: nrsc.dll was
never run natively (the builder stack writes were replayed in Unicorn; the game exe was only read from disk).
Machine-readable data: `evidence/signatures/signatures.json` (keyed by `table_offsets` and `lookup_call`, joins with
`evidence/hooks/hooks.json`). Resolver: `tools/resolve_signatures.py` (standalone, read-only, run with `python3 -I`; about 3 minutes, pure Python regex).

## 1. Result summary

| Item | Count |
|---|---|
| Byte-pattern lookups in the table builder | 153 (120 `FUN_18007c8f0` calls, 26 inlined copies of it, 7 find-all) |
| RTTI (type-descriptor name to vtable) lookups | 15 |
| Inline patterns in the hook installer (`0x18001edb0`, EB patch set) | 2 |
| Unique match in `nightreign.exe` v1.03.3 | 145 byte patterns + 15 RTTI |
| Ambiguous (2 matches; first match is used, so the lookup still succeeds) | 1 (`0x180065aba`, table `+0x110`) |
| find-all, within expected slot count | 7 (counts 2, 2, 2, 3, 4, 2, 2) |
| find-all over its slot count (fatal) | 0. `0x180073610` was first misread as 2 slots; it has 3 (`+0x438/+0x440/+0x448`), corrected 2026-10-15 |
| Missing (0 matches) | **0** |
| Installer EB-patch sites | 267 `B9 .. E8 .. F3 0F 11 05` hits, 267 `72 ?? 48 8D` sites |

## 2. Subsystem layout

| Finding | Grade | Evidence |
|---|---|---|
| Source path string `nrsc\signatures\signatures.cpp` at `.rdata 0x18014c6d9`. Used as the file argument of the fatal helper `FUN_180040470(msg, file, line)` | C | xrefs `0x1800698f9`.. (inline failure paths), `0x18007147c`.. (`incompatible game version`), `0x18007cb26/0x18007cb42/0x18007cfc8` |
| Table builder `FUN_1800655a0` (`0x1800655a0..0x180069af5`); at entry it calls `FUN_18006b830` (`0x18006b830..0x18006ee6f`), later tail-jumps (`0x180069638`) to `FUN_18006f570` (`0x18006f570..0x18007b05a`). Execution order = `exec_seq` in the JSON | C | `callers`, asm `0x1800655c9`, `0x180069638` |
| Nobody calls the builder directly (entered from protected code) | S | no `e8/e9` xref, pdata only |
| Pattern text is not stored: each pattern is built as XOR-obfuscated 8-byte stack constants (key block XORed with `xorps`), then parsed by `FUN_18002bf30` | C | asm `0x18006f5a3`..`0x18006f777`; all 168 strings decoded by replaying the builder in Unicorn with the three lookup functions and the parser hooked |
| Pattern grammar is Pattern16 (Dasaav): `XX`, `?`/`??`, nibble wildcards `X?`, `&MM` mask, `#N` repeat; compare `(mem & mask) == val` | C | decompile of `FUN_18002bf30` |
| Scan `FUN_18002c340(pattern, rarest_idx, out, start, size)`: first match, forward from `start`, window `[start, start+size)`. Rarest-byte start uses byte-frequency table `.rdata 0x18014ad50` | C | decompile |
| `FUN_18007c8f0(table, pattern, offset, mode)`: first match + offset, then mode 0 as is, mode 1 `A+5+rel32(A+1)`, mode 2 `A+7+rel32(A+3)`; success increments the counter at `table+0x10` | C | decompile |
| `FUN_18007cb60(base, size, out_vec, pattern, offset)`: all (overlapping, advance +1) matches + offset. Callers store at most N results into table slots and are fatal above N | C | decompile + asm `0x18007140d`.. |
| `FUN_18007d380(range, name_fn)`: walks every 8-byte slot of the range; a slot is accepted when it points to a COL (`signature == 1`, `pSelf` RVA == own RVA) whose TypeDescriptor name begins `.?` and `strcmp(name, wanted) == 0` (callback `FUN_18007d520`); stores `slot + 8` | C | decompile + asm |
| Table layout: `table[0]` = scan base, `table[1]` = scan size, `table[2]` = counter (`+0x10`), then `u64` result slots from `+0x18` to `+0x5d0`. The hook context holds it at `ctx+0x28` | S | hooks.json `installer_note`, `local_d0[0],[1]` in the installer |
| Scanned module is the main game module image (full range, executable and data); the base/size writer is in protected code | W | RTTI walk needs `.rdata`, so the range is not `.text` only; 159 of 160 unique results fall in the first `.text`, `.data` or `.rdata` of `nightreign.exe` |

| Scan details (from the C++ integration, 2026-10-15): trailing all-wildcard bytes are dropped from the compared length, but the window must still hold the full pattern; candidate positions are `[start, start+size-N]`. `FUN_18007c8f0` scans *before* checking the mode, so an invalid mode with no match reports "Failed to find signature" first. find-all and RTTI lookups do not advance the counter | C | asm `FUN_18002bf30` / `FUN_18002c340` / `FUN_18007c8f0`; `src/nrsc/signatures/` |
| `exec_seq` in the JSON covers find1/inline only (1..146); the order of the 22 find-all/RTTI entries comes from builder address order | S | `tools/gen_signature_table.py` |

### Failure behaviour

| Case | Behaviour | Line | Grade |
|---|---|---|---|
| find1 / inline pattern has no match | fatal `Failed to find signature: %u`, `%u` = number of earlier successful lookups (= `exec_seq - 1`) | 1000 (0x3e8) | C |
| invalid mode | fatal `Not a valid signature type` | 993 (0x3e1) | C |
| find-all returns more than its slot count | fatal `incompatible game version: inconsistent pattern result` | 488, 521, 641, 695, 803, 840 | C |
| find-all, the 4-slot entry (`0x180073d5f`) overflows | jump-table default is unreachable code, no explicit fatal | n/a | S |
| find-all, zero matches | silent, slots keep 0 | n/a | C |
| find-all hit address 0 | fatal `Signature not found: %s` | 1016 (0x3f8) | C (unreachable) |
| RTTI name not found | silent, slot keeps 0 | n/a | C |

## 3. Resolution against `nightreign.exe` v1.03.3

| Property | Value | Grade |
|---|---|---|
| File | 118,835,320 bytes, image base `0x140000000`, SizeOfImage `0x7c5d600`, PE timestamp 1776344106 (2026-04-16), entry RVA `0x26916bc` | C |
| Plain sections | first `.text` (RVA `0x1000`, 45 MB, entropy ~6.5, normal code), `.interpr` (`0x2bb5000`), `.rdata`, `.data`, `.pdata` | C |
| Opaque section | second `.text` (RVA `0x4a94000`, 52 MB): entropy 7.3 to 8.0 per 256 KB block, mostly encrypted or compressed (Arxan-like), with plain pieces (entropy 6.6 near its tail). One signature (`0x180067d87`, `+0x1b8`) matches only here, so at least part of it is plain code | S |
| Result | 0 missing, 1 ambiguous, 0 over-count (see section 1); resolved RVAs in the JSON | C |

**Does v1.03.3 look like the target build?** Yes (S, with C for the user report). 160 lookups (145 byte patterns, 15 RTTI) resolve exactly once,
1 twice (the first is used), all 7 find-all fit their slots, and nothing is missing. The user reports (2026-10-15) that nrsc 1.1.3 ran and got
in game on this install under Windows.

*Correction (2026-10-15, main session):* `0x180073610` (`41 83 BE D0 00 00 00 00 74`, offset 3) has 3 matches (`0x140aecf63`, `0x140aed0f3`,
`0x140aeea2f`, the third in `0x140aee790`, which contains `CS::PartyMemberInfo::Update_PartyMemberS*`). An earlier version of this doc
said the code allows 2 and would abort. The store loop (`0x180073637..0x1800736b6`) writes hit 0 to `+0x438`, hit 1 to `+0x440` and hit 2 to
`+0x448`, and calls the fatal (line 0x2b7 = 695) only for a 4th hit. So 3 matches is exactly the expected count (C).

## 4. Naming

The 15 RTTI entries are named by exact RTTI string (C). Byte patterns give no names by themselves: the names below come from the strings and
vtables referenced in the containing function (`context` in the JSON) and from `hooks.json` behaviour summaries. Only 3 reach
S, 42 are W, 108 are U (kept unnamed rather than guessed). Game singleton globals (mode 2, `+0x188..+0x1f8`, `+0x270`) are
the least understood.

## 5. Full table

`off` = table offset (join key with `hooks.json` `table_off`). `mode`: 0 plain, 1 call target, 2 RIP-relative. `n` = matches.
RVA = resolved RVA (image base `0x140000000`).


| off | lookup call | kind | pattern / RTTI name | offs | mode | n | resolved RVA | name (grade) |
|---|---|---|---|---|---|---|---|---|
| 0x18 | `0x18006b99f` | find1 | `E8 ?? ?? ?? ?? C6 83 BD 01 00 00` | 0 | 1 | 1 | 0xdecc10 |  (U) |
| 0x20 | `0x18006b9e3` | rtti | `.?AV?$CSEzUpdateTask@VCSEzTask@CS@@VGameMan@2@@CS@@` |  |  | 1 | 0x2c32590 | vtable of .?AV?$CSEzUpdateTask@VCSEzTask@CS@@VGameMan@2@@CS@@ (C) |
| 0x28 | `0x18006c18e` | find1 | `E8 ?? ?? ?? ?? 48 89 44 24 38 4C 8B C0 8D 53 08 B9 40` | 0 | 1 | 1 | 0x80b1d0 |  (U) |
| 0x30 | `0x18006c2e3` | find1 | `4C 0F 42 FE 4D 85 F6 74 ?? E8 ?? ?? ?? ?? 4C 8B 00 ` | 9 | 1 | 1 | 0x1fc3480 |  (U) |
| 0x38 | `0x18006bb76` | find1 | `E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84 ?? ?? ?? ?? 8B 54 24 20` | 0 | 1 | 1 | 0x518280 |  (U) |
| 0x40 | `0x18006bce3` | find1 | `E8 ?? ?? ?? ?? 48 8B 83 B8 01 00 00 48 8B 88 90 01 00` | 0 | 1 | 1 | 0x460c40 |  (U) |
| 0x48 | `0x18006bea6` | find1 | `E8 ?? ?? ?? ?? 48 8B 83 B8 01 00 00 48 8B 88 68 01 00 00 48 85 C9 74 ?? E8` | 24 | 1 | 1 | 0x47f9b0 |  (U) |
| 0x50 | `0x18006c023` | find1 | `E8 ?? ?? ?? ?? 48 8B 83 B8 01 00 00 48 8B 88 68 01 00 00 48 85 C9 74 ?? E8` | 0 | 1 | 1 | 0x457880 |  (U) |
| 0x58 | `0x18006c496` | find1 | `48 8B D6 E8 ?? ?? ?? ?? 8B 8B D0 00 00 00 E8 ?? ?? ?? ?? 48 81 C3` | 3 | 1 | 1 | 0x51b2b0 |  (U) |
| 0x60 | `0x18006c5e8` | find1 | `00 48 8D 54 24 30 48 8B CB E8 ?? ?? ?? ?? 48 8B 0D` | 9 | 1 | 1 | 0x20bde0 |  (U) |
| 0x68 | `0x18006c791` | find1 | `48 8D 15 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? B2 01 8B 8B D0 00 00 00 E8` | 10 | 1 | 1 | 0x20dcb0 |  (U) |
| 0x70 | `0x18006c854` | find1 | `BA 1A 01 00 00 E8` | 5 | 1 | 1 | 0x504c40 |  (U) |
| 0x78 | `0x18006c953` | find1 | `E8 ?? ?? ?? ?? 0F 57 C9 48 8B CF E8` | 0 | 1 | 1 | 0x3c80a0 |  (U) |
| 0x80 | `0x18006ca70` | find1 | `0F 57 C9 48 8B CF E8 ?? ?? ?? ?? 48 8B 8F B8` | 6 | 1 | 1 | 0x3e0ab0 |  (U) |
| 0x88 | `0x18006cb6b` | find1 | `48 8B 09 E8 ?? ?? ?? ?? 40 80 FD 02` | 3 | 1 | 1 | 0x428bf0 |  (U) |
| 0x90 | `0x18006cc74` | find1 | `E8 ?? ?? ?? ?? B0 01 E9 ?? ?? ?? ?? 83 F8 02` | 0 | 1 | 1 | 0xaef640 | callee in MsgRepository code (call target) (W) |
| 0x98 | `0x18006cd7d` | find1 | `E8 ?? ?? ?? ?? C6 83 ?? 00 00 00 01 40 FE C6` | 0 | 1 | 1 | 0xdfe250 |  (U) |
| 0xa0 | `0x18006ced2` | find1 | `C7 44 24 20 10 00 00 00 41 B8 68 00 00 00 48 8B D3 E8` | 17 | 1 | 1 | 0xdf2270 |  (U) |
| 0xa8 | `0x18006d01d` | find1 | `48 83 C0 18 49 3B D0 7C ?? E9 ?? ?? ?? ?? 48 8B 40 F0` | 9 | 1 | 1 | 0x215cdb0 |  (U) |
| 0xb0 | `0x18006d129` | find1 | `B2 07 E8 ?? ?? ?? ?? 48 8B 47 ?? 48 85 C0` | 2 | 1 | 1 | 0xdfd940 |  (U) |
| 0xb8 | `0x18006d348` | inline | `48 89 5C 24 08 48 89 6C 24 10 56 57 41 56 48 83 EC 50 48 8B F2 48 8B D9` | 0 | 0 | 1 | 0x86a7a0 |  (U) |
| 0xc0 | `0x18006d46b` | find1 | `44 0F B6 C6 48 8B CF E8 ?? ?? ?? ?? B0 01` | 7 | 1 | 1 | 0x577630 |  (U) |
| 0xc8 | `0x18006d60a` | find1 | `E8 ?? ?? ?? ?? 48 85 C0 74 ?? 80 B8 90 01 00 00 00 75 ?? 48 89 43 08` | 0 | 1 | 1 | 0x70ca60 |  (U) |
| 0xd0 | `0x18006d758` | find1 | `48 89 AC 24 C0 00 00 00 E8 ?? ?? ?? ?? 48 8B E8 48 85 C0` | 8 | 1 | 1 | 0x5f8ad0 |  (U) |
| 0xd8 | `0x18006d8f7` | find1 | `E8 ?? ?? ?? ?? 48 8B C8 48 8D 54 24 40 E8 ?? ?? ?? ?? 48 8B 57 08` | 0 | 1 | 1 | 0x5f8b00 |  (U) |
| 0xe0 | `0x18006d9bd` | find1 | `48 8B 89 78 08 00 00 E8` | 7 | 1 | 1 | 0x8b26c0 |  (U) |
| 0xe8 | `0x18006db05` | find1 | `E8 ?? ?? ?? ?? 90 48 8D 05 ?? ?? ?? ?? 49 89 06 0F 10 03` | 0 | 1 | 1 | 0xb19a10 | callee related to CS::ServerRequestJob / CS::LoginJob (call target) (S) |
| 0xf0 | `0x18006dc01` | find1 | `E8 ?? ?? ?? ?? 48 8B D8 49 8B 56 08` | 0 | 1 | 1 | 0x53e2b0 |  (U) |
| 0xf8 | `0x18006dd0a` | find1 | `48 8B D0 48 8B CB E8 ?? ?? ?? ?? 83 E7 FD 89 7C` | 6 | 1 | 1 | 0x80e3f0 | callee in CS::MenuJobSequence::IfElseJob family (call target) (W) |
| 0x100 | `0x1800657a3` | inline | `48 89 5C 24 08 57 48 83 EC 30 B9 01 00 00 00` | 0 | 0 | 1 | 0x2691548 |  (U) |
| 0x108 | `0x180065989` | inline | `48 89 5C 24 08 57 48 83 EC 20 48 8B D9 0F B6 FA 8B CF` | 0 | 0 | 1 | 0xf271d0 | hook target (hooks.json #2): detour is a shared stub `mov al,1; ret`: patched function alw (W) |
| 0x110 | `0x180065aba` | find1 | `80 BB D0 00 00 00 00 0F 84 ?? ?? ?? ?? E8` | 13 | 0 | 2 | 0xde743b | hook target (hooks.json #3): call site replaced by `return true` stub. (W) |
| 0x118 | `0x180065b8a` | find1 | `41 B8 90 01 00 00 48 8B D9 E8` | 9 | 1 | 1 | 0x27f4d20 | hook target (hooks.json #4): looks up arg4 in a std::map<uint,std::function> at ctx[0x50]+ (W) |
| 0x120 | `0x180065d4c` | find1 | `68 90 41 B8 04 00 00 00 48 8D 15 ?? ?? ?? ?? 48 8D 8B D8 00 00 00 E8` | 22 | 0 | 1 | 0x25a1762 | SaveLoad2 save-path/file-name construction site (function references ".sl2" and the SaveLo (S) |
| 0x128 | `0x180065e5f` | find1 | `E8 ?? ?? ?? ?? 44 0F B6 F8 45 32 F6` | 0 | 1 | 1 | 0xdf9420 | hook target (hooks.json #6): auto_control_api SignIn auto-call (see world_chr_man.md secti (W) |
| 0x130 | `0x180065fc5` | find1 | `48 83 7C 24 28 00 74 ?? 48 8D 54 24 20 48 8B CB E8` | 16 | 1 | 1 | 0x3db250 | hook target (hooks.json #7): param_repository: sets the multiplayer scaling effect by play (W) |
| 0x138 | `0x180066145` | inline | `E8 ?? ?? ?? ?? 80 7B 0C 00 0F 84 ?? ?? ?? ?? 48` | 0 | 0 | 1 | 0x7fb19b | hook target (hooks.json #8): calls original then, if a condition holds, iterates listeners (W) |
| 0x140 | `0x18006621b` | find1 | `44 8B C0 49 8B C9 E8` | 6 | 0 | 1 | 0x94afb4 | hook target (hooks.json #9): same detour as hook 8 at a second site; no original saved. (W) |
| 0x148 | `0x18006638c` | find1 | `48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 4C 8B C0 48 8B D7 E8` | 18 | 0 | 1 | 0x1abe89 | hook target (hooks.json #10): param_repository once-only setup after regulation load (para (W) |
| 0x150 | `0x1800664db` | find1 | `E8 ?? ?? ?? ?? 48 8B 44 24 40 48 85 C0 74 ?? 66 44` | 0 | 1 | 1 | 0xea17e0 | param row getter (FD4ParamHeaderAccessor call target) (S) |
| 0x158 | `0x180066671` | find1 | `E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 48 E8 ?? ?? ?? ?? 90 48 8B 5C 24 50` | 13 | 1 | 1 | 0x9e5b10 | callee used by CS::MatchingBossSelectDialog::_CreateBoss* (call target) (W) |
| 0x160 | `0x18006678e` | find1 | `44 0F B6 CE 4C 8B C3 48 8D 55 D7 48 8D 4D 77 E8` | 15 | 0 | 1 | 0x1fd6bb | hook target (hooks.json #13): stores *(param_4+4) atomically into ctx[0x30]->[0x70]+0x58 t (W) |
| 0x168 | `0x18006682c` | find1 | `C6 81 30 01 00 00 01 E8` | 7 | 0 | 1 | 0x698469 | hook target (hooks.json #14): returns true when ctx[0xe0]->+0x195 == 0, else (not decompil (W) |
| 0x170 | `0x180066983` | find1 | `48 8B 0D ?? ?? ?? ?? 48 8D 54 24 60 E8 ?? ?? ?? ?? 90 48 8D` | 12 | 0 | 1 | 0x9ee060 | hook target (hooks.json #15): allocates and initialises a 0x78-byte ref-counted object (al (W) |
| 0x178 | `0x180066a99` | find1 | `4D 8B 87 A8 00 00 00 48 8D 55 B8 48 8B CB E8` | 14 | 0 | 1 | 0xb5361e | site inside CS::CSQuickMatchingCtrl::RequestUpdateCh* (quick-match control) (W) |
| 0x180 | `0x180068d3c` | find1 | `80 B8 89 00 00 00 06 74` | 7 | 0 | 1 | 0xb5725d | site inside CS::CSQuickMatchingCtrl::STEP_HostMultiG* (2-byte NOP patch) (W) |
| 0x188 | `0x18006776b` | find1 | `48 8B 05 ?? ?? ?? ?? 0F 28 F2` | 0 | 2 | 1 | 0x3c0f0a8 |  (U) |
| 0x190 | `0x1800678a6` | find1 | `48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 83 C1 48 B2 01` | 0 | 2 | 1 | 0x3c21ec8 |  (U) |
| 0x198 | `0x180067965` | find1 | `C6 45 64 00 48 8B 05` | 4 | 2 | 1 | 0x3c21fc8 |  (U) |
| 0x1a0 | `0x180067ab2` | find1 | `48 8B 0D ?? ?? ?? ?? 49 8B D4 E8 ?? ?? ?? ?? 48 8B 8D` | 0 | 2 | 1 | 0x3c21f48 |  (U) |
| 0x1a8 | `0x180067bb8` | find1 | `48 8B 0D ?? ?? ?? ?? 48 8B 49 68 48 8D 57 18` | 0 | 2 | 1 | 0x3c046a8 |  (U) |
| 0x1b0 | `0x180067c6e` | find1 | `48 8B 0D ?? ?? ?? ?? 48 69 C0` | 0 | 2 | 1 | 0x3c078d0 |  (U) |
| 0x1b8 | `0x180067d87` | find1 | `48 89 79 28 48 8B 05 ?? ?? ?? ?? 48 85 C0` | 4 | 2 | 1 | 0x3c149d8 |  (U) |
| 0x1c0 | `0x180067e97` | find1 | `48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 5E` | 0 | 2 | 1 | 0x3c28880 |  (U) |
| 0x1c8 | `0x180067fea` | find1 | `48 8B 05 ?? ?? ?? ?? 80 B8 ?? ?? 00 00 0D 0F 94 C0` | 0 | 2 | 1 | 0x3c13258 |  (U) |
| 0x1d0 | `0x1800681cf` | find1 | `48 8B 0D ?? ?? ?? ?? 48 8D 53 40 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 74 ?? 80 B8 90` | 0 | 2 | 1 | 0x3c134f8 |  (U) |
| 0x1d8 | `0x1800682f2` | find1 | `BA 24 00 00 00 48 8B CE E8 ?? ?? ?? ?? 48 8B 0D` | 13 | 2 | 1 | 0x3c10b68 | global read in CSInGamePad code (singleton pointer) (W) |
| 0x1e0 | `0x180068402` | find1 | `C6 41 0D 00 74 ?? 48 8B 0D ?? ?? ?? ?? 48` | 6 | 2 | 1 | 0x3c13158 |  (U) |
| 0x1e8 | `0x1800684ee` | find1 | `BA 13 00 00 00 E8 ?? ?? ?? ?? 4C 8B 05` | 10 | 2 | 1 | 0x3c2d9c0 |  (U) |
| 0x1f0 | `0x180068e93` | find1 | `4C 8D 15 ?? ?? ?? ?? 48 8B D9` | 0 | 2 | 1 | 0x39d9b50 |  (U) |
| 0x1f8 | `0x180068fa2` | find1 | `48 6B C8 1E 49 63 C3 48 03 C8 48 8D 05` | 10 | 2 | 1 | 0x39d9c40 |  (U) |
| 0x200 | `0x180069113` | rtti | `.?AV?$DLRuntimeClassImpl@VCSAutoControlAPI@CS@@$0A@@DLRF@@` |  |  | 1 | 0x2cba100 | vtable of .?AV?$DLRuntimeClassImpl@VCSAutoControlAPI@CS@@$0A@@DLRF@@ (C) |
| 0x208 | `0x18006915a` | rtti | `.?AVPartyMemberInfo@CS@@` |  |  | 1 | 0x2c70cb0 | vtable of CS::PartyMemberInfo (C) |
| 0x210 | `0x1800691a1` | rtti | `.?AVCSQuickMatchLobbyContext@CS@@` |  |  | 1 | 0x2c75d20 | vtable of CS::CSQuickMatchLobbyContext (C) |
| 0x218 | `0x1800691e8` | rtti | `.?AVCSChaosMatchRateDecreaseJob@CS@@` |  |  | 1 | 0x2c717d8 | vtable of CS::CSChaosMatchRateDecreaseJob (C) |
| 0x220 | `0x18006922f` | rtti | `.?AVCSChaosMatchRateUpdateJob@CS@@` |  |  | 1 | 0x2c71858 | vtable of CS::CSChaosMatchRateUpdateJob (C) |
| 0x228 | `0x180069276` | rtti | `.?AV?$DLRuntimeClassImpl@VCSLuaEventScriptImitation@CS@@$0A@@DLRF@@` |  |  | 1 | 0x2c27318 | vtable of .?AV?$DLRuntimeClassImpl@VCSLuaEventScriptImitation@CS@@$0A@@DLRF@@ (C) |
| 0x230 | `0x1800692bd` | rtti | `.?AVChrFinderImp@?5??_UpdateMultiDoping@WorldChrManImp@CS@@AEAAXXZ@` |  |  | 1 | 0x2c1ba68 | vtable of .?AVChrFinderImp@?5??_UpdateMultiDoping@WorldChrManImp@CS@@AEAAXXZ@ (C) |
| 0x238 | `0x180069304` | rtti | `.?AVPlayerDebugSession@CS@@` |  |  | 1 | 0x2ca49c8 | vtable of CS::PlayerDebugSession (C) |
| 0x240 | `0x18006934b` | rtti | `.?AVPlayerNetworkSession@CS@@` |  |  | 1 | 0x2ca4a10 | vtable of CS::PlayerNetworkSession (C) |
| 0x248 | `0x180069392` | rtti | `.?AVEcTestPlayerNo@CS@@` |  |  | 1 | 0x2c249f0 | vtable of CS::EcTestPlayerNo (C) |
| 0x250 | `0x1800693d9` | rtti | `.?AVEcTestChrDead@CS@@` |  |  | 1 | 0x2c23c00 | vtable of CS::EcTestChrDead (C) |
| 0x258 | `0x180069420` | rtti | `.?AVEcTestTeamType@CS@@` |  |  | 1 | 0x2c24d38 | vtable of CS::EcTestTeamType (C) |
| 0x260 | `0x180069467` | rtti | `.?AVEcTestEventFlagState@CS@@` |  |  | 1 | 0x2c240f8 | vtable of CS::EcTestEventFlagState (C) |
| 0x268 | `0x1800694ae` | rtti | `.?AVEcTestSortieMemberNum@CS@@` |  |  | 1 | 0x2c24c20 | vtable of CS::EcTestSortieMemberNum (C) |
| 0x270 | `0x180069581` | find1 | `28 48 89 5C 24 68 48 8D 05` | 6 | 2 | 1 | 0x2c74930 | data in .rdata reached from ServerRequestJob code (likely a vtable or table of CS::ServerR (W) |
| 0x278 | `0x18006f792` | find1 | `B9 58 04 00 00 E8 ?? ?? ?? ?? 48 89 44 24 38 48 85 C0 74 ?? 48 8B C8 E8` | 23 | 0 | 1 | 0x1fd5fc |  (U) |
| 0x280 | `0x18006f90b` | find1 | `8D 4A 60 E8 ?? ?? ?? ?? 48 89 45 CF 48 85 C0 74 ?? 48 8B C8 E8` | 20 | 0 | 1 | 0xc2b294 | site inside CS::InGameStep::_Common_Initialize (W) |
| 0x288 | `0x18006fb34` | find1 | `48 8B 0D ?? ?? ?? ?? 48 8D 54 24 ?? E8 ?? ?? ?? ?? 84 C0 0f 84 ?? ?? ?? ?? 48 8d 4C 24 70 E8` | 12 | 0 | 1 | 0xdf8a7e |  (U) |
| 0x290 | `0x18006fbf8` | find1 | `44 0F B6 C3 0F B6 D3 E8` | 7 | 1 | 1 | 0xdfe4f0 |  (U) |
| 0x298 | `0x18006fdb2` | inline | `48 89 5C 24 08 57 48 83 EC 20 F6 81 AA` | 0 | 0 | 1 | 0x572c10 |  (U) |
| 0x2a0 | `0x18006ff2e` | find1 | `0F B6 85 28 02 00 00 88 44 24 28 48 89 4C 24 20 49 8B CF E8` | 19 | 1 | 1 | 0x581b20 |  (U) |
| 0x2a8 | `0x180070047` | find1 | `C6 44 24 20 01 45 33 C9 44 8B C7 48 8B D6 E8` | 14 | 1 | 1 | 0x5769d0 | site inside CS::CSBattleGroup::CheckIsAllDead (call target at mode 1) (W) |
| 0x2b0 | `0x1800701e6` | inline | `E8 ?? ?? ?? ?? 4C 39 64 24 50 75 ?? 4C` | 0 | 0 | 1 | 0x581ba5 |  (U) |
| 0x2b8 | `0x180070310` | find1 | `48 8B CA E8 ?? ?? ?? ?? 84 C0 75 ?? 44 38` | 3 | 0 | 1 | 0x576a59 |  (U) |
| 0x2c0 | `0x180070482` | inline | `E8 ?? ?? ?? ?? 44 0F B6 F0 48 8B 15` | 0 | 0 | 1 | 0x37f401 | site inside CS::CSBattleGroup::CheckIsAllDead (W) |
| 0x2c8 | `0x18007059a` | find1 | `44 8D 4F 03 4C 8B C3 BA 2C 00 00 00 E8` | 12 | 0 | 1 | 0x57fb35 |  (U) |
| 0x2d0 | `0x180070652` | find1 | `48 8D 8E 40 9A 00 00 E8` | 7 | 1 | 1 | 0x4ae430 |  (U) |
| 0x2d8 | `0x180070750` | find1 | `4C 8D B6 F0 9A 00 00 49 8B CE E8` | 10 | 1 | 1 | 0x4ae340 |  (U) |
| 0x2e0 | `0x18007093f` | find1 | `E8 ?? ?? ?? ?? EB 03 49 8B C6 48 89 86 ?? ?? 00 00 4C 8B 05 ?? ?? ?? ?? BA 04  00 00 00` | 0 | 1 | 1 | 0xaec960 | callee related to CS::PartyMemberInfo (call target), called from a GameMan function (W) |
| 0x2e8 | `0x180070b4e` | inline | `48 89 5C 24 08 57 48 83 EC 20 8B 41 28 48 8B D9 B9 FF FF FF FF 33 ` | 0 | 0 | 1 | 0xaee1e0 |  (U) |
| 0x2f0 | `0x180070c23` | find1 | `F6 40 58 01 75 ?? 8B CB E8` | 8 | 1 | 1 | 0x1fecb0 |  (U) |
| 0x2f8 | `0x180070d29` | find1 | `E8 ?? ?? ?? ?? 89 B7 D0 00 00 00` | 0 | 1 | 1 | 0x200aa0 |  (U) |
| 0x300 | `0x180070e51` | inline | `48 83 EC 28 83 F9 01 77` | 0 | 0 | 1 | 0x1fdd80 |  (U) |
| 0x308 | `0x180070f6d` | find1 | `E8 ?? ?? ?? ?? 48 81 C3 F0 00 00 00 48 81 C6` | 0 | 1 | 1 | 0x200a50 | site inside CS::CSEmkActionForEvent::ExecuteEmkAction* (WorldMapPointParam user) (W) |
| 0x310 | `0x180071072` | find1 | `8B CA E8 ?? ?? ?? ?? 40 32 FF 48 8B D8` | 2 | 1 | 1 | 0x1fe7b0 |  (U) |
| 0x318 | `0x18007117b` | find1 | `4C 8B F8 E8 ?? ?? ?? ?? 44 8B F0 33 FF 85 C0` | 3 | 0 | 1 | 0xc3bff4 |  (U) |
| 0x320 | `0x1800712e3` | inline | `E8 ?? ?? ?? ?? 45 8B EE 48 63 D8` | 0 | 0 | 1 | 0x7c5815 | site inside CSCam/CSPersCam code (W) |
| 0x328,0x330 | `0x18007140d` | findall | `FF C3 E8 ?? ?? ?? ?? 3B D8 7C ?? 48 8B 0D` | 2 |  | 2 | 0xdfe919,0xdfeb31 |  (U) |
| 0x338 | `0x180071601` | inline | `E8 ?? ?? ?? ?? 33 DB 8B F8 85 C0 7E ?? 8B CB` | 0 | 0 | 1 | 0xb5f1b9 |  (U) |
| 0x340 | `0x180071724` | find1 | `8B 10 E8 ?? ?? ?? ?? 48 8B 87 88 00 00 00` | 2 | 0 | 1 | 0x90ec02 |  (U) |
| 0x348,0x350 | `0x180071842` | findall | `40 B7 01 41 8B CE E8 ?? ?? ?? ?? 48 85 C0` | 6 |  | 2 | 0x576aa6,0x581c5a |  (U) |
| 0x358 | `0x18007198f` | find1 | `F6 81 F9 01 00 00 40 75 ?? E8` | 9 | 0 | 1 | 0x3cd37b |  (U) |
| 0x360 | `0x180071a52` | find1 | `EB ?? 48 83 C1 58 E8` | 6 | 0 | 1 | 0x72af64 |  (U) |
| 0x368 | `0x180071b6c` | find1 | `48 83 C1 08 49 3B C8 75 ?? 48 8D 4B 58 E8` | 13 | 0 | 1 | 0x729730 |  (U) |
| 0x370 | `0x180071c7c` | find1 | `48 83 C1 58 E8 ?? ?? ?? ?? F2 0F 10 05` | 4 | 0 | 1 | 0x72b201 |  (U) |
| 0x378 | `0x180071d6b` | find1 | `48 83 C1 58 E8 ?? ?? ?? ?? F2 0F 10 05` | 4 | 0 | 1 | 0x72b201 |  (U) |
| 0x380 | `0x180071e7e` | find1 | `90 48 8D 4B 58 48 83 C4 20 5B E9` | 10 | 0 | 1 | 0x728a70 |  (U) |
| 0x388 | `0x180071fd4` | find1 | `0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 74 ?? F2 0F` | 6 | 0 | 1 | 0x72ae17 |  (U) |
| 0x390 | `0x1800720e4` | find1 | `48 8D 4B 58 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? C7` | 4 | 0 | 1 | 0x72ae38 |  (U) |
| 0x398 | `0x1800721ed` | find1 | `48 8D 4F 58 0F B6 D8 48 8D 54 24 60 E8` | 12 | 0 | 1 | 0x729f2c |  (U) |
| 0x3a0 | `0x1800722e8` | find1 | `48 8B 49 68 E8 ?? ?? ?? ?? 3B C7 0F 94 C0` | 4 | 0 | 1 | 0x5a8c18 |  (U) |
| 0x3a8 | `0x1800723b2` | find1 | `C7 87 70 01 00 00 03 00 00 00` | 6 | 0 | 1 | 0xdee240 | site inside a CSSessionManagerImp method (W) |
| 0x3b0 | `0x1800724da` | inline | `BA 50 11 00 00 41 B8 10` | 0 | 0 | 1 | 0x1faaba | site inside a debug-text function ("Net Player No:%d") (W) |
| 0x3b8 | `0x180072598` | find1 | `B8 02 00 00 00 4D 85` | 1 | 0 | 1 | 0x1faacc | site inside a debug-text function ("Net Player No:%d") (W) |
| 0x3c0 | `0x180072646` | find1 | `45 8D 7D 02 48 8B 4E 10` | 3 | 0 | 1 | 0x1fab30 | site inside a debug-text function ("Net Player No:%d") (W) |
| 0x3c8 | `0x1800727b4` | inline | `83 F8 01 77 ?? 48 63 D0 4C 69 C2` | 0 | 0 | 1 | 0x1fe560 |  (U) |
| 0x3d0 | `0x1800728ec` | inline | `83 F9 01 77 ?? 48 63 C1` | 0 | 0 | 1 | 0x1fe530 |  (U) |
| 0x3d8 | `0x180072a3f` | inline | `83 F8 02 73 ?? 8B C0 48 69 C0` | 0 | 0 | 1 | 0x1fe93c |  (U) |
| 0x3e0 | `0x180072ca0` | inline | `83 F8 02 73 ?? 8B C0 48 69 C8 A0 08 00 00 48 8B 05 ?? ?? ?? ?? 48 03 48 10 48 85` | 0 | 0 | 1 | 0x1fc3e1 |  (U) |
| 0x3e8 | `0x180072e79` | inline | `83 F9 01 77 ?? 48 8B 05 ?? ?? ?? ?? 48 63 D1 48 69 CA A0` | 0 | 0 | 1 | 0x1fde00 |  (U) |
| 0x3f0 | `0x180072f3e` | find1 | `83 F8 02 7C ?? B8 FF` | 2 | 0 | 1 | 0x1fe57f |  (U) |
| 0x3f8 | `0x18007303a` | find1 | `E8 ?? ?? ?? ?? 89 9F E8 00 00 00 48 8B` | 0 | 1 | 1 | 0x200ad0 | call target in DLTX string code (W) |
| 0x400,0x408 | `0x180073104` | findall | `FF C0 48 FF C1 48 83 F9 02` | 8 |  | 2 | 0x1fe16e,0x1fe19e |  (U) |
| 0x410 | `0x18007323e` | find1 | `83 B9 CC 00 00 00 02` | 2 | 0 | 1 | 0xaeccae |  (U) |
| 0x418 | `0x1800732f4` | find1 | `C7 86 CC 00 00 00 03 00 00 00` | 2 | 0 | 1 | 0xaecdf6 |  (U) |
| 0x420 | `0x1800733a6` | find1 | `39 97 CC 00 00 00` | 2 | 0 | 1 | 0xaee4a0 | site inside CS::PartyMemberInfo::ReqInvadeNPCWorld* (W) |
| 0x428 | `0x18007345f` | find1 | `83 BB D0 00 00 00 00 75 ?? 32` | 2 | 0 | 1 | 0xaed31b |  (U) |
| 0x430 | `0x18007352d` | find1 | `83 B8 D0 00 00 00 00 0F 84` | 2 | 0 | 1 | 0x58dd47 | site inside CS::CSEmkActionForEvent::ExecuteEmkAction* (WorldMapPointParam user) (W) |
| 0x438,0x440,0x448 | `0x180073610` | findall | `41 83 BE D0 00 00 00 00 74` | 3 |  | 3 | 0xaecf66,0xaed0f6,0xaeea32 |  (U) |
| 0x450 | `0x18007378b` | find1 | `8B 81 D0 00 00 00 48 8B F1` | 2 | 0 | 1 | 0xaecc9d |  (U) |
| 0x458 | `0x1800738e9` | find1 | `83 B8 D0 00 00 00 00 74 ?? E8 ?? ?? ?? ?? 8B 88 D0` | 2 | 0 | 1 | 0x6022ea |  (U) |
| 0x460 | `0x1800739b4` | find1 | `83 BF D0 00 00 00 00 0F 85` | 2 | 0 | 1 | 0xaee478 | site inside CS::PartyMemberInfo::ReqInvadeNPCWorld* (W) |
| 0x468 | `0x180073af7` | find1 | `83 B8 D4 00 00 00 00 74 ?? E8 ?? ?? ?? ?? 8B 88 D4` | 2 | 0 | 1 | 0xc30be4 | site inside CS::MoveMapStep::MoveMapStep (W) |
| 0x470 | `0x180073bbe` | find1 | `8B 86 D4 00 00 00 89 4C` | 2 | 0 | 1 | 0xaecdc0 |  (U) |
| 0x478,0x480,0x488,0x490 | `0x180073d5f` | findall | `E8 ?? ?? ?? ?? 83 B8 DC 00 00 00 00 7C ?? E8 ?? ?? ?? ?? 8B 90 DC` | 7 |  | 4 | 0x5c0dfd,0x5c0ecd,0x5c0f9d,0x5c106d | sites inside a function referencing "SummonReloadStart" (4 matches expected) (W) |
| 0x498 | `0x180073ee1` | find1 | `80 BE EC 00 00 00 00` | 2 | 0 | 1 | 0xaef4d2 |  (U) |
| 0x4a0 | `0x180073f82` | find1 | `C6 86 EC 00 00 00 00` | 2 | 0 | 1 | 0xaef4df |  (U) |
| 0x4a8 | `0x180074030` | find1 | `C6 80 EC 00 00 00 01 48` | 2 | 0 | 1 | 0xc35a3d |  (U) |
| 0x4b0 | `0x1800740e5` | find1 | `41 C6 86 ED 00 00 00 00` | 3 | 0 | 1 | 0xaeefb2 | site inside CS::PartyMemberInfo::Update_PartyMemberS* (W) |
| 0x4b8 | `0x18007419a` | find1 | `C6 80 ED 00 00 00 01 E8` | 2 | 0 | 1 | 0x5bf0f0 | site inside a function referencing BlockClear/SoloBlockClear strings (W) |
| 0x4c0,0x4c8 | `0x180074293` | findall | `C6 80 ED 00 00 00 01 48 8B CB E8` | 2 |  | 2 | 0x5bff4d,0x5c0175 |  (U) |
| 0x4d0 | `0x1800743ae` | find1 | `41 80 BE ED 00 00 00 00 0F 84` | 3 | 0 | 1 | 0xaee9dd | site inside CS::PartyMemberInfo::Update_PartyMemberS* (W) |
| 0x4d8 | `0x18007445c` | find1 | `44 8D 70 03 8B 46 F8` | 3 | 0 | 1 | 0xaef66f |  (U) |
| 0x4e8,0x4f0 | `0x180074569` | findall | `41 BF 03 00 00 00 41 BC FF FF FF FF` | 2 |  | 2 | 0xaeceef,0xaed07f |  (U) |
| 0x4f8 | `0x1800746a9` | find1 | `48 83 FA 03 7C ?? 45` | 3 | 0 | 1 | 0xaed691 | site inside MsgRepository code (W) |
| 0x500 | `0x18007475f` | find1 | `48 83 FA 03 7C ?? 32 C0 4C` | 3 | 0 | 1 | 0xaed6b5 | site inside MsgRepository code (W) |
| 0x508 | `0x18007481f` | find1 | `49 83 F9 03 7C ?? 48 83 C4 28` | 3 | 0 | 1 | 0xaedb69 |  (U) |
| 0x510 | `0x180074921` | find1 | `83 FE 03 4C 8D 05 ?? ?? ?? ?? B9 FF` | 2 | 0 | 1 | 0xaeef9c | site inside CS::PartyMemberInfo::Update_PartyMemberS* (W) |
| 0x518 | `0x1800749e4` | find1 | `41 BC FF FF FF FF 44 8D 77 03` | 9 | 0 | 1 | 0xaed1d5 |  (U) |
| 0x520 | `0x180074a93` | find1 | `41 BC 03 00 00 00 41 BD` | 2 | 0 | 1 | 0xaef4ee |  (U) |
| 0x550 | `0x180074bc5` | inline | `48 8D BE F0 D5 00 00 41 8B CD` | 0 | 0 | 1 | 0x7c582c | site inside CSCam/CSPersCam code (W) |
| 0x558 | `0x180074d30` | find1 | `88 44 24 31 48 8B 5B 08 E8 ?? ?? ?? ?? B9 03 00 00 00 BA 01` | 13 | 0 | 1 | 0x576a7e |  (U) |
| 0x560 | `0x180074e32` | find1 | `89 44 24 30 E8 ?? ?? ?? ?? B9 03 00 00 00 BA 01` | 9 | 0 | 1 | 0x581c1a |  (U) |
| 0x568 | `0x180074f1b` | inline | `66 41 83 E6 03` | 0 | 0 | 1 | 0x576b9d |  (U) |
| 0x570 | `0x18007504f` | inline | `66 0B D0 66 83 E1 03` | 0 | 0 | 1 | 0x581f4e |  (U) |
| 0x578 | `0x180075152` | inline | `8B 49 08 8B C3` | 0 | 0 | 1 | 0x588d22 |  (U) |
| 0x580 | `0x180075283` | inline | `8B 49 08 83 E9 01 41` | 0 | 0 | 1 | 0x5970a2 |  (U) |
| 0x588 | `0x180066ba8` | inline | `48 8D 7B 58 48 8B 1F` | 0 | 0 | 1 | 0x72ae9e |  (U) |
| 0x590 | `0x1800753b7` | inline | `48 8B 42 20 48 F7 D9` | 0 | 0 | 1 | 0x7296ef |  (U) |
| 0x598 | `0x1800754eb` | inline | `48 8B 47 20 48 8D 34 C1` | 0 | 0 | 1 | 0x729781 |  (U) |
| 0x5a0 | `0x18007561f` | inline | `49 8B 51 20 4D 8D 04 D0` | 0 | 0 | 1 | 0x729fdd |  (U) |
| 0x5a8 | `0x1800756ea` | find1 | `41 83 FF 04 0F 8C 05 99 2A FE` | 3 | 0 | 1 | 0x1f329c4 |  (U) |
| 0x5b0 | `0x180075795` | find1 | `41 B8 90 00 00 00 49 3B` | 2 | 0 | 1 | 0x4fe832 | site inside CS::AtqSpEffectEquipCtrl code (W) |
| 0x5b8 | `0x180075840` | find1 | `41 B9 90 00 00 00 4C 2B` | 2 | 0 | 1 | 0x4feabf |  (U) |
| 0x5c0 | `0x1800758ee` | find1 | `BA 18 00 00 00 32` | 1 | 0 | 1 | 0x4ff047 |  (U) |
| 0x5c8 | `0x18007599c` | find1 | `41 83 FC 18 7D ?? 8B` | 3 | 0 | 1 | 0x4fee75 |  (U) |
| 0x5d0 | `0x180075a47` | find1 | `8D 46 FA 83 F8 11` | 5 | 0 | 1 | 0x4ff6f7 |  (U) |

## 6. Installer patterns (hook installer `FUN_18001edb0`, owned by the hooks worker)

| Lookup call | Pattern | Role | Result |
|---|---|---|---|
| `0x18001f060` (parse at `0x18001ef11`) | `B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 11 05 ?? ?? ?? ??` | find-all A over the image | 267 hits (C) |
| `0x18001f14b` (parse at `0x18001ef7d`) | `72 ?? 48 8D ?? ?? ?? ?? ??` | first match within 0x80 bytes after each A hit; byte `0x72` becomes `0xEB` | 267 sites, one per A hit (C). Meaning of the patch: U (a `mov ecx,imm; call; movss [rip+x],xmm0` sequence followed by `jb`; looks like a per-entry initialiser with a range check) |

Patterns are plain ASCII stack strings (not XOR-obfuscated) in the installer.

## 7. Open unknowns

- Where `table[0]`/`table[1]` come from (module base/size); the builder is entered from protected code.
- The 108 unnamed lookups and the purpose of the mode-2 globals; they need a call-graph pass over `nightreign.exe`.
- Contents of the encrypted part of the second `.text`; only one signature (`+0x1b8`) lives there.
