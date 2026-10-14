# nrsc.dll baseline

Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. Structure — C

- `evidence/overview.json`: 16 sections. `.text` 1.1 MB, entropy 6.55; `.themida` 7.6 MB.
- Import descriptors: kernel32, USER32, steam_api64, ADVAPI32, WS2_32, ntdll, WLDAP32, Normaliz, CRYPT32.
- `evidence/redirected_functions.json`: only **7** `.text` functions are bare jumps into `.themida`
  (`0x1800221e0`, `0x180033210`, `0x180040630`, `0x180061140`, `0x180064120`, `0x18008e050`,
  `0x18008e110`). `ersc.dll` had 44. More of `nrsc.dll` may be readable, but code can also be reached only
  from `.themida`, so this count is not a measure of coverage.
- Embedded libraries visible in strings: libcurl (schannel), nlohmann::json (`Expected JSON object…`),
  crashpad (`client/settings.cc`).

Source modules (from compiler path strings, `evidence/source_paths.txt`):

```
nrsc\hooks\hooks.cpp                nrsc\signatures\signatures.cpp      nrsc\dantelion\memory\memory.cpp
nrsc\mod\preferences\preferences.cpp nrsc\mod\param_repository\param_repository.cpp
nrsc\mod\server_manager\server_manager.cpp
nrsc\cs\world_chr_man\world_chr_man.cpp  nrsc\cs\bloodstain_man\bloodstain_man.cpp
nrsc\cs\auto_control_api\auto_control_api.cpp
nrsc\cs\game_message_repository/../../mod/message_repository/locale.h
```

## 2. Settings — `FUN_180037c50` (`ev_a3a8a266709861a9`)

| Finding | Grade | Evidence |
|---|---|---|
| File `<dll dir>\nrsc_settings.ini`; no `\` in the module path → fatal "Failed to locate settings file" (line 9) | C | l.225–300 |
| Same embedded inih configuration as ersc.dll (200-byte lines, BOM, `;#` comments, inline `;`, `=`/`:`, 49-char section/key, first-error line) | C | l.342–430, 2190–2235 |
| Same INIReader: key `lower(section "=" name)` (`FUN_180005d80`, `tolower` = `FUN_180104878`, ASCII-only in the "C" locale), repeated keys joined with `\n` (`FUN_180005960`, 0x180005be9) | C | asm |
| Key names XOR-obfuscated with `0x7e0848fd21d1e705` / `0xabcce144cc75dbce`; all decode cleanly | C | l.437–2054 |
| `SCALING/health_scaling`, `damage_scaling`, `posture_scaling` → `(float)(|v| / 100.0)` at `+0x30/+0x34/+0x38`; missing or unparsable → 100.0 (the default is baked into `FUN_1800067d0`, `ev_84a4b5a7538a663e`) | C | asm 0x180038732…0x180038d5a, consts 0x18011f250 / 0x180145d10 |
| `GAMEPLAY/allow_invaders` → `GetBoolean(default true)`, inlined, stored as `+0x04` bit 0. **Not present in the shipped INI**, so it is always true unless added | C | l.874–960 |
| `SAVE/save_file_extension`: identical rules to ersc.dll, including `std::out_of_range` for > 128 chars (`FUN_18000bba0` → "invalid string position") and the user32 character test (name length 18–19, IAT thunk `0x16224a`) | C / S | l.1850–2075 |
| Fatal "Failed to read settings. Unable to find:\n\"%s\"" (line **0x16**) and "…Error on line %u" (line **0x1a**) | C | l.2254–2290, XOR-decoded |
| l.959–1840 build a session secret from the fixed placeholder `"[ -- ]"` | C (existence) | out of scope; not reconstructed |
| No `PASSWORD` or `LANGUAGE` keys are read | C | full decode of the function |

## 3. Comparison with ersc.dll v2.0.1

| Aspect | ersc.dll | nrsc.dll |
|---|---|---|
| Game | Elden Ring | Elden Ring Nightreign |
| Settings keys | 17 (gameplay ×8, scaling ×6, password, save, language) | 5 (scaling ×3, allow_invaders, save) |
| Scaling transform | `v/100`, negatives → 0, separate enemy/boss values | `|v|/100`, one value per kind, default 100 |
| Empty password | fatal | no password key |
| Fatal line numbers (missing / parse error) | 0x15 / 0x19 | 0x16 / 0x1a |
| Modules only here | battle_royale, buddy, spectate, voice_chat, networking, param, session/yui_nexus_3, … | server_manager, param_repository, bloodstain_man, auto_control_api, dantelion\memory |
| Shared code (verified so far) | inih parser, INIReader, save-extension validation | same |

## 4. Next steps (in scope)

1. ~~Localisation~~: done, see `localisation.md`.
2. ~~param_repository~~: done, see `param_repository.md`.
3. ~~world_chr_man, bloodstain_man, auto_control_api~~: done, see `world_chr_man.md`.
4. ~~dantelion\memory~~: done, see `dantelion_memory.md`.
5. Sweep the remaining readable functions: the hook installer `FUN_18001edb0`, the detour bank
   `0x180026000..0x1800296ff`, `server_manager`.
