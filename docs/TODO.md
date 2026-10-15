# Known problems and open work

These problems were known when the refactor ran (2026-10-15). They were **left as they were on purpose**: the refactor does
not change behaviour. In code, the sites are marked `TODO(known-issue)` (`grep -rn 'TODO(known-issue)' src`). Grades follow
the project convention: C = confirmed, S = strong, W = weak, U = unknown.

## Gameplay / in-game

| # | Problem | Current behaviour | Where | Evidence |
|---|---|---|---|---|
| 1 | **P12 (6-slot bloodstain block) crashes a new game.** Game code at `0x14072ad50` reads the block with the vanilla 0x68-byte layout (effect handle at +0x58). The original compensates somewhere that has not been recovered (likely P15 or other +0x58 readers) | P12 held back; vanilla 3-slot block | `hooks/hook_filter.cpp` `held_back_reason` | docs/plan_gaps.md "First in-game test" |
| 2 | ~~Enemy scaling #7 / #10 target ids~~ **recovered 2026-10-16**: 7700..7779 (devirt.md 2.11); both hooks are installed again. **Not yet tested in game** | – | `hooks/params/params_group.cpp` `scaling_target_ids` | docs/devirt.md §2.11 |
| 3 | ~~No text override~~ **recovered 2026-10-16**: handler map {200 -> override}, key table, fixed texts, english-then-Steam-language order (devirt.md 2.9, 2.11). **Not yet tested in game** | – | `hooks/params/params_group.cpp` | docs/devirt.md §2.11 |
| 4 | ~~Lobby creation trigger~~ **recovered 2026-10-16**: vtable hooks on the game's ISteamMatchmaking CreateLobby (Public -> FriendsOnly + create pending) and LeaveLobby; the 20 s auto-host is removed (`no-auto-host` now disables these hooks). **Not yet tested in game** | – | `dll/session_driver.cpp`, `session/session.cpp` | docs/devirt.md §2.11 |
| 5 | **Roles not yet tested**: the rebuild as host, the rebuild on both sides, 3+ players. Only "rebuild = guest, original = host" was tried (it worked) | – | – | docs/plan_gaps.md "First two-player test" |
| 6 | **#12 and P2 are MinHook inline hooks**, not the original's entry jumps. Prologue relocation is untested in game | – | `hooks/net/net_group.cpp` | docs/plan_gaps.md phase 2 |
| 7 | **Solo expedition fails** ("Matchmaking failed"). The original fails the same way on the same install, so it is a limitation of the test install, not of the reconstruction | – | – | docs/plan_gaps.md |
| 8 | Open session-side unknowns (W/U): (job queue: never created in v1.1.3, devirt 2.13; timer rates = 1.0 and `X+0x140` = 0x…E0C0 are C, 2.12) the party bit-5 setter, the meaning of event flags `0xa7f418` / `0x7f8`, the boss list, the auto-control task objects | vanilla defaults documented per member | `session/`, `hooks/*/` state headers | docs/plan_gaps.md, docs/hooks.md §8 |
| 9 | ~~Two of the 7 devirtualized functions do not return in emulation~~ **resolved 2026-10-16**: `0x180061140` returns every tick on the real state, `0x180040630` returns once its flag is cleared (no other code can clear it) | – | `devirt/tools/realimage3.py` | docs/devirt.md §2.10 |
| 17 | Party data sync (sub-channels 3/4 per member per tick) and the F4 co-op pause, **recovered 2026-10-16** (devirt.md 2.12) and wired (`session/player_sync`). **Not yet tested in game**; field meanings U | – | `session/player_sync.cpp`, `dll/session_driver.cpp` | docs/devirt.md §2.12 |

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
