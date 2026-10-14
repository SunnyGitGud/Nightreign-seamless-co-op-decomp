# Known problems and open work

These problems were known when the refactor ran (2026-10-15). They were **left as they were on purpose**: the refactor does
not change behaviour. In code, the sites are marked `TODO(known-issue)` (`grep -rn 'TODO(known-issue)' src`). Grades follow
the project convention: C = confirmed, S = strong, W = weak, U = unknown.

## Gameplay / in-game

| # | Problem | Current behaviour | Where | Evidence |
|---|---|---|---|---|
| 1 | **P12 (6-slot bloodstain block) crashes a new game.** Game code at `0x14072ad50` reads the block with the vanilla 0x68-byte layout (effect handle at +0x58). The original compensates somewhere that has not been recovered (likely P15 or other +0x58 readers) | P12 held back; vanilla 3-slot block | `hooks/hook_filter.cpp` `held_back_reason` | docs/plan_gaps.md "First in-game test" |
| 2 | **Enemy scaling #7 / #10**: the 80 target ids are filled by protected code and are not recovered | both held back; the game's own scaling runs | `hooks/hook_filter.cpp`; `ParamsState::effects` is null | docs/wiring_params.md |
| 3 | **No text override**: the text key table and handlers are not recovered | lookups fall through to the game | `hooks/params/params_group.hpp` `text_handlers`, `messages` | docs/localisation.md |
| 4 | **Lobby creation trigger** not recovered (W) | auto-hosts a lobby after 20 s (`no-auto-host` turns it off) | `dll/session_driver.cpp` `kAutoHostDelaySeconds` | docs/phase3_vm_networking.md §4 |
| 5 | **Roles not yet tested**: the rebuild as host, the rebuild on both sides, 3+ players. Only "rebuild = guest, original = host" was tried (it worked) | – | – | docs/plan_gaps.md "First two-player test" |
| 6 | **#12 and P2 are MinHook inline hooks**, not the original's entry jumps. Prologue relocation is untested in game | – | `hooks/net/net_group.cpp` | docs/plan_gaps.md phase 2 |
| 7 | **Solo expedition fails** ("Matchmaking failed"). The original fails the same way on the same install, so it is a limitation of the test install, not of the reconstruction | – | – | docs/plan_gaps.md |
| 8 | Open session-side unknowns (W/U): default timer rates, job-queue payloads, `X+0x140`, the party bit-5 setter, the meaning of event flags `0xa7f418` / `0x7f8`, the boss list, the auto-control task objects | vanilla defaults documented per member | `session/`, `hooks/*/` state headers | docs/plan_gaps.md, docs/hooks.md §8 |
| 9 | Two of the 7 devirtualized functions do not return in emulation | – | `devirt/` | docs/devirt.md §5 |

## Build and repository

| # | Problem | Notes |
|---|---|---|
| 10 | GCC `-O2` flags `-Wstringop-truncation` on `config/ini_parser.cpp` `strncpy0`. The truncation is deliberate: inih semantics, matching the original | `build.sh` keeps CMake's default host flags, as in the baseline, so `-Werror` holds. All 341 tests also pass at `-O2` |
| 11 | The DLL is built with CMake's default (unoptimised, `/Od /RTC1`) flags, as was the DLL tested in game | switching to an optimised build needs an in-game re-test |
| 12 | `build.ps1` (native MSVC on Windows) has been syntax-checked only, never run on Windows. The installer test is POSIX-only, so it does not run there | the reference build is `./build.sh` on Linux |
| 13 | Python tools default to machine paths: `tools/resolve_signatures.py` (`DEFAULT_EXE`), `devirt/tools/pe.py`, `realimage.py`, `realimage2.py` | offline tools, not part of the build. Pass paths explicitly or set `NRSC_GAME_EXE` |
| 14 | Runtime evidence (`evidence/runtime/`, 16 GB, includes SteamIDs) is not in git | keep it out, or redact it before publishing |
| 15 | Old hand-made build trees (`build/`, `build-wp*`, `build-w*`, `build-session`, `build-win*`) remain on disk | ignored by git; safe to delete once nobody needs them. `build-win/nrsc.map` matches the DLL in `dist/` |
| 16 | No `.clang-format` / clang-tidy gate. Adding one means reformatting the tree in a separate change | – |
