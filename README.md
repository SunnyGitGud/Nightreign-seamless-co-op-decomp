# nrsc.dll analysis and reconstruction

Seamless Co-op for **Elden Ring Nightreign** (`nightreign.exe`).

| | |
|---|---|
| Binary | `../binary/SeamlessCoop/nrsc.dll` (not modified) |
| SHA-256 | `243eec929a97b71e1e2e3b4215778b89c37d629436b8dd5403e830593d3ce24e` |
| Built | 2026-07-02, PE32+ x64, 9.2 MB; mod version **1.1.3** (banner text) |
| Protection | Themida, section `.themida` (`0x18019e000`, 7.6 MB); the entry point is inside it |
| Exports | `modengine_ext_init` (`.text 0x180001220`), `me3_load_early` (data, `.rdata 0x180145f00`) |

**Rules.** Read-only analysis of the shipped files (Ghidra 12.1.2 via REA, `objdump`, byte reads); the DLL and the game exe are never
modified or run natively. Since 2026-10-14 the scope includes hooks, signatures, `server_manager` and the Themida-virtualized functions.
Those are emulated only in Unicorn on Linux (`devirt/`). The gap-closing plan is [docs/plan_gaps.md](docs/plan_gaps.md). Hooks are documented
([docs/hooks.md](docs/hooks.md)) and rebuilt as code (`src/nrsc/hooks/`).

**Start here:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (modules, start-up order), [docs/BUILD.md](docs/BUILD.md) (build and test),
[docs/TODO.md](docs/TODO.md) (known problems), [docs/REFACTOR_PLAN.md](docs/REFACTOR_PLAN.md) (2026-10-15 refactor audit).

**Relationship to `ersc.dll`.** Same author and framework, different program. See
[docs/baseline.md](docs/baseline.md) §3. Nothing from `../reconstruction/` is assumed to hold for `nrsc.dll`
unless re-verified here.

## Status

| Module | Status | Tests |
|---|---|---|
| Settings (`nrsc\mod\preferences\preferences.cpp`) | **done**: parser, reader, all 5 keys, validation, fatal paths | 42 |
| Localisation (`message_repository/locale.h`) | **done** for readable parts: loader and error handling, lookup, override, UTF-16. Key table, fixed-text placement and language choice are only in protected code; see [docs/localisation.md](docs/localisation.md) | 14 |
| Param repository (`nrsc\mod\param_repository\param_repository.cpp`) | **done**: param row lookup, multiplayer scaling builder (`YuiNexusRegulationManager`), override lookup, effect choice by player count. Hook targets and the target-id source are open; see [docs/param_repository.md](docs/param_repository.md) | 23 |
| `world_chr_man`, `bloodstain_man`, `auto_control_api` | **done**: CSChrSet allocators (6 players, 120 NecroBuddy), bloodstain block (6 slots), the `SignIn` auto-call with its fatal and notice. Hook targets are open; see [docs/world_chr_man.md](docs/world_chr_man.md) | 11 |
| `dantelion\memory` | **done**: the engine memory block `NETWORK` is set to 30 MiB, with its fatal message. It is inlined in the hook installer `FUN_18001edb0`; see [docs/dantelion_memory.md](docs/dantelion_memory.md) | 5 |
| Signatures (`nrsc\signatures\signatures.cpp`) | **done**: Pattern16 parser, scanner, resolve modes, find-all slots, RTTI vtable lookup, the 168-entry table (generated from `evidence/signatures/signatures.json`). Optional test `NRSC_GAME_EXE=<nightreign.exe>` resolves all 178 slots on v1.03.3 with 0 mismatches; see [docs/signatures.md](docs/signatures.md) | 49 |
| Hooks (`nrsc\hooks\hooks.cpp`) | **reconstructed** (phase 2, 2026-10-15): installer + 44 hooks in 3 groups (world, params, net); all 44 sites match the original's live patch sites; net hooks fall back to the game until phase 3. See [docs/hooks.md](docs/hooks.md) §9, [docs/plan_gaps.md](docs/plan_gaps.md) | 81 |
| Session layer (`src/nrsc/session`, `src/nrsc/platform/steam`) | **built** (phase 3, 2026-10-15): JoinFlow (LobbyDataUpdate 0x180064120, C), FrameTask (0x180061140 limiters + handshake, C), party word, Steam flat-API adapter + 8 callbacks; lobby create/join flow W; driven from a SteamAPI_RunCallbacks hook on the game thread. See [docs/session.md](docs/session.md), [docs/phase3_vm_networking.md](docs/phase3_vm_networking.md) | 36 |
| Windows DLL (`src/nrsc/dll`) | builds on Linux with clang-cl + xwin: `cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clang-cl-xwin.cmake -DNRSC_BUILD_DLL=ON -DNRSC_BUILD_TESTS=OFF`. Exports `modengine_ext_init`, `me3_load_early`; imports only KERNEL32/USER32. Test package in `dist/` (see dist/TESTING.md). **Runs in game**; a two-player expedition with the original nrsc.dll on the other side worked (2026-10-15) | – |
| Devirtualized functions | **5 of 7 return in emulation**, all 7 identified (curl easy init/perform, curl write callback, a second hook installer, the startup thread, a server_manager task); see [docs/devirt.md](docs/devirt.md) | – |
| `server_manager` (`nrsc\mod\server_manager\server_manager.cpp`) | **done for readable code**: Steam interface subset, 7-byte wire header, lobby tagging/enter/chat-update, peer pool, message bus (poll, send flags, control message, missing-handler fatal), leaderboard state machine and tiers, partial per-frame task timer, startup wiring (handler ids 0–3, the 8 Steam callbacks, peer pool 5) from the real-image emulation; see [docs/server_manager.md](docs/server_manager.md) | 39 |

