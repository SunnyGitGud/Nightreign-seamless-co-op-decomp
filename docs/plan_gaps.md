# Plan: closing the nrsc.dll reconstruction gaps (2026-10-14)

Scope as agreed with the user on 2026-10-14: Themida devirtualization, hooks and their game targets, signatures,
server_manager/networking/sessions, and init are all in scope. Everything runs on Linux. nrsc.dll is never run
natively; its code is only executed inside Unicorn (`devirt/.venv`). `nightreign.exe` is read from disk and is
never executed by us. Grading is C/S/W/U, with evidence cited.

Inputs
- `../binary/SeamlessCoop/nrsc.dll`, sha256 `243eec92…ce24e` (read-only)
- `nightreign.exe` v1.03.3 at `<game-dir>` (set `NRSC_GAME_EXE` for the tests)
  (read-only). The nrsc.dll in that install is byte-identical to ours.

| # | Work package | Worker | Output | Depends on |
|---|---|---|---|---|
| 1 | Hook installer `FUN_18001edb0` + detour bank `0x180026000..0x1800296ff`: list every hook (target reference, detour, trampoline slot, owning module) | hooks | `docs/hooks.md`, `evidence/hooks/` | – |
| 2 | Signature table (`nrsc\signatures\signatures.cpp`): extract patterns and resolve them against `nightreign.exe` | signatures | `docs/signatures.md`, `evidence/signatures/` | joins with 1 by address |
| 3 | Devirt: fix the 3 faulting VM functions (`0x180040630`, `0x180061140`, `0x180064120`), lift semantics for all 7, narrow the 62 ambiguous IAT slots | devirt | `devirt/out/`, `docs/devirt.md` | – |
| 4 | Runtime: a Linux-side memory-only collector (`/proc/<pid>/mem`, no ptrace attach) for the game running under Lutris/GE-Proton | runtime-tool | `tools/linux/`, `docs/runtime_linux.md` | 1, 2 for addresses (later) |
| 5 | `server_manager`: decode the obfuscated strings, recover endpoints, request/response shapes and the call flow | server | `docs/server_manager.md`, `evidence/server_manager/` | – |

Integration (main session): merge the new modules into `src/`, `tests/` and `CMakeLists.txt`; update README status and
`docs/baseline.md` §4.

Caveat for 4: the test install does not use the regular Steam client. Network, session and lobby behaviour captured
there may differ from a legitimate Steam install. Non-network state such as hooks, params and world data is unaffected.

# Phase 2 (2026-10-15): a loadable nrsc.dll built from our source

Goal: a Windows x64 DLL, built on Linux, that ME3 loads like the original. It resolves the signatures, resizes the memory block,
installs the hooks of docs/hooks.md section 9, and runs the reconstructed modules. Two-player networking (the lobby/handler logic in the
VM functions `0x180061140` and `0x180064120`) is **phase 3**: until then the net detours fall back to the game's originals when no session
layer is attached. It is a functional reconstruction, not a byte-identical one (no thunk arena with xmm5 closures; plain relays and functions).

Decisions (user, 2026-10-15): Windows SDK/CRT via **xwin** (`~/.xwin`, clang-cl + lld-link); **MinHook v1.3.4** vendored in
`third_party/minhook` (BSD-2, commit c3fcafd) for inline hooks.

Not reconstructed, on purpose: hooks #28-#30 (GetThreadContext / SetUnhandledExceptionFilter / AddVectoredExceptionHandler: anti-debugging
of the original DLL), the anti-piracy notice in #6, and the EB/NOP patch set (purpose U).

Contracts (written by the main session; workers code against them, never change them without asking):
- `src/nrsc/hooks/hook.hpp`: `platform::CodeMemory`, `hooks::HookSpec/Kind/Installer/InlineBackend`.
- `src/nrsc/hooks/groups.hpp`: `groups::world/params/net(table, settings) -> std::vector<HookSpec>`.

