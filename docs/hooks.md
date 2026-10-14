# nrsc.dll hooks: installer `FUN_18001edb0` and the detour bank

Source module `nrsc\hooks\hooks.cpp` (compiler path `.rdata 0x18014c6c4`). Evidence: `evidence/pseudocode/0x18001edb0.c`,
`evidence/hooks/hooks.json` (machine-readable), REA/Ghidra pseudocode of the detours, `objdump` of `0x180025e00..0x18002d000`.
Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. Mechanism (C unless noted)

`FUN_18001edb0(ctx)` runs once; `ctx` is the Hooks object:

| ctx field | Meaning |
|---|---|
| `+0x08/+0x10/+0x18` | thunk arena base / cursor / capacity (0x20-byte slots; overflow is fatal `"out of memory"`, `hooks.cpp` 0x2f6 and 0x367) |
| `+0x28` | **signature table**: one `u64` per game address, filled by `FUN_1800655a0` (signatures.cpp) before the installer runs |
| `+0x38` | patch registry: image range, vector of patched addresses (`FUN_18002cad0`), MinHook original-pointer slots `+0xb0/+0xb8/+0xc0` for the three kernel32 hooks |
| `+0x40..+0xe8` | per-module sub-structs holding original-function pointers (`+0x48`, `+0x50`, `+0x58`, `+0x60`, `+0x68`, `+0x78`, `+0x88` = param_repository, `+0xa0` = Ec* conditions, `+0xc8`) and module state; `+0xb8` = engine memory table (see dantelion_memory.md) |

**Hook library: three mechanisms, all in `hooks.cpp`.**

1. **MinHook, statically linked** (**S**, 6 hooks): `FUN_1800068d0` = `MH_CreateHook(target, detour, &orig)` (spin-lock init flag at `0x180182f78`, `VirtualQuery`
   checks for committed + executable pages, relay buffer within +-0x40000000 of the target, `FF 25` relay, inlined HDE64 jump tables at
   `0x180008560`), `FUN_1800095f0` = `MH_EnableHook`. Used for hooks 4, 6, 11 and the three kernel32 hooks.
2. **Custom call-site / jump patcher** (**C**, 12 hooks): the target address is the first byte of an `E8` call site or an `E9`/function entry. The installer
   writes `E8` or `E9 rel32` to a 16-byte relay in the arena; the original is the old rel32 target (`target + *(int*)(target+1) + 5`) saved to an
   orig slot (when the hook has one). Protection is changed through the IAT slot `0x180161260` (VirtualProtect), `FUN_18002cad0` records each patched address.
3. **Vtable slot swap** (**C**, 12 hooks): `slot = *(tbl+X)+off` (the table entry is a vtable found by RTTI name); old value is saved, the slot gets an arena relay.
   `FUN_1800289b0` is the helper used twice, the others are inlined.

**Arena relay** (**C**): per hook, a std::function-style closure `{vtbl, ctx, detour}` (0x40 bytes, `+0x38` self pointer; vtables at `0x180147200..0x1801475a0`,
`_Do_call` is slot 2) and a 0x20-byte thunk `mov rax,closure; movq xmm5,rax; mov r11,invoker; jmp r11`, plus a 0x10-byte `mov rax,thunk; jmp rax`
reachable by rel32. The *invoker* (`FUN_18002bae0`, `0x1800284d0`, `0x18002af20`, ...) reads the closure back with `FUN_18007d9d0` (`movq rax,xmm5`) and calls
`closure->vtbl[2]`, which calls `detour(ctx, args...)`. This is why every detour has `ctx` as its first parameter.

## 2. Installation order

0. EB patch set (scan, then `0x72 -> 0xEB`; patterns inline: `B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 11 05 ?? ?? ?? ??` then `72 ?? 48 8D ?? ?? ?? ?? ??` within 0x80 bytes). Purpose **U**.
1. Hooks 1 to 27 (game) in the order of the table below (installer address order, `0x18001f549` .. `0x180020be8`).
2. NOP patch: 2 bytes at table `+0x180` set to `90 90` (`0x180020c..`), then `thunk_FUN_18061e518(ctx)` (protected, **U**).
3. kernel32 hooks 28 to 30 (`GetThreadContext`, `SetUnhandledExceptionFilter`, `AddVectoredExceptionHandler`), found with `GetModuleHandle`/`GetProcAddress` and XOR-obfuscated names.
4. `NETWORK` memory block resize (dantelion_memory.md).

