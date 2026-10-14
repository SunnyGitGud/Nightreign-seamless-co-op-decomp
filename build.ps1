<#
.SYNOPSIS
  One-command build for the nrsc.dll reconstruction on a Windows host (docs/BUILD.md).

.DESCRIPTION
  Native MSVC build: configures, builds nrsc_core + nrsc.dll + nrsc_tests, runs the tests and packages into out\package.
  Run it from a "Developer PowerShell for VS 2022" (or any shell where cl.exe is on PATH) so the x64 MSVC toolchain is used.
  nlohmann_json (>= 3.11) and GTest come from vcpkg when VCPKG_ROOT is set (vcpkg install nlohmann-json gtest --triplet
  x64-windows-static), otherwise from CMAKE_PREFIX_PATH.

  The reference build is ./build.sh on Linux (clang-cl + xwin). This script has not been run on real Windows yet:
  see docs/TODO.md.

.EXAMPLE
  .\build.ps1            # build + test + package
  .\build.ps1 -Clean     # fresh build
  .\build.ps1 -NoTests   # faster development build
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$NoTests
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Out = Join-Path $Root 'out'
$Build = Join-Path $Out 'build\msvc'

function Step([string]$Message) { Write-Host "`n==> $Message" }
function Fail([string]$Message) { Write-Host "build.ps1: error: $Message" -ForegroundColor Red; exit 1 }
function Invoke-Checked([string]$What, [scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { Fail "$What failed (exit code $LASTEXITCODE)" }
}

if ($Clean -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out }

# ---- 1. toolchain ---------------------------------------------------------------------------------------------------
Step 'Checking toolchain'
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail 'cmake not found (need >= 3.20)' }
$cmakeVersion = [version]((cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
if ($cmakeVersion -lt [version]'3.20') { Fail "cmake $cmakeVersion is too old (need >= 3.20)" }
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Fail 'cl.exe not on PATH: run from "Developer PowerShell for VS 2022" (x64)'
}
$python = Get-Command python3, python, py -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $python) { Fail 'Python 3 not found (needed for the signature-table check)' }
$generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { @('-G', 'Ninja') } else { @() }
$toolchain = @()
if ($env:VCPKG_ROOT) {
    $toolchain = @("-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake",
                   '-DVCPKG_TARGET_TRIPLET=x64-windows-static')
}
Write-Host "cmake $cmakeVersion, generator: $(if ($generator) { 'Ninja' } else { 'default' }), vcpkg: $([bool]$env:VCPKG_ROOT)"

# ---- 2. static checks -----------------------------------------------------------------------------------------------
Step 'Checking that the generated signature table matches evidence\signatures\signatures.json'
Invoke-Checked 'signature-table check' { & $python.Source -I (Join-Path $Root 'tools\gen_signature_table.py') --check }

# ---- 3. build -------------------------------------------------------------------------------------------------------
Step "Configuring (out\build\msvc)"
Invoke-Checked 'configure' {
    cmake -S $Root -B $Build @generator @toolchain -DNRSC_BUILD_DLL=ON -DNRSC_BUILD_TESTS=ON -DNRSC_WARNINGS_AS_ERRORS=ON
}
Step 'Building'
Invoke-Checked 'build' { cmake --build $Build --config Debug --parallel }

# ---- 4. tests -------------------------------------------------------------------------------------------------------
if (-not $NoTests) {
    Step 'Running tests'
    if (-not $env:NRSC_GAME_EXE) { Write-Host 'note: NRSC_GAME_EXE unset, the 5 real-binary tests will be skipped' }
    Invoke-Checked 'tests' { ctest --test-dir $Build -C Debug --output-on-failure --parallel }
} else {
    Write-Host 'note: tests skipped (-NoTests)'
}

# ---- 5. package -----------------------------------------------------------------------------------------------------
Step 'Packaging (out\package)'
$dll = Get-ChildItem -Path $Build -Recurse -Filter 'nrsc.dll' | Select-Object -First 1
if (-not $dll) { Fail "build reported success but nrsc.dll is missing under $Build" }
$pkg = Join-Path $Out 'package\SeamlessCoop'
if (Test-Path (Join-Path $Out 'package')) { Remove-Item -Recurse -Force (Join-Path $Out 'package') }
New-Item -ItemType Directory -Force -Path (Join-Path $pkg 'locale') | Out-Null
Copy-Item $dll.FullName (Join-Path $pkg 'nrsc.dll')
Copy-Item (Join-Path $Root 'dist\SeamlessCoop\nrsc_settings.ini'), (Join-Path $Root 'dist\SeamlessCoop\nrsc_disable_hooks.txt') $pkg
Copy-Item (Join-Path $Root 'dist\SeamlessCoop\locale\*.json') (Join-Path $pkg 'locale')
$map = Join-Path $Build 'nrsc.map'
if (Test-Path $map) { Copy-Item $map (Join-Path $Out 'package') }
$hash = (Get-FileHash (Join-Path $pkg 'nrsc.dll') -Algorithm SHA256).Hash.ToLower()
"$hash  SeamlessCoop/nrsc.dll" | Set-Content -Encoding ascii (Join-Path $Out 'package\SHA256SUMS')
Write-Host "package: $Out\package ($($hash.Substring(0, 16))...)"
Write-Host 'note: dist\ (the in-game test install) is not touched'

Step 'Done'
