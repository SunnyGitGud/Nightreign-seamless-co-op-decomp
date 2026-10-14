# Architecture

`nrsc.dll` is a ModEngine/ME3 extension DLL loaded into `nightreign.exe`. It patches the game, through detours and byte
patches, to raise the party size and to add a Steam-lobby based co-op session layer. This repository rebuilds it as a
portable C++20 library (`nrsc_core`, tested on Linux) plus a thin Windows shell (`nrsc.dll`).

## Layers and dependency direction

```
            ┌──────────────────────── dll/  (bootstrap: exports, init thread, log, fatal box, crash log, session driver)
            │                                      Windows-only; the only code that knows about process start-up
            ▼
  platform/ (Win32 / POSIX code memory, MinHook back end, Steam flat-API adapter)      ── infrastructure
            │ implements interfaces declared by ▼
  hooks/    (installer, hook_filter, world | params | net detour groups + their state)  ── game integration
  session/  (join flow, lobby lifecycle, frame task, party)                            ── services
            │ use ▼
  server/   (wire header, peers, lobby, message bus, leaderboard, task timer, callbacks) ── domain: co-op protocol
  signatures/ (Pattern16 parser, scanner, resolver, generated 168-entry table)         ── domain: game lookup
  params/ cs/ dantelion/ locale/  (game-structure logic: param rows, scaling, chr sets, memory blocks, messages)
  config/   (inih parser, reader, Settings: the 5 nrsc_settings.ini keys)              ── configuration
            │ use ▼
  core/     (FatalError, raw_memory load/store, version)                              ── shared kernel
```

Measured from the `#include` graph (no cycles):

| Module | Depends on |
|---|---|
| core | – |
| config, cs, params, server, signatures | core (+ config for params) |
| dantelion | core, signatures |
| hooks | core, config, cs, locale, params, server, signatures |
| session | hooks, server |
| platform | hooks (the `CodeMemory` / `InlineBackend` interfaces), server, session (Steam interfaces) |
| dll | everything it wires together |

Rules:

- **Only `platform/` and `dll/` contain OS-specific code.** Everything else builds and is tested on Linux. Game memory is
  reached through `CodeMemory`, `raw::load/store` and function pointers that `wire()` resolves from the signature table.
- **Interfaces are declared on the consumer side** (`hooks/hook.hpp` declares `platform::CodeMemory`; `server/steam_iface.hpp`
  and `session/steam_session_iface.hpp` declare the Steam subset). Tests substitute fakes for them.
- **Behaviour comments carry evidence.** Grades C/S/W/U and original addresses (`FUN_1800xxxxx`, `0x1800…`) point to
  `docs/` and `evidence/`. Keep them when editing.

## Module responsibilities

| Path | Responsibility | Docs |
|---|---|---|
| `src/nrsc/core/` | `FatalError` (message + the original's source label + line), unaligned field access, the version constants | – |
| `src/nrsc/config/` | `nrsc_settings.ini`: inih-compatible parser, reader, `Settings` with validation and fatal paths | baseline.md |
| `src/nrsc/locale/` | `MessageRepository`: locale JSON loading, lookup, override, UTF-16 | localisation.md |
| `src/nrsc/params/` | param-file row lookup; `RegulationManager` multiplayer scaling rows | param_repository.md |
| `src/nrsc/cs/` | `WorldChrMan` chr-set allocation (6 players), bloodstain block, `SignIn` auto-call | world_chr_man.md |
| `src/nrsc/dantelion/` | engine memory-block resize (NETWORK → 30 MiB) and the block-table locator | dantelion_memory.md, wiring_net.md |
| `src/nrsc/signatures/` | pattern parsing and scanning, resolve modes, RTTI vtables. `signature_table.inc` is **generated** by `tools/gen_signature_table.py` | signatures.md |
| `src/nrsc/hooks/` | `Installer` (relay arena, E8/E9 retarget, vtable swap, byte patch, inline via back end); `hook_filter` (diagnostic disable list, held-back hooks); three detour groups, each = specs builder + `wire()` + state singleton | hooks.md §9, wiring_*.md |
| `src/nrsc/server/` | readable `server_manager` code: 7-byte wire header, peer pool, lobby callbacks, P2P message bus, leaderboard, task timer, startup callbacks | server_manager.md |
| `src/nrsc/session/` | the session layer driven once per frame: join flow (LobbyDataUpdate), lobby lifecycle, frame task (limiters + handshake), party word | session.md |
| `src/nrsc/platform/` | Win32 / POSIX `CodeMemory`, `MinHookBackend`, Steam flat-API adapter + callback decode | steam_adapter.md |
| `src/nrsc/dll/` | exports, init thread, log file, fatal box, live module snapshot, vectored-exception crash logger, `SteamAPI_RunCallbacks` session driver | this file |
| `tests/` | GoogleTest, mirroring `src/`. `tests/reference/` holds copies of the shipped ini and locale with `SHA256SUMS` | – |
| `cmake/` | per-module fragments (selected by `NRSC_MODULES`), `nrsc_add_module()`, MinHook, DLL target, clang-cl/xwin toolchain | BUILD.md |
| `tools/`, `devirt/`, `phase3/` | offline analysis tools (Python, Unicorn emulation). Not part of the build | devirt.md, runtime_linux.md |
| `evidence/` | read-only evidence the code cites. `evidence/signatures/signatures.json` is the source of the generated table | – |
| `dist/` | the in-game test install that the ME3 profile points at. The build never writes it | dist/TESTING.md |

## Start-up sequence (order is behaviour)

1. ME3 reads `me3_load_early = 1` and calls `modengine_ext_init(connector, &ext)` (`dll/exports.cpp`). That call blocks
   until the init thread is ready.
2. The init thread (`dll/init.cpp::run_init`) runs:
   1. Log directory, then the crash logger.
   2. `nrsc_settings.ini`.
   3. A snapshot of the live `nightreign.exe` image, then `SignatureTable::build` (logs `signature table built: N lookups`).
   4. The NETWORK memory-block resize.
   5. For each group world → params → net:
      1. `build`.
      2. `wire()`.
      3. `filter_hooks` (disabled, then held back).
      4. The world `on_fatal` handler.
      5. `install` each spec (`installed <name>`).
   6. The auto-host switch, then the session driver hook on `SteamAPI_RunCallbacks`.
   7. `enable_inline()`.
3. From then on everything runs on the game thread. Detours run when the game calls them. The session layer ticks after
   each `SteamAPI_RunCallbacks`, so callbacks, ticks and detours share one thread.

## Error handling

Library code throws `nrsc::FatalError(message, original_source_file, line)`. That mirrors the original's fatal routine
`FUN_180029f40`, which is virtualized. `dll/fatal_box.cpp` turns it into the original message box ("Seamless Coop 1.1.3 -
Fatal Error"), logs `FATAL: ...` and terminates the process. Detours must not let exceptions unwind through game frames, so
world detours report through `world::state().on_fatal`. The DLL has no shutdown path: like the original, it stays loaded for
the life of the process.

## Extending

- A new module: a `cmake/<module>.cmake` that calls `nrsc_add_module(SOURCES … TESTS …)`, added to `NRSC_MODULES`.
- A new hook: add a `HookSpec` to the group builder. Its **name prefix** is how `nrsc_disable_hooks.txt` and
  `hook_filter` find it. Wire any game function it needs in that group's `wire()`.
- A new signature: edit `evidence/signatures/signatures.json`, then run `python3 -I tools/gen_signature_table.py`.