Total **30 hooks** (27 game, 3 kernel32) in the installer. More hooks are installed from protected code (section 5).

## 3. Hook table

What each detour does is in section 9 (2026-10-15); the "Owner" column below predates it.

"Sig lookup" is the address of the call in `FUN_1800655a0` that fills the table entry (join key for the signatures worker). Lookup kinds:
`0x18002c340` raw pattern scan, `0x18007c8f0` scan+resolve (mode 0 = match+off, 1 = rel32 call/jmp target, 2 = rip-relative data), `0x18007d380` RTTI scan (vtable by type-descriptor name).

| # | Kind | Target (table offset) | Sig lookup | Detour | Orig slot | Owner | Grade |
|---|---|---|---|---|---|---|---|
| 1 | vtable-slot | tbl+0x228 slot +0x58 (vtbl of DLRuntimeClassImpl<CSLuaEventScriptImitation>) | `0x180069276` | `0x180028990` | ctx+0x48 -> *[0] | cs\auto_control_api (S: same game class family as hook 16) | S |
| 2 | jmp-patch (E9, no trampoline) | tbl+0x108 | `0x180065989` | `0x180004c80` | - | unknown (W: anti-cheat/online check bypass) | S |
| 3 | call-patch (E8) | tbl+0x110 | `0x180065aba` | `0x180004c80` | - | unknown | S |
| 4 | inline-hook (MinHook) | tbl+0x118 | `0x180065b8a` | `0x180028ac0` | ctx[0x50]+0x18 | unknown (W: session/message module) | S |
| 5 | call-patch (E8) | tbl+0x120 | `0x180065d4c` | `0x180028bf0` | ctx[0xc8]+0x20 (= call target) | unknown | S |
| 6 | inline-hook (MinHook) | tbl+0x128 | `0x180065e5f` | `0x180028c20` | ctx[0x60]+0x40 | nrsc\cs\auto_control_api | C |
| 7 | jmp-patch (E9 at function entry) | tbl+0x130 | `0x180065fc5` | `0x180029140` | - | nrsc\mod\param_repository | C |
| 8 | call-patch (E8) | tbl+0x138 | `0x180066145` | `0x1800292f0` | ctx[0x78]+0x18 | unknown (W: party/session events) | S |
| 9 | call-patch (E8) | tbl+0x140 | `0x18006621b` | `0x1800292f0` | - | unknown | S |
| 10 | call-patch (E8) | tbl+0x148 | `0x18006638c` | `0x1800295e0` | ctx[0x88]+0x8 | nrsc\mod\param_repository | C |
| 11 | inline-hook (MinHook) | tbl+0x150 | `0x1800664db` | `0x1800297c0` | ctx[0x88]+0x10 | nrsc\mod\param_repository | C |
| 12 | jmp-patch (E9 at function entry) | tbl+0x158 | `0x180066671` | `0x180029860` | - | unknown | S |
| 13 | call-patch (E8) | tbl+0x160 | `0x18006678e` | `0x180029920` | ctx[0x78]+0x28 | unknown | S |
| 14 | call-patch (E8) | tbl+0x168 | `0x18006682c` | `0x180029990` | ctx[0x78]+0x30 | unknown | W |
| 15 | call-patch (E8) | tbl+0x170 | `0x180066983` | `0x1800299c0` | - | unknown | S |
| 16 | call-patch (E8) | tbl+0x178 | `0x180066a99` | `0x180029b20` | - | unknown | S |
| 17 | vtable-slot | tbl+0x200 slot +0x58 (vtbl of DLRuntimeClassImpl<CSAutoControlAPI>) | `0x180069113` | `0x180029ec0` | ctx[0x40] | nrsc\cs\auto_control_api | S |
| 18 | vtable-slot | tbl+0x238 slot +0x8 (vtbl of PlayerDebugSession) | `0x180069304` | `0x18002aaa0` | ctx[0x68]+0x28 | unknown (W: session/network bridge) | S |
| 19 | vtable-slot | tbl+0x240 slot +0x18 (vtbl of PlayerNetworkSession) | `0x18006934b` | `0x18002abb0` | ctx[0x68]+0x30 | unknown | S |
| 20 | vtable-slot | tbl+0x210 slot +0x20 (vtbl of CSQuickMatchLobbyContext) | `0x1800691a1` | `0x18002ad20` | ctx[0x58]+0x8 | unknown (W: matchmaking) | S |
| 21 | vtable-slot | tbl+0x210 slot +0x28 (vtbl of CSQuickMatchLobbyContext) | `0x1800691a1` | `0x18002ad40` | - | unknown | W |
| 22 | vtable-slot | tbl+0x210 slot +0x68 (vtbl of CSQuickMatchLobbyContext) | `0x1800691a1` | `0x18002ae00` | - | unknown | S |
| 23 | vtable-slot | tbl+0x248 slot +0x8 (vtbl of EcTestPlayerNo) | `0x180069392` | `0x18002ae90` | - | unknown | S |
| 24 | vtable-slot | tbl+0x250 slot +0x8 (vtbl of EcTestChrDead) | `0x1800693d9` | `0x18002afa0` | ctx[0xa0]+0x50 | unknown | S |
| 25 | vtable-slot | tbl+0x258 slot +0x8 (vtbl of EcTestTeamType) | `0x180069420` | `0x18002afc0` | ctx[0xa0]+0x58 | unknown | S |
| 26 | vtable-slot | tbl+0x260 slot +0x8 (vtbl of EcTestEventFlagState) | `0x180069467` | `0x18002b1f0` | ctx[0xa0]+0x60 | unknown | S |
| 27 | vtable-slot | tbl+0x268 slot +0x8 (vtbl of EcTestSortieMemberNum) | `0x1800694ae` | `0x18002b230` | ctx[0xa0]+0x68 | unknown | S |
| 28 | inline-hook (MinHook) | kernel32!GetThreadContext | `-` | `0x180025f00` | ctx[0x38]+0xb0 | nrsc\hooks (anti-debug / anti-tamper) | C |
| 29 | inline-hook (MinHook) | kernel32!SetUnhandledExceptionFilter | `-` | `0x180025f50` | ctx[0x38]+0xb8 | nrsc\hooks | W |
| 30 | inline-hook (MinHook) | kernel32!AddVectoredExceptionHandler | `-` | `0x180025f80` | ctx[0x38]+0xc0 | nrsc\hooks | W |
| 28-30 | MinHook | kernel32 exports | - | see per-hook notes | `ctx[0x38]+0xb0/0xb8/0xc0` | nrsc\hooks | C/W |

