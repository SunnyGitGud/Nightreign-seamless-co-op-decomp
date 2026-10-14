# nrsc reconstruction

Readable, tested C++ source for **`nrsc.dll` 1.1.3**, the Seamless Co-op mod for *Elden Ring Nightreign*. It is rebuilt from
static analysis of the shipped DLL. The result is a working `nrsc.dll` that ME3 loads into `nightreign.exe` v1.03.3. It
raises the party size, installs the mod's 44 game hooks and runs the Steam-lobby co-op session layer.

**Status:** it builds and runs in game. A two-player expedition with the original DLL on the other side worked
(2026-10-15). Some features are held back or still missing; see [docs/TODO.md](docs/TODO.md).

## Build

```sh
./build.sh      # Linux: tests + Windows nrsc.dll (clang-cl + xwin) -> out/package/
```

Requirements and options are in [docs/BUILD.md](docs/BUILD.md). How to try a build in game: [dist/TESTING.md](dist/TESTING.md).

## Docs

| | |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | modules, dependency rules, start-up order |
| [STATUS.md](docs/STATUS.md) | per-module status, tests, evidence and layout |
| [TODO.md](docs/TODO.md) | known problems and open work |
| [hooks.md](docs/hooks.md), [session.md](docs/session.md), … | per-subsystem findings, with C/S/W/U evidence grades |
