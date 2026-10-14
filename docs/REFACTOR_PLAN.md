# Refactor plan (audit of 2026-10-15)

This is a behaviour-preserving refactor of the `nrsc.dll` reconstruction. The baseline is git commit `0693c1b`. It is the
source of truth, and every step below was checked against it.

## 1. What the project is

| | |
|---|---|
| Language | C++20 (sources), C (vendored MinHook), Python 3 (offline tools) |
| Build | CMake ≥ 3.20 + Ninja. Two configurations: **host** (Linux GCC; library + GoogleTest) and **Windows DLL** (clang-cl + lld-link cross-compile against the xwin SDK/CRT, `cmake/toolchain-clang-cl-xwin.cmake`) |
| Dependencies | nlohmann_json ≥ 3.11 (host package; header-only), GoogleTest (host package), MinHook 1.3.4 (vendored, `third_party/minhook`) |
| Output | `nrsc.dll`: exports `modengine_ext_init` and `me3_load_early`, imports only KERNEL32 and USER32. ME3 loads it into `nightreign.exe` |

The code is not raw decompiler output. It is a hand-written reconstruction whose behaviour is graded against evidence (C/S/W/U)
with original addresses cited in comments. `FUN_1800xxxxx`-style tokens appear **only as evidence citations**. They are kept on
purpose: they are the traceability link to `docs/` and `evidence/`.

## 2. Baseline (before any change)

| Check | Result |
|---|---|
| Host configure + build (GCC 16.2, `-Wall -Wextra -Wpedantic`) | OK, 0 warnings |
| `ctest` | **332/332 pass**; 5 real-binary tests skip without `NRSC_GAME_EXE` |
| `ctest` with `NRSC_GAME_EXE=<nightreign.exe v1.03.3>` | 332/332 pass, none skipped |
| Windows DLL (clang-cl 22.1, `/W4`) | OK, 0 warnings, 2.1 MB; exports and imports as above |
| Build command | not one command: README gives two separate cmake lines, and there are 13 hand-made build trees (`build`, `build-wp6`..`build-win-steam`) |

## 3. Execution flow

