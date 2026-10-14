# Building and testing

## One command

```sh
./build.sh            # Linux: check -> host build -> tests -> Windows DLL (if toolchain present) -> out/package
.\build.ps1           # Windows host, native MSVC (not yet run on real Windows; see TODO.md #12)
```

Options for `build.sh`:

| Option | Effect |
|---|---|
| `--clean` | delete `out/` first |
| `--dll` | fail if the DLL toolchain is missing. Without it, the DLL step is skipped with a note |
| `--no-dll` | host build and tests only |
| `--no-tests` | skip `ctest` (faster inner loop; never for a build that goes into the game) |

| Environment | Effect |
|---|---|
| `NRSC_GAME_EXE=<nightreign.exe v1.03.3>` | enables the 5 real-binary tests (signature resolution and hook sites against the real exe) |
| `XWIN_DIR` | xwin SDK/CRT root (default `~/.xwin`) |
| `JOBS` | build/test parallelism |

The script returns non-zero on the first failing step and prints which step failed.

## What it does

1. **Toolchain check.** CMake ≥ 3.20, a C++20 host compiler and Python 3. Ninja is used if present. For the DLL it checks `clang-cl`, `lld-link`, `llvm-lib` and an xwin splat.
2. **Static check.** `tools/gen_signature_table.py --check`: the generated `signature_table.inc` must match `evidence/signatures/signatures.json`.
3. **Host build** (`out/build/host`, warnings as errors), then **ctest**. Currently 341 tests.
4. **Windows DLL** (`out/build/win`), cross-compiled with clang-cl + lld-link against xwin. It also checks that the exports `modengine_ext_init` and `me3_load_early` are present.
5. **Package.** `out/package/SeamlessCoop/` holds `nrsc.dll`, `nrsc_settings.ini`, `nrsc_disable_hooks.txt` and `locale/*.json`; `out/package/` also holds `nrsc.map` and `SHA256SUMS`. The template files come from `dist/SeamlessCoop/`.

`dist/` is the in-game test install that the ME3 profile points at, and the build **never writes to it**. To try a new build in
game, copy `out/package/SeamlessCoop/*` over `dist/SeamlessCoop/` and follow `dist/TESTING.md`.

## Dependencies

| Dependency | Version | Source |
|---|---|---|
| CMake | ≥ 3.20 | system |
| nlohmann_json | ≥ 3.11 | system package (`nlohmann-json`). The cross build reuses the host headers (`NRSC_NLOHMANN_INCLUDE_DIR` to override) |
| GoogleTest | any recent (tested 1.17) | system package (`gtest`) |
| MinHook | 1.3.4 (commit c3fcafd) | vendored, `third_party/minhook` |
| clang-cl / lld-link / llvm-lib | tested with LLVM 22 | system (`clang`, `lld`, `llvm`) |
| Windows SDK + CRT | via `xwin --accept-license splat --output ~/.xwin` | xwin |

Arch Linux: `pacman -S cmake ninja gcc clang lld llvm nlohmann-json gtest python` and `cargo install xwin`.

## Manual commands (what build.sh runs)

```sh
cmake -S . -B out/build/host -G Ninja -DNRSC_WARNINGS_AS_ERRORS=ON && cmake --build out/build/host && ctest --test-dir out/build/host
cmake -S . -B out/build/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clang-cl-xwin.cmake \
      -DNRSC_BUILD_DLL=ON -DNRSC_BUILD_TESTS=OFF && cmake --build out/build/win
```

`-DNRSC_MODULES="signatures;hooks"` builds a subset of the optional modules (see `cmake/*.cmake`). `session` needs `server` and `hooks_net`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Could NOT find nlohmann_json` / `GTest` | install the system packages above, or pass `-DCMAKE_PREFIX_PATH=...` |
| `nlohmann/json.hpp not found` (DLL build) | install nlohmann-json on the host, or `-DNRSC_NLOHMANN_INCLUDE_DIR=<dir containing nlohmann/>` |
| `signature_table.inc is stale` | `python3 -I tools/gen_signature_table.py` and review the diff |
| DLL step skipped | install the LLVM tools and the xwin splat, or pass `--no-dll` to silence the note |
| Real-binary tests skipped | set `NRSC_GAME_EXE` to the v1.03.3 `nightreign.exe` |
| In game: fatal box "Seamless Coop 1.1.3 - Fatal Error" | read `dist/SeamlessCoop/nrsc_reconstruction.log` (last `FATAL:` line); bisect hooks with `nrsc_disable_hooks.txt` |
