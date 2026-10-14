# Runtime evidence on Linux (Lutris / GE-Proton / wine-staging)

Tool: `nrsc/tools/linux/collect_evidence.py` (Python 3 stdlib only). It is the Linux counterpart of
`reconstruction/tools/windows/collect_evidence.ps1`. Self-test: `python3 nrsc/tools/linux/selftest.py`
(starts a harmless child process with synthetic PE mappings; 12 checks, all pass).

Rules the tool follows: memory is only **read** (`/proc/<pid>/maps`, `pread` on `/proc/<pid>/mem`); no
ptrace attach or seize, no signals, no stop, no writes, no injection. It never launches the game, Wine or Lutris.
nrsc.dll's code is never read: of the mod image only the header page and the non-code sections are readable. The
reader blocks every other byte of that range, so heap walks, private-executable dumps, string scans and probes
cannot touch it even if Wine mapped a code section anonymously.

Caveat: the test install does not use the regular Steam client. Network, session, lobby and Steam-ID data may
differ from a real Steam install. The manifest says so. Hook, patch, param and world data are unaffected.

## 1. Permission to read another process's memory

Reading `/proc/<pid>/mem` is an ACCESS check (`PTRACE_MODE_ATTACH_REALCREDS`), not an attach: nothing is
traced and the game keeps running. The kernel allows it when:

| Situation | Needs |
|---|---|
| same uid, `kernel.yama.ptrace_scope = 0` | nothing else |
| `ptrace_scope = 1` (this machine's current value) | the target must be a descendant of the reader. A game started by Lutris is **not**, so this fails with `EACCES` |
| `ptrace_scope = 2` | `CAP_SYS_PTRACE` |
| `ptrace_scope = 3` | impossible until reboot |
| any scope | root / `CAP_SYS_PTRACE` always works |

The tool never changes sysctl. Pick one yourself:

- `sudo python3 nrsc/tools/linux/collect_evidence.py ...` (output is chowned back to `$SUDO_UID`), or
- temporarily `sudo sysctl kernel.yama.ptrace_scope=0`, run the collector as your user, and set it back to `1` afterwards.

If the read fails, the tool prints this explanation with the current `ptrace_scope`.

## 2. Launching the game

Lutris (GE-Proton or `wine-10.8-staging-tkg-ntsync`, both under `~/.local/share/lutris/runners/wine/`):

1. Run the game normally from Lutris, with a runner that loads nrsc.dll. nrsc.dll is an ME3 native
   (`me3_load_early` export, `modengine_ext_init`), so it is loaded by ME3, not by DLL search order.
2. **ME3 on Linux:** me3 (garyttierney/me3) documents a Linux install: a shell installer
   (`curl --proto '=https' --tlsv1.2 -sSfL https://github.com/garyttierney/me3/releases/latest/download/installer.sh | sh`),
   `me3` on `PATH` from `~/.local/bin`, profiles under `~/.config/me3/profiles`, config in `~/.config/me3/me3.toml`
   with a `windows_binaries_dir` setting that must point at the Windows binaries. (Source: me3 installation and
   troubleshooting docs in `reconstruction/tools/me3doc`; I did not install or run it. `me3` is currently not on `PATH` here.)
   Use `reconstruction/tools/windows/nightreign-capture.me3` as the template: only `nrsc.dll` with `load_early = true`,
   with the native `path` changed to the Wine form of the DLL, e.g. `Z:<game-dir>/SeamlessCoop/nrsc.dll`
   (or a Linux path if me3 accepts it; check `me3 info`). Launch with `me3 launch --profile ...` through the
   same Wine prefix, or set Lutris's executable/launch option to the me3 wrapper. This was **not** tested.
3. If the install instead loads the mod through a proxy DLL (winmm/dinput8 style), Wine needs a native override: Lutris -> Runner options -> DLL overrides, or
   `WINEDLLOVERRIDES="winmm=n,b"` (replace the name with whatever proxy DLL sits next to `nightreign.exe`). Check
   which one exists with `ls <game dir>` and the static phase's `game_binaries.csv`.
4. Windowed or borderless mode is recommended (screenshots). Disable other mods so the only hooks in the game are nrsc's.

### 2a. What worked on 2026-10-15

Plain `proton run` outside Steam fails: `SteamAPI_Init` can't connect, so nrsc aborts (`0x40000015` via its crashpad handler,
`nrsc.dll+0x88396`). A winmm proxy in the install loads its Steam DLL too late for an ME3 early native. What works:

- A Steam **non-Steam shortcut** (GE-Proton10-17) running
  `me3.exe launch -g nightreign -e <nightreign.exe> -p <profile> --skip-steam-init true`, with the game folder as the start-in folder.
- `WINEDLLOVERRIDES` set to native for the proxy DLL, `steam_api64` and the install's Steam DLL.
- A capture profile that loads that Steam DLL as an early native before `nrsc.dll`.
- `ptrace_scope=0`, set by the user for the session.

## 3. Finding the pid

Wine runs the game as an ordinary Linux process whose `comm` and argv contain `nightreign.exe`:

```sh
pgrep -af nightreign.exe          # several lines may show (reaper, proton wrapper): take the one the tool picks
grep -l nightreign.exe /proc/[0-9]*/maps 2>/dev/null | head   # the real game process maps the exe
```

The collector auto-detects it (a process whose maps contain a file named `nightreign.exe`). Pass `--pid N` to force one.

## 4. Running the collector