1. ME3 loads `nrsc.dll`, sees `me3_load_early = 1`, and calls `modengine_ext_init(connector, &ext)` (`dll/exports.cpp`).
2. `start_and_wait()` starts the init thread and blocks until it sets the ready flag (`dll/init.cpp`).
3. The init thread runs these steps in this order. **The order is behaviour.**
   1. Crash logger.
   2. `nrsc_settings.ini` (`config/`).
   3. A copy of the live game image.
   4. `SignatureTable::build` (`signatures/`).
   5. NETWORK memory-block resize (`dantelion/`).
   6. For each group world → params → net:
      1. Build the specs.
      2. Run that group's `wire()`.
      3. Drop the hooks named in `nrsc_disable_hooks.txt`.
      4. Drop the held-back hooks (#7, #10, P12).
      5. Set the world `on_fatal` handler.
      6. Install.
   7. Auto-host switch.
   8. Session driver hook on `SteamAPI_RunCallbacks`.
   9. `enable_inline()`.
4. At run time, detours (`hooks/world|params|net`) read their state singletons. The session layer (`session/`, `server/`,
   `platform/steam/`) runs on the game thread from the `SteamAPI_RunCallbacks` hook.
5. Errors: modules throw `nrsc::FatalError(message, original_source_file, line)`. The DLL turns it into the original's message box
   and terminates the process. Inside detours `world::state().on_fatal` reports in place, so nothing unwinds through game frames.
6. Logging: `nrsc_reconstruction.log` next to the DLL (Windows only). The DLL has no shutdown path. Like the original, it stays
   loaded for the life of the process.

## 4. Findings

| # | Finding | Kind | Action |
|---|---|---|---|
| F1 | `load<T>`/`store<T>` unaligned-access helpers are defined separately in `hooks/world/game_layout.hpp`, `hooks/net/net_group.cpp`, `hooks/params/params_group.cpp`, `params/param_file.cpp`, `params/regulation_manager.cpp` and `cs/world_chr_man.cpp`, all with the same `memcpy` semantics | duplication | one `core/raw_memory.hpp`; the old names stay as using-declarations so call sites don't churn |
| F2 | `dll/init.cpp::run_init` (~100 lines) mixes logging, the fatal box, PE image copying, parsing of the diagnostic file, the held-back policy and group wiring. Group dispatch compares function pointers (`if (build == &groups::world)`) | long function, unclear control flow | split into `dll/log`, `dll/fatal_box`, `dll/live_image` and a table of `{build, wire}` groups. Move the hook filtering policy into portable, unit-tested `hooks/hook_filter` |
| F3 | The mod version `"1.1.3"` is written out in `server/lobby.hpp`, the fatal-box title in `dll/init.cpp` and the banner text | magic string | `core/version.hpp`; the wire constant `server::kModVersion` stays (same value) |
| F4 | MinHook is defined in two CMake fragments (`hooks.cmake` with `hde32.c`, `dll.cmake` without, guarded by `if(NOT TARGET)`). Every fragment repeats `${CMAKE_CURRENT_SOURCE_DIR}` and the `if(TARGET nrsc_tests)` boilerplate | duplication | `cmake/minhook.cmake` and an `nrsc_add_module()` helper; source lists unchanged |
| F5 | The cross build symlinks `/usr/include/nlohmann`: a hard-coded host path | machine-specific build | `find_path(nlohmann/json.hpp)` on the host, overridable with `NRSC_NLOHMANN_INCLUDE_DIR` |
| F6 | No single build command, toolchain check or packaging step. `dist/` is packaged by hand and also holds the user's test logs | missing build entry point | `build.sh` / `build.ps1`. Packaging goes to `out/package/` so it never overwrites the tested `dist/` |
| F7 | No VCS and no `.gitignore`. 13 build trees, a 16 GB `evidence/runtime/`, a Steam-config backup (personal data) and `__pycache__` sit next to the sources | hygiene | git + `.gitignore` (done in the baseline commit) |
| F8 | README is stale (it says hooks are "not rebuilt as code") and has no architecture doc | docs | update README; add `docs/ARCHITECTURE.md` and `docs/TODO.md` |
| F9 | Python tools and `devirt/tools` default to absolute paths of the analysis machine | machine-specific | **left as a TODO** (see `docs/TODO.md`). They are offline tools, not part of the build |
| F10 | Known functional gaps: P12 crash, #7/#10 held back, the host role untested, and the rest of `docs/plan_gaps.md` | open problems | **left as TODOs** (per the user's instruction). Documented in `docs/TODO.md` and marked `TODO(known-issue)` at the code sites. Behaviour is unchanged |

## 5. Behaviour-sensitive interfaces (must not change)

- DLL exports `modengine_ext_init` (C ABI, `bool(void*, void**)`) and `me3_load_early` (`const u64 = 1`); the import set
  KERNEL32 + USER32 only.
- Hook specs: names (`"hook#7 …"`, `"P12 …"`), because the diagnostic file and the held-back policy match name prefixes; sites,
  kinds, `expect` bytes, and installation order.
- Game-structure offsets in `*/layout` and `*_state.hpp`; detour signatures (calling conventions follow the game's x64 ABI).
- The signature table `signature_table.inc` (generated; checked by `tools/gen_signature_table.py --check`) and its execution order.
- Wire format: 7-byte header, lobby tags, the version string `"1.1.3"`, and channel numbers (`server/`, `session/`).
- File names and formats: `nrsc_settings.ini` keys and defaults, `locale/*.json`, `nrsc_disable_hooks.txt` (prefix per line;
  `#` comments; `no-auto-host`), and `nrsc_reconstruction.log` line texts (the user greps them; see `dist/TESTING.md`).
- The original source-file labels and line numbers carried by `FatalError` (shown in the fatal box).

## 6. Target architecture

See [ARCHITECTURE.md](ARCHITECTURE.md). The directory layout is unchanged, because it already follows module boundaries. The
changes are inside the bootstrap (`dll/`), shared core helpers (`core/`) and the build.

## 7. Phased migration and how each phase was checked

| Phase | Change | Check |
|---|---|---|
| 0 | git baseline and `.gitignore` | `git status` clean; nothing over 5 MB tracked |
| 1 | CMake: `minhook.cmake`, `nrsc_add_module()`, nlohmann lookup, `NRSC_WARNINGS_AS_ERRORS` | the same 332 tests are discovered and pass; the DLL builds with the same exports and imports |
| 2 | `core/raw_memory.hpp` replaces the 6 copies (F1) | full test suite; new unit tests for the helpers |
| 3 | `core/version.hpp` (F3) | tests; string literals in the DLL unchanged |
| 4 | Split the DLL bootstrap and extract `hooks/hook_filter` (F2), with no change to log texts or step order | new `hook_filter` tests; DLL build; side-by-side read of the init sequence |
| 5 | `build.sh` / `build.ps1` (F6) | fresh-clone build in a temp dir; failure paths return non-zero |
| 6 | Docs (F8), `TODO.md` (F9, F10) | – |

## 8. Risks

- **No in-game regression test.** Detours and init can only be checked by unit tests on fake structures and by the DLL
  building. Anything that changes the DLL's behaviour at run time needs a new in-game test. That is why phase 4 keeps the log
  texts, the step order and the hook order exactly as they were, and needs a re-test in game before the new DLL replaces
  `dist/`.
- Unaligned `memcpy` access in hooks is deliberate (game structures are packed by offset). The shared helper keeps `memcpy`.
- `world::state().on_fatal` must be set after `groups::world()` resets the world state and before any world hook is live.
- MinHook relocation of the #12 / P2 prologues is still untested in game. Nothing here touches it.

## 9. Status (2026-10-15)

| Phase | Result |
|---|---|
| 0 | done: git baseline and `.gitignore` (later narrowed to an allowlist of 194 files: build, tests, docs, MinHook sources, the signature generator and its input, the package template) |
| 1 | done: DLL `.text` **byte-identical** to the baseline (built in a build directory whose name has the same length; the build is deterministic). Same 332 tests |
| 2 | done: 6 helper copies → `core/raw_memory.hpp`, +4 tests |
| 3–4 | done: `core/version.hpp`; `dll/` split into `init`, `log`, `fatal_box`, `live_image`; `hooks/hook_filter` with +5 tests that pin the log texts and order. Exports and imports unchanged, and every baseline log/box string still present. **Needs an in-game re-test before the new DLL replaces `dist/`** |
| 5 | done: `build.sh` (verified from a fresh clone) and `build.ps1` (syntax-checked only) |
| 6 | done: README, ARCHITECTURE.md, BUILD.md, TODO.md |

Not done, on purpose: renaming. Identifiers were already descriptive, and the `FUN_…` tokens are evidence citations. The
known functional problems stay as they were (TODO.md).