(Rows 28-30 are listed individually in `hooks.json`.) **Targets from signatures: 27 of 30** (15 pattern, 12 RTTI vtable); the table also holds unused entries (see section 6).
The three kernel32 hooks use exports (IAT/GetProcAddress, no signature).

**Runtime confirmation (2026-10-15, C):** in the live capture under Proton (`evidence/runtime/20261015-live_capture_analysis/FINDINGS.md`)
all 27 table-based hooks are patched. Each goes into a private RWX thunk arena (`0xffe00000`) and on to an nrsc.dll invoker. Detour,
invoker and closure vtable match this table for 27/27. No nrsc hook exists outside this table and the protected installer's table.
The 9 other jump hooks in the game image belong to ME3 (`me3_mod_host.dll`).

## 4. Per-hook notes

Full per-hook details (detour reference site, invoker, closure vtable, install site, summary) are in `evidence/hooks/hooks.json`.
Highlights:

- **Hooks 6, 17**: `auto_control_api`. 17 captures the game's method registration into `ctx[0x40]`; 6 is the `SignIn` auto-call in world_chr_man.md section 4 (**C**).
- **Hooks 7, 10, 11**: `param_repository` (param_repository.md). Note: hook 11 is `FUN_1800297c0` and is a MinHook (the numbering of the table is installer order).
- **Hooks 2, 3**: both use the shared stub `0x180004c80` (`mov al,1; ret`), so the patched function/call returns true (**S**). The same stub is also referenced from `.rdata 0x18014aa98`.
- **Hooks 23-27**: `EcTest*` condition classes (event/ESD style condition evaluators). Hook 27 and 23 cap or fold the party size to 3 unless a preference bit (`FUN_180009c90`) is set; hook 25 extends a team condition with ids 10002..10006. These implement party-size > 3 compatibility (**S**).
- **Hooks 18-19**: `PlayerDebugSession` / `PlayerNetworkSession` slot hooks that mirror session messages (types 0x18, 1) to registered listeners; they are the bridge to `server_manager`/networking (**W**).
- **Hooks 15, 16**: object factories (0x78 and 0xe0 byte ref-counted objects with two std::function members) (**S**); the class is **U**.
- **Hook 28**: zeroes debug registers in `CONTEXT` after `GetThreadContext`: anti-debug / hardware-breakpoint hiding (**C**). 29, 30 guard exception-handler registration by module range (**W**).