## Build

```sh
./build.sh          # check, host build, 341 tests, Windows nrsc.dll (clang-cl + xwin), package to out/package
./build.sh --help   # options; NRSC_GAME_EXE=<nightreign.exe> enables the real-binary tests
```

Details, dependencies and troubleshooting: [docs/BUILD.md](docs/BUILD.md). The in-game test install `dist/` is never written by the build.

## Layout

| Path | Contents |
|---|---|
| `evidence/` | `overview.json`, `redirected_functions.json` (7 protected stubs), `source_paths.txt`, `strings_rdata.txt`, `sha256.txt`, `pseudocode/` (incl. the hook installer `0x18001edb0.c`) |
| `tools/` | `pe_baseline.py` (PE baseline), `func_of.py` (address → .pdata function), `decode_sse_strings.py` (static decoder for XOR/SSE stack strings) |
| `build.sh`, `build.ps1` | one-command build (Linux reference; Windows/MSVC) |
| `src/nrsc/core/` | `FatalError`, `raw_memory` (unaligned field access), `version` |
| `src/nrsc/config/` | `ini_parser`, `ini_reader`, `settings` |
| `src/nrsc/locale/` | `message_repository` (loader, lookup, override) |
| `src/nrsc/cs/` | `world_chr_man`, `bloodstain_man`, `auto_control_api` |
| `src/nrsc/dantelion/` | `memory` (engine memory block resize) |
| `src/nrsc/params/` | `param_file` (row lookup), `regulation_manager` (scaling rows) |
| `tests/` | GoogleTest; `tests/reference/` holds copies of the shipped `nrsc_settings.ini` and `locale/english.json` with `SHA256SUMS` |
| `docs/baseline.md` | binary identity, module map, settings findings, ersc/nrsc comparison |
| `docs/localisation.md` | localisation findings, decoded messages, what is not recoverable |
| `docs/param_repository.md` | param lookup and multiplayer scaling findings |
| `docs/world_chr_man.md` | world_chr_man, bloodstain_man, auto_control_api detours |
| `docs/dantelion_memory.md` | memory block resize |
| `src/nrsc/server/` | Steam lobby, P2P message bus, leaderboard, task timer |
| `src/nrsc/signatures/` | Pattern16 parser, scanner, resolver, RTTI lookup, generated `signature_table.inc` |
| `src/nrsc/hooks/` | installer, `hook_filter` (disable list, held-back hooks), detour groups `world/`, `params/`, `net/` |
| `src/nrsc/session/`, `src/nrsc/platform/` | session layer; Win32/POSIX code memory, MinHook back end, Steam adapter |
| `src/nrsc/dll/` | exports, init thread, log, fatal box, crash log, session driver |
| `cmake/` | per-module fragments selected by `NRSC_MODULES` (`nrsc_add_module()`), MinHook, DLL target, toolchain |
| `tools/resolve_signatures.py`, `tools/gen_signature_table.py` | resolve the table against `nightreign.exe`; regenerate `signature_table.inc` (`--check`) |
| `tools/linux/` | memory-only runtime collector for the game under Wine/Proton, and its self-test |
| `devirt/` | Unicorn harness (`tools/boundary2.py` is current), results in `out/v2/` |
| `docs/hooks.md`, `docs/signatures.md`, `docs/server_manager.md`, `docs/devirt.md`, `docs/runtime_linux.md` | gap-closing results |
