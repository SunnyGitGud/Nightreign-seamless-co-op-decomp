#!/usr/bin/env bash
# One-command build for the nrsc.dll reconstruction (docs/BUILD.md).
#
#   ./build.sh               host build + tests, then the Windows DLL if the clang-cl/xwin toolchain is present, then package
#   ./build.sh --dll         same, but fail if the DLL toolchain is missing
#   ./build.sh --no-dll      host build + tests only
#   ./build.sh --clean       delete out/ first (fresh build)
#   ./build.sh --no-tests    skip ctest (faster development build; never use for a release)
#
# Environment: NRSC_GAME_EXE=<nightreign.exe v1.03.3> enables the 5 real-binary tests; XWIN_DIR (default ~/.xwin) is the
# xwin SDK/CRT; JOBS overrides the parallelism. Outputs: out/build/{host,win}, out/package/SeamlessCoop.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$ROOT/out"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
DLL_MODE=auto   # auto | require | skip
RUN_TESTS=1

for arg in "$@"; do
  case "$arg" in
    --dll) DLL_MODE=require ;;
    --no-dll) DLL_MODE=skip ;;
    --no-tests) RUN_TESTS=0 ;;
    --clean) rm -rf "$OUT" ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build.sh: unknown option '$arg' (see --help)" >&2; exit 2 ;;
  esac
done

step() { printf '\n==> %s\n' "$*"; }
die() { printf 'build.sh: error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

version_ge() {  # version_ge <have> <need>
  [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

# ---- 1. toolchain ---------------------------------------------------------------------------------------------------
step "Checking toolchain"
have cmake || die "cmake not found (need >= 3.20)"
CMAKE_VERSION="$(cmake --version | head -n1 | awk '{print $3}')"
version_ge "$CMAKE_VERSION" 3.20 || die "cmake $CMAKE_VERSION is too old (need >= 3.20)"
GENERATOR=()
if have ninja; then GENERATOR=(-G Ninja); fi
have c++ || have g++ || have clang++ || die "no host C++20 compiler (c++/g++/clang++) found"
have python3 || die "python3 not found (needed for the signature-table check)"
echo "cmake $CMAKE_VERSION, generator: ${GENERATOR[*]:-default}, jobs: $JOBS"

XWIN_DIR="${XWIN_DIR:-$HOME/.xwin}"
BUILD_DLL=0
if [ "$DLL_MODE" != skip ]; then
  missing=()
  for tool in clang-cl lld-link llvm-lib; do have "$tool" || missing+=("$tool"); done
  [ -d "$XWIN_DIR/crt" ] && [ -d "$XWIN_DIR/sdk" ] || missing+=("xwin SDK/CRT in $XWIN_DIR (run: xwin --accept-license splat --output $XWIN_DIR)")
  if [ ${#missing[@]} -eq 0 ]; then
    BUILD_DLL=1
  elif [ "$DLL_MODE" = require ]; then
    die "the Windows DLL toolchain is incomplete; missing: ${missing[*]}"
  else
    echo "note: skipping the Windows DLL; missing: ${missing[*]}"
  fi
fi

# ---- 2. static checks -----------------------------------------------------------------------------------------------
step "Checking that the generated signature table matches evidence/signatures/signatures.json"
python3 -I "$ROOT/tools/gen_signature_table.py" --check \
  || die "src/nrsc/signatures/signature_table.inc is stale: run python3 -I tools/gen_signature_table.py"

# ---- 3. host build + tests ------------------------------------------------------------------------------------------
step "Configuring host build (out/build/host)"
cmake -S "$ROOT" -B "$OUT/build/host" "${GENERATOR[@]}" -DNRSC_WARNINGS_AS_ERRORS=ON \
  || die "host configure failed (missing nlohmann_json >= 3.11 or GTest? see docs/BUILD.md)"
step "Building host library and tests"
cmake --build "$OUT/build/host" -j "$JOBS" || die "host build failed"
if [ "$RUN_TESTS" = 1 ]; then
  step "Running tests"
  [ -n "${NRSC_GAME_EXE:-}" ] || echo "note: NRSC_GAME_EXE unset, the 5 real-binary tests will be skipped"
  ctest --test-dir "$OUT/build/host" -j "$JOBS" --output-on-failure || die "tests failed"
else
  echo "note: tests skipped (--no-tests)"
fi

# ---- 4. Windows DLL -------------------------------------------------------------------------------------------------
if [ "$BUILD_DLL" = 1 ]; then
  step "Configuring Windows DLL cross build (out/build/win)"
  # No CMAKE_BUILD_TYPE: the tested DLL in dist/ was built with CMake's default (unoptimised) flags; keep them so the
  # packaged DLL matches what was tried in game (docs/TODO.md).
  XWIN_DIR="$XWIN_DIR" cmake -S "$ROOT" -B "$OUT/build/win" "${GENERATOR[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-clang-cl-xwin.cmake" -DXWIN_DIR="$XWIN_DIR" \
    -DNRSC_BUILD_DLL=ON -DNRSC_BUILD_TESTS=OFF -DNRSC_WARNINGS_AS_ERRORS=ON || die "DLL configure failed"
  step "Building nrsc.dll"
  cmake --build "$OUT/build/win" -j "$JOBS" || die "DLL build failed"
  DLL="$OUT/build/win/nrsc.dll"
  [ -f "$DLL" ] || die "build reported success but $DLL is missing"
  if have llvm-objdump; then
    exports="$(llvm-objdump -p "$DLL" | sed -n '/Export Table/,/^$/p')"
    for sym in modengine_ext_init me3_load_early; do
      grep -q " $sym\$" <<<"$exports" || die "nrsc.dll does not export $sym"
    done
    echo "exports OK: modengine_ext_init, me3_load_early"
  fi

  # ---- 5. package -------------------------------------------------------------------------------------------------
  step "Packaging (out/package)"
  PKG="$OUT/package/SeamlessCoop"
  rm -rf "$OUT/package"
  mkdir -p "$PKG/locale"
  cp "$DLL" "$PKG/nrsc.dll"
  cp "$ROOT/dist/SeamlessCoop/nrsc_settings.ini" "$ROOT/dist/SeamlessCoop/nrsc_disable_hooks.txt" "$PKG/"
  cp "$ROOT"/dist/SeamlessCoop/locale/*.json "$PKG/locale/"
  [ -f "$OUT/build/win/nrsc.map" ] && cp "$OUT/build/win/nrsc.map" "$OUT/package/"
  (cd "$OUT/package" && sha256sum SeamlessCoop/nrsc.dll > SHA256SUMS)
  echo "package: $OUT/package ($(cut -c1-16 "$OUT/package/SHA256SUMS")…)"
  echo "note: dist/ (the in-game test install) is not touched; copy out/package/SeamlessCoop over it to test a new build"
fi

step "Done"