## 5. Detour bank beyond the installer

`0x180025e00..0x18002d000` contains (from `.pdata` and the closure vtables):

- The 30 detours above and their invokers (`0x180028160`, `0x1800280f0`, `0x180028320`, `0x180028530`, `0x18002af20`, `0x18002b380`, ...).
- *Protected-installed detours*, with no references from the readable installer (their addresses are stored only by `.themida` code): `0x180026830`, `0x180026b30`, `0x180027090` (world_chr_man, bloodstain_man), plus `0x180026250`, `0x180026370`, `0x1800264e0`, `0x1800265f0`, `0x180026700`, `0x180026c50`, `0x180027870` (see JSON). Their targets are **U** but their ctx offsets (`+0x60`, `+0x68`, `+0x80`, `+0x90`, `+0xb0`) are known.
- *Install helper templates* (call-site/jmp/MinHook with arena relay) `0x180026970`, `0x180027150`, `0x1800273a0`, `0x180027650`, `0x180027b10`, `0x180027e00`: used only from protected code.
- Closure/`std::function` boilerplate, SEH unwind funclets (`Unwind@...`), and the hex pattern parser `0x18002bf30`, BMH scanner `0x18002c340`, registry `0x18002cad0`.

## 6. Signature table (join with signatures worker)

Builder `FUN_1800655a0` (17.7 KB, `0x1800655a0..0x180069af5`, refs `signatures.cpp` at `0x1800698f9`, `0x18006999b`, `0x180069a3d`, `0x180069adf`). Patterns are XOR-obfuscated stack constants decoded
at run time. Lookup fatal text format at `.rdata 0x18014c25c`, `signatures.cpp` line `0x3e8`. Entries `+0x100..+0x178`, `+0x180`, `+0x188..+0x1f8`, `+0x270`, `+0x588` are patterns; `+0x200..+0x268` are RTTI names:
`DLRuntimeClassImpl<CSAutoControlAPI>`, `PartyMemberInfo`, `CSQuickMatchLobbyContext`, `CSChaosMatchRateDecreaseJob`, `CSChaosMatchRateUpdateJob`, `DLRuntimeClassImpl<CSLuaEventScriptImitation>`, `ChrFinderImp` (in `WorldChrManImp::UpdateMultiDoping`), `PlayerDebugSession`, `PlayerNetworkSession`, `EcTestPlayerNo`, `EcTestChrDead`, `EcTestTeamType`, `EcTestEventFlagState`, `EcTestSortieMemberNum`.
Exact lookup call addresses per entry are in the JSON (`sig_lookup_call`, `sig_store`).

## 7. Proposed C++ design (not in src/)

`nrsc/hooks/`: `arena` (thunk arena + closure relay), `hook_installer` (call-site, jmp, vtable slot and MinHook back ends behind one `install(target, detour, orig*)`),
`hook_table` (the ordered list above, data-driven from the signature table), per-module detour files owned by their modules
(`params/hooks`, `cs/auto_control_api`, `cs/party_conditions` for hooks 20-27, `net/session_bridge` for 18-19), and `anti_tamper` (hooks 28-30 and the EB patch).

## 8. Open unknowns