```sh
# Static phase only (no process needed, nothing read from memory):
python3 nrsc/tools/linux/collect_evidence.py --static-only --game-exe "/path/to/nightreign.exe"

# One non-interactive snapshot of the running game (add sudo or lower ptrace_scope, section 1):
python3 nrsc/tools/linux/collect_evidence.py --once title-screen --pointer-depth 2

# Guided experiments (prompts like the Windows script; start the game when asked):
python3 nrsc/tools/linux/collect_evidence.py --experiment baseline
python3 nrsc/tools/linux/collect_evidence.py --experiment all
```

Other options: `--pid`, `--game-exe`, `--mod-dll`, `--sc-dir`, `--me3-profile`, `--string-scan`,
`--probe-file`, `--copy-exe` (the exe is large, so it is only hashed by default), `--screenshot` (uses `grim`),
`--out-root`, `--no-zip`. Run `-h` for the list.

Output goes to a new directory, never overwriting:
`nrsc/evidence/runtime/<YYYYmmdd-HHMMSS>_nightreign-<version>-<exe sha8>_nrsc-<nrsc sha8>/` (+ `.zip`), holding
`static/`, `experiments/<id>/<label>/` or `snapshots/<label>/`, `collector.log`, `manifest.csv`, `manifest.json`
(build, hashes, install note, policy, ptrace scope, address tables loaded).

Snapshot contents and formats (same names and CSV headers as the Windows output; `memmap.csv` has two extra trailing columns,
`LinuxPerms` and `LinuxPath`, and the Windows `State/Protect/Type` values are synthesised from the maps):

| File | Content |
|---|---|
| `modules.csv` | Name, Base, Size, Path; modules are PE headers found at 64 KiB-aligned region starts of `/proc/pid/maps` (works even when Wine keeps the header page anonymous; named from the file mapping or by matching SizeOfImage/timestamp against the on-disk exe and DLL). RVA = address - Base |
| `memmap.csv`, `maps_raw.txt` | regions; the raw maps text |
| `game_image.bin`, `game_image.info` | `nightreign.exe` as mapped (SizeOfImage bytes) |
| `mod_data/` | nrsc.dll header + non-code sections only; `sections.txt` marks `SKIPPED(code)` |
| `reachable/` | private heap regions pointed to by nrsc's data sections (depth 1, or 2); `index.csv` |
| `private_exec/` | anonymous executable regions (trampolines) |
| `strings.txt` | locale/language strings in heap (`--string-scan` or language experiments) |
| `probes/` | extra reads from `--probe-file` (`[{"name","module","offset"|"rva","deref":[...],"size"}]`) |
| `hook_targets.csv` | address-table entries resolved to live addresses: 32 bytes, a decoded first-instruction jump (`E9`, `FF 25`, `mov r,imm64; jmp r`) and which module the destination is in |
| `patches.csv` | made offline by `collect_evidence.py diff <snapshot>`: byte differences between `game_image.bin` and the exe on disk (relocations undone; import/IAT ranges labelled), i.e. every detour and byte patch |

Experiments (ported to the nrsc open items; run `--experiment <id>`):

| id | covers | edits (backed up, restored even on error; never the DLL) |
|---|---|---|
| `baseline` | hook table, patches, trampolines; heap objects (param rows with `--pointer-depth 2`, CSChrSet / bloodstain blocks) | none |
| `lang-game` | localisation language selection (no INI key exists in nrsc; it follows the game language, **U**) | none |
| `missing-english` | loader's "no fallback" Critical Error path | removes `locale/english.json` |
| `malformed-translation` | `fmt` format error from a bad placeholder | appends an unmatched `{` to strings with placeholders |
| `nonascii-name` | narrow vs wide player-name strings | none |
| `ext-nonascii`, `ext-overlong` | save extension handling (limit 120) | `[SAVE] save_file_extension` |
| `scaling-values` | param repository scaling rows (health 150 / damage 175 / posture 125) | `[SCALING]` keys |

The ersc-only experiments (language override keys, battle-royale modes) were not ported: nrsc has no such settings.

## 5. Address tables (hooks joined with signatures)

`load_joined_tables` reads `nrsc/evidence/signatures/signatures.json` and `nrsc/evidence/hooks/hooks.json` and joins them on the
signature-table offset (`hooks[].tgt.table_off`, signature `table_offsets` / find-all `resolved_slots`):

- 178 signature slots, as `nightreign.exe` RVAs (image base `0x140000000`).
- 27 table-based hooks. Call-site and jump hooks point at the patched instruction (`E8`/`E9` decoded). Vtable-slot hooks point at the
  slot (vtable RVA + `vtable_slot_off`), and the live u64 in it is reported as the destination. The 3 kernel32 hooks have no table
  offset and are not listed. The 17 hooks from the protected installer `0x1800221e0` (`devirt/out/v2/0x1800221e0.hooks_table.txt`)
  are not joined yet.

`hook_targets.csv` shows for each target whether it is patched and where the detour goes (nrsc image, private trampoline, other).
That is the runtime evidence for the open "hook targets" items in `param_repository.md`, `world_chr_man.md` and
`dantelion_memory.md`. Use `diff` to find every patched address without any table.

## 6. Limits

- Not run against the real game: it was tested only against the synthetic child (`selftest.py`). Wine details
  (for example, which sections Wine maps anonymously) are handled by range, not by assumption, but unverified.
- Everything in nrsc's `.themida` and code sections stays unread, so detour bodies inside nrsc.dll cannot be seen. Only
  the jump instruction in the game and any private trampolines are visible.
- `--pointer-depth 2` can be slow (pure Python, up to 1 GiB of heap dumps).