| # | Work package | Owns (new files only, plus its cmake fragment) | Tests |
|---|---|---|---|
| 6 | Hook framework + platform: `Installer` (relay arena near the site, E8/E9 retarget, entry jmp, vtable swap, byte patch, `expect` check), POSIX and Win32 `CodeMemory`, MinHook `InlineBackend` (Windows only) | `src/nrsc/hooks/installer.cpp`, `src/nrsc/platform/`, `cmake/hooks.cmake` | Linux: patch real RWX code (mmap) and execute it |
| 7 | DLL target + toolchain: clang-cl/xwin toolchain file, `nrsc_dll` SHARED target (`nrsc.dll`), exports `modengine_ext_init` + `me3_load_early` (u64 1), init thread (settings → game module → live `ModuleImage` → `SignatureTable::build` → memory block → groups → `enable_inline`), fatal box `Seamless Coop %s - Fatal Error`, MinHook static lib | `cmake/toolchain-clang-cl-xwin.cmake`, `cmake/dll.cmake`, `src/nrsc/dll/` | DLL links; exports checked with `llvm-objdump`; Windows unit-test exe runs under GE-Proton's wine if possible |
| 8 | World group (party 3→6, conditions, DLRF capture, SignIn auto-call) | `src/nrsc/hooks/world/`, `cmake/hooks_world.cmake`, `tests/hooks/world_test.cpp` | detours called on fake game structs |
| 9 | Params group (EOS skip, offline notice, text override, save ext, scaling, rows, shared loot) | `src/nrsc/hooks/params/`, `cmake/hooks_params.cmake`, `tests/hooks/params_test.cpp` | same |
| 10 | Net group (P2P bridge #18/#19 via `server::MessageBus`, class-id broadcast, unicast/broadcast, matchmaking jobs, boss list) | `src/nrsc/hooks/net/`, `cmake/hooks_net.cmake`, `tests/hooks/net_test.cpp` | same, Steam mocked |

Rules for all: keep the existing 176 tests passing; grade every behaviour C/S/W/U in comments with the evidence address (docs/hooks.md section 9 and
`evidence/hooks/detours/group*.md`); where behaviour depends on a ctx field that protected code fills, use a named member of the group's
state with a documented default and say so; game structures are accessed by documented offsets; no Linux-only or Windows-only code outside
`src/nrsc/platform/` and `src/nrsc/dll/`. Do not touch `devirt/` or `binary/`. Do not run the game.

**Phase 2 status (2026-10-15): done, not yet tried in game.** 257 Linux tests pass (176 before + 81). `nrsc.dll` builds with clang-cl + xwin
(exports `modengine_ext_init`, `me3_load_early`; imports KERNEL32/USER32 only). `HooksRealBinary` (needs `NRSC_GAME_EXE`) builds the table from
nightreign.exe v1.03.3 and validates all 44 HookSpecs; all 44 sites coincide with the original's live patch sites (capture 20261015-012952).
Test package: `dist/` (DLL + settings + locale) and the ME3 profile `dist/nightreign-reconstruction-linux.me3`.
Known gaps before it behaves like the original (all need protected-code state, phase 3 or more analysis):
- NETWORK memory block resize is a logged no-op (block table source U).
- WorldState / ParamsState / NetState members that protected code fills default to "vanilla" behaviour: the party-size preference is off
  (event scripts see at most 3 members), #6 SignIn helpers are unset, #7 scaling `GameEffectApi` is null (scaling does nothing), and the text
  handler map and `messages` are unset (no localisation override), shared-loot settings are zero, and the net session is not attached.
- #12 and P2 are Inline (MinHook) instead of entry jumps so they can fall back; MinHook relocation of their prologues is untested.

**Phase 2b (wiring, 2026-10-15).** `world::wire`, `params::wire`, `net::wire` (docs/wiring_world.md, wiring_params.md, wiring_net.md) fill the
state from signature slots, grade C for most members, confirmed against the two-player probes. The DLL init now:
- resizes NETWORK to 30 MiB through `dantelion::find_block_table` (signature slot 0x1f0, sizes row 0, entry 17);
- calls the three `wire()`s right after their builders;
- **holds back #7 and #10** (scaling) because the 80 target ids are not recovered, so the game's own scaling runs.

The party session word (0 alone, 0x21 in a party) is one setter, `hooks::set_party_session_active()` (src/nrsc/hooks/session_word.cpp),
for the phase-3 session layer. Tests: 289 pass, plus 5 real-binary checks with `NRSC_GAME_EXE`.
Still open: the scaling target ids, the text key table / handlers, the event flags 0xa7f418 / 0x7f8 meaning, the boss list, the auto-control
task objects, and everything session-side (phase 3).

**Phase 3 status (2026-10-15): built, not yet run in game.** Session layer `src/nrsc/session/` (docs/session.md) on top of the other
session's server additions (callbacks.hpp, peer pool 5, AcceptSessionWithUser) and a Steam flat-API adapter
(`src/nrsc/platform/steam/`, docs/steam_adapter.md). The DLL drives it from an inline hook on `SteamAPI_RunCallbacks`
(src/nrsc/dll/session_driver.cpp): callbacks, tick and hooks share the game thread. Auto-host after 20 s (W). 332 tests pass.
Open (W/U): who creates/joins the lobby in the original, default timer rates, job queue payloads, `X+0x140`, the party bit 5 setter.
How to test: dist/TESTING.md.

**First in-game test (2026-10-15, solo).** The reconstruction loads, scans 146 signatures, installs its hooks, resizes NETWORK to 30 MiB,
starts the session and auto-hosts a Steam lobby. Starting a new game crashed (AV at nightreign.exe+0xed6d30 releasing a garbage effect
handle at object+0x58, called from 0x14072ad50). Bisected with `nrsc_disable_hooks.txt` (diagnostic) to **P12** (6-slot bloodstain block):
the game reads that block with its vanilla 0x68-byte layout; the original compensates somewhere not yet recovered. P12 is now held back
in init (vanilla 3-slot block). The DLL also has a diagnostic crash logger (src/nrsc/dll/crash_log.cpp) and writes build-win/nrsc.map.
Open: how the original makes the 6-slot bloodstain block safe (likely together with P15 / other readers of +0x58).
Solo expedition: "Matchmaking failed" with the reconstruction, and **also with the original nrsc.dll** (baseline test, same install), so it is
a limitation of the test install, not of the reconstruction. Bisects that ruled out our side: no lobby (`no-auto-host`), and
#20-#22 skipped.

**First two-player test (2026-10-15 03:37): works.** Host = friend running the **original** nrsc.dll 1.1.3; guest = this machine running the
reconstruction (DLL d98037b9…, P12/#7/#10 held back). Log `evidence/runtime/20261015-0337_two-player_rebuild-vs-original.log`:
GameLobbyJoinRequested → left own auto-hosted lobby → LobbyEnter friend's lobby (net hooks attached) → SessionRequest accepted →
the game's own expedition lobby correctly not adopted. The user reports the expedition played normally. So the reconstruction is
wire-compatible with the original (lobby tags, version check, P2P session, message handling). Not yet tested: rebuild as host,
rebuild on both sides, 3+ players.