- Purpose and game functions behind the table offsets (signature bytes not decoded here; signatures worker).
- Hooks installed from protected code and what `thunk_FUN_18061e518` does.
- Target semantics of hooks 2, 3, 4, 5, 12-16, 20-22; purpose of the `72 -> EB` patch set and the NOP patch.
- Exact role of ctx sub-structs (+0x40..+0xe8) and of `FUN_18003f030` (event fan-out) and the wrapper at `0x18002d290`.
- Hooks 29/30 behaviour beyond the range check (**W**).

## 9. Detour behaviour (2026-10-15)

Static read of every detour body (45 unique detours: the 27 game hooks and 3 kernel32 hooks of this installer, plus the 17 from the protected
installer `0x1800221e0`), joined with the live capture (`evidence/runtime/20261015-live_capture_analysis/FINDINGS.md`: every site is patched,
and detour, invoker and closure vtable were verified live for 27/27 + 16/16). Full write-ups with evidence and pseudocode:
`evidence/hooks/detours/group1..4.md` / `.json`. "orig" says whether the detour calls the game's original. The **ctx** is the Hooks object; its
fields `ctx[0x58..0xe8]` are filled by protected code (types mostly W/U).

| Detour | Hook | Hooked game function | orig | What it does | Grade |
|---|---|---|---|---|---|
| `0x180004c80` | #2 | `0x140f271d0`, start of the EOS (Easy Anti-Cheat) session | no | `return true`: the EOS anti-cheat session is never started | S |
| `0x180004c80` | #3 | call `0x140bc7f90` in `CSNetworkFlowStep::STEP_NotifyLeaveOfflineModeWait` | no | forces true, so the step returns early and skips the "leave offline mode" notice (text 0x1068) | S/W |
| `0x180025f00` | #28 | kernel32 `GetThreadContext` | yes, first | zeroes Dr0-3, Dr6, Dr7 and the debug-control/LBR fields: hides hardware breakpoints | C |
| `0x180025f50` | #29 | kernel32 `SetUnhandledExceptionFilter` | conditional | filters inside a protected-written range are not installed | C/W |
| `0x180025f80` | #30 | kernel32 `AddVectoredExceptionHandler` | yes | wraps in-range handlers with `0x18002d290`, which passes a zeroed CONTEXT | C/W |
| `0x180026200` | P0 | ctor `0x1401fa740` of the 0x458-byte global at `0x143c078d0` (GameDataMan-like, W) | yes, first | resets nrsc ctx fields, zeroes 5 bytes at `this+0x20` | W |
| `0x180026230` | P1 | `0x140dfe070`, collect pending type-0x94 peer payload | yes | pure pass-through (captures the original) | C/U |
| `0x180026250` | P2 | `0x140dfe4f0`, send payload to peers | no | reimplemented: sends to every peer not yet sent to, without the game's first/last index range (larger party) | C/S |
| `0x180026310` | P3 (MinHook) | `0x140572c10`, map item visible to the local player | yes | true if setting `+0x194`, else original OR owner-bit test: **shared loot** | C/S |
| `0x180026370` | P4 (MinHook) | `0x140581b20`, MapItemMan request | conditional | in mode `(W&5)==1` sends a 0x28-byte message to all ready peers instead | S/W |
| `0x1800264e0` | P5 (MinHook) | `0x1405769d0` | conditional | runs the original only when id != -1 and mode bits allow, under a lock | W |
| `0x1800265f0` | P6 | `0x1405f7fb0`, handle → (chr_a, chr_b) | no | picks the output slot from the handle type (8/6) instead of `chr+0x190` | C/S |
| `0x180026690` | P7 | `0x1403d3fe0`, "local player or owned" | no | true when `*ctx[0xe0]` bit 0 is clear, else session state == 3 | W |
| `0x1800266c0` | P8 | `0x140df0af0` (CSSessionManager key lookup) in `CSBattleGroup::CheckIsAllDead` | conditional | with mod flag set: `session+0xc == 3` instead of the map lookup | S |
| `0x180026700` | P9 | `0x140df24d0`, CSSessionManager broadcast (this site: type 0x2c) | fallback | unicast to the peer id at `ctx[0x90]+0x28/+0x2c`; broadcast if both are -1 | S |
| `0x180026c50` | P10 | ctor `CS::PartyMemberInfo` | no | allocates **0x180 bytes / 6 member slots** instead of 0xf0 / 3: **party 3→6** | S |
| `0x180026e50` | P11 | PartyMemberInfo reset | tail | resets all 6 slots (keeps state-2 records when session `+0x10 == 5`) | S |
| `0x180027090` | P12 | ctor `0x1407286d0` (0x68 bytes, 3 slots) in `CS::InGameStep::_Common_Initialize` | no | 0x78-byte, 6-slot block (bloodstain_man) | S |
| `0x180027310` | P13 | `0x1401fecb0`, `bool f(playerId)` on `0x143c078d0` | no | lookup over 5 records (stride 0x8a0) instead of 2, plus an nrsc flag array | W |
| `0x180027870` | P14 | `0x1403df9b0`, ChrIns-family action | yes, first | repeats the call for every other player (`handle + id*100`, or ids 1..6) | W |
| `0x180027dc0` | P15 | `0x140ed6740`, effect-handle IsValid | no | tests `h[+0x18]` instead of `h[+0]` (unexplained) | W/U |
| `0x180028030` | P16 | **not reached here**: slot 0x5d0 is a live **1-byte patch** `cmp eax,0x11`→`0x2f` at `0x1404ff6f7` (`CS::AtqSpEffectEquipCtrl` slot 11: 3 → 8 groups of 6). The detour (`min(3 or 6, count)`) has no readable reference | – | devirt's "E9 jmp" label is wrong | C (patch) / W |
| `0x180028990` | #1 | `DLRuntimeClassImpl<CSLuaEventScriptImitation>` vtbl+0x58 → `0x142152fe0` (DLRF method register) | yes | pass-through; only captures the original | S |
| `0x180028ac0` | #4 (MinHook) | `0x1427f4d20`, FMG text lookup `(repo, ver, category, id)` | fallback | per-category handler map → nrsc messages (`0x18000a1c0`) → original: **localisation override** | S |
| `0x180028bf0` | #5 | `0x1425a2f40`, wstring assign in the `SaveLoad2::SLContentFormat` ctor | replaces | `.sl2` → `save_file_extension` (default `co2`) | S |
| `0x180028c20` | #6 (MinHook) | `0x140df9420`, bool predicate over a singleton list | tail | auto-calls `SignIn`, fatal if loaded too late, anti-piracy notice (auto_control_api) | S |
| `0x180029140` | #7 | `0x1403db250`, apply multiplayer-scaling SpEffect by player count | no | reimplemented for **2-6 players** (`ids[count-2]`, generated param 0x4f rows) | S |
| `0x1800292f0` | #8, #9 | `0x140202aa0`, `(GameDataMan*, PlayerGameData*, id)` setter | yes, first | if local player: sends the 4-byte id to all peers on P2P channel 7 | S |
| `0x1800295e0` | #10 | `0x140f4d3b0` (end of `CS::CSRegbndFileCap` vslot 9) | yes, first | once builds the extra scaling rows (param 0xf, 0x4f) | S |
| `0x1800297c0` | #11 (MinHook) | `0x140ea17e0`, param-0xf row getter | fallback | override row if present, else original | C |
| `0x180029860` | #12 | `0x1409e5b10`, byte-vector copy used by `CS::MatchingBossSelectDialog` | no | returns nrsc's byte list plus a forced u32 instead of the boss-id list | W |
| `0x180029920` | #13 | `0x1406c2900` (site `0x1fd6bb`) | yes, after | saves the entry's active byte (selects the fake rate range below) | W |
| `0x180029990` | #14 | `0x1401fe1c0`, getter of byte `+0xd8` on `0x143c078d0` | no | true if `ctx[0xe0]+0x195 != 0`, else the singleton's byte `+0xd0` (**hooks.json summary is inverted**) | C |
| `0x1800299c0` | #15 | creator of `CS::CSChaosMatchRateDecreaseJob` | no | local job, callback just sets done (`+0x15a`): no FromSoftware server round trip | S |
| `0x180029b20` | #16 | creator of `CS::CSChaosMatchRateUpdateJob` | no | local job with a random rate delta, sets done (`+0x159`) | S |
| `0x180029ec0` | #17 | `DLRuntimeClassImpl<CSAutoControlAPI>` vtbl+0x58 → `0x142152fe0` | yes, after | copies each method name into nrsc's FNV-1a map `ctx[0x40]` (read by #6's SignIn lookup) | S |
| `0x18002aaa0` | #18 | `PlayerDebugSession` vtbl+0x8 → `0x140e0ba70` (send) | other types | types 1 and 0x18 → all active peers via `0x18003f030`, channels 5/6 | S |
| `0x18002abb0` | #19 | `PlayerNetworkSession` vtbl+0x18 → `0x140e0bce0` (receive) | other types | types 1 and 0x18 are read from nrsc's peer queue (`0x180040370`, 0x400 buffer) | S |
| `0x18002ad20` | #20 | `CSQuickMatchLobbyContext` vtbl+0x20 → `0x140b45530` | tail | forces mode `+0x10 = 2` | S/W |
| `0x18002ad40` | #21 | `CSQuickMatchLobbyContext` vtbl+0x28 → `0x140b454a0` (setter `+0x98`) | no | stores the member count = vector size / **0xF0** (verified: divide-by-240 magic) | C |
| `0x18002ae00` | #22 | `CSQuickMatchLobbyContext` vtbl+0x68 → `0x140b45350` (setter `+0xc0`) | no | stores the byte and mirrors it as bit 6 of an nrsc word `**ctx[0x58]` | S |
| `0x18002ae90` | #23 | `EcTestPlayerNo` vtbl+0x8 → `0x1405a75d0` | no | player number `% 3` for listed script ids unless the preference is on | S |
| `0x18002afa0` | #24 | `EcTestChrDead` vtbl+0x8 → `0x14059d0a0` | tail | pure pass-through | C |
| `0x18002afc0` | #25 | `EcTestTeamType` vtbl+0x8 → `0x1405a9680` | yes, first | for id 0x4a0 in mode 2, retries team types 10002-10006 | S |
| `0x18002b1f0` | #26 | `EcTestEventFlagState` vtbl+0x8 → `0x1405a0270` | other flags | flag 0x7f8 / state 1 answered from an nrsc state byte | S/W |
| `0x18002b230` | #27 | `EcTestSortieMemberNum` vtbl+0x8 → `0x1405a8cf0` | no | member count (0xF0 records), **capped at 3 unless preference `0x180009c90` is on** | C |

**Corrections from this pass.** hooks.json #14 summary is inverted (C, disassembly `0x180029990`). Slot 0x5d0 is a byte patch, not an E9 hook
(C, live bytes; devirt table `0x1800221e0.hooks_table.txt` row 16 needs fixing in `devirt/`). The member record stride is 0xF0, not 0x78 as
group4.md says (C, the magic `0x8888888888888889` + add + `shr 7` divides by 240).

**Still open.** What fills `ctx[0x58]`, `ctx[0x60]`, `ctx[0x78]`, `ctx[0x90]`, `ctx[0xa0]`, `ctx[0xe0]`, `ctx[0xe8]` (protected code); preference
`0x180009c90` (key `0xa7f418`); meaning of message types 1/0x18/0x2c/0x94, flag 0x7f8, id 0x4a0, lobby mode 2; singleton names at
`0x143c078d0`, `0x143c21fc8`, `0x143c2d400`, `0x143c17248`. A two-player live capture with probes on those ctx fields would close most of these.

**Live two-player check (2026-10-15, test install, two players; `evidence/runtime/20261015-0206…0210*`).** The Hooks object
is shared by every closure (closure `+0x08`). Probes of its fields at roundtable, matchmaking and expedition:
- The member vector `ctx[0x60]->+0x28->[0]` (`+0x30/+0x38`) held **2 records of 0xF0 bytes** at all three moments (C: stride and count).
- Hook 25's id log `ctx[0xa0]+0x18` was empty at roundtable and matchmaking, and held **`0x4a0`** during the expedition (C: the 0x4a0
  team-type path runs in co-op expeditions; what 0x4a0 means is still W).
- `ctx[0x78]->[0]` bytes `+0xd8..+0xf8` change with the session phase (decoding open). The member records themselves were not reached by
  the depth-2 heap walk.
