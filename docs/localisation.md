# nrsc.dll localisation

Source path: `nrsc\cs\game_message_repository/../../mod/message_repository/locale.h`.
Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. What is and is not recoverable

| Part | Status | Why |
|---|---|---|
| Translation-file loader and its error handling | **C**, reconstructed | readable: `FUN_18004cbb0`, catch funclet `FUN_180050b40` |
| Message lookup | **C**, reconstructed | readable: `FUN_18000a1c0`, catch funclet `FUN_18000a7d0` |
| Message override (id `0x52409`, fallback to the game) | **C**, reconstructed | readable: `FUN_18001e1c0` |
| UTF-8 → UTF-16 | **C**, reconstructed | `FUN_18000a920` → `MultiByteToWideChar(0xfde9, 0, …)` |
| (category, id) → key table | **U** | The 8 key strings are in `.rdata`, but no readable code or data references them (searched for `lea` targets, 64-bit pointers in `.rdata`/`.data`, imm64 and RVA32 in `.text`/`.themida`). They are built only by protected code. |
| Which fixed texts sit at which (category, id) | **U** | The texts are in `.rdata` (below); the banner, welcome and save texts have no readable references |
| Language selection and English-first order | **U** | The loader has no readable callers (only an EH IP-to-state entry at `0x180174930` and a `.themida` reference). `"english"` does not appear in the file in any searched form. |

So the reconstructed `MessageRepository` takes the key table and fixed-text placement as **inputs**.

## 2. Loader — `FUN_18004cbb0` + `FUN_180050b40` (C)

Same structure as ersc.dll's loader (2036 pseudocode lines in both, same nlohmann paths), but nrsc.dll
**catches** failures. All messages are obfuscated (XOR or SSE `xorps` with stack key tables) and were
decoded with `tools/decode_sse_strings.py`:

| Situation | nrsc.dll behaviour | Evidence |
|---|---|---|
| File missing, has fallback | Warning: `Transation file for locale "{}" not found; will use fallback "{}"` (typo as in the binary); empty locale | l.636–650 |
| File missing, no fallback | Critical Error: `Locale path "{}" not found which has no fallback! Please make sure to follow the installation instructions, make sure you have the correct files installed, and then try again.` (`{}` = path, built by `FUN_180052b50`) | 0x18004f74d–0x18004fad8 → `FUN_180052d00` |
| Parse error, has fallback | Warning: `Failed to parse translation file for locale "{}"; will use fallback "{}". Error: {}`, then resumes at `0x18004f50c` | `FUN_180050b40` → `FUN_1800526b0` |
| Parse error, no fallback | Critical Error: `Failed to parse translation file for locale "{}" which has no fallback! Error: {}` | `FUN_180050b40` → `FUN_180052990` |
| Root not an object / non-string value | message text as in ersc.dll, but caught by the handler above (S: the handler catches via `what()`; the exception type in the EH table was not decoded) | l.1904 |
| `GetModuleHandleExA` fails | Critical Error: `GetModuleHandleExA failed in dll_folder() call. This should never happen` | 0x18004fcd7 |
| `GetModuleFileNameW` fails | Critical Error: `GetModuleFileNameW failed! The path to your seamless dll is most likely too long.` | 0x18004fef1 |

- **Warning:** `MessageBoxA`-style call with `MB_ICONWARNING` (`0x30`) and title `"Warning"`, on a worker thread
  that the caller waits for (`FUN_1800526b0` → thread `0x180052ef0`).
- **Critical Error:** message box with title `"Critical Error"` and `MB_ICONERROR` (`0x10`), then fatal
  (`FUN_180040470`, `locale.h` line 0x24).

W: after a with-fallback parse error, entries added before the failure stay in the locale. Only the
resume address is known.

## 3. Lookup — `FUN_18000a1c0(repo, category, id)` (C)

Differences from ersc.dll:
- **Argument order is (category, id)**; ersc.dll used (id, category).
- **No locale → `L"[string not found]"`**; ersc.dll raised a fatal "Locale" error.
- **Format errors are caught** (funclet `FUN_18000a7d0`) and give `FORMAT ERROR FOR "<key>": <what>`. The
  first argument is the parent's `local_88`, the key: `frame + 0xd0` = parent `rsp + 0xd0`; the prologue is
  0x118 bytes plus 8 pushes, so it is `entry − 0x88`. In ersc.dll the exception propagated.

Unchanged from ersc.dll: fixed texts first (returned unformatted), then key table, then locale chain with
FNV-1a `unordered_map`s, then `KEY "<key>" NOT FOUND`; the text is always used as a format string.

The `what()` text of a format error comes from `fmt` in the original and from `std::format` here, so only
the `FORMAT ERROR FOR "<key>": ` prefix is equivalent (W for the remainder).

## 4. Message override — `FUN_18001e1c0` (C for logic; how it is attached to the game is out of scope)

```
override(ctx, …, category, id):
  if id == 0x52409:
      if no locale: return "[string not found]"
      if (200, 0x52409) is a fixed text or in the key table:
          text = lookup(200, 0x52409) formatted with (v, v), v = *(int*)(*ctx + 400)
          if text: return text
  text = lookup(category, id)
  return text if text else original_game_function(...)
```

- Category 200 is used for `0x52409` whatever category the game passed.
- The same value is passed **twice**. The only shipped text with two placeholders is
  `MODMESSAGE_YUINEXUSPLAYERCOUNTREMAINING = "{} / {}"`, so the result would read `N / N` (W: the key binding
  is not readable).

## 5. Compiled-in texts (C; placement U)

| `.rdata` | Text | Readable reference |
|---|---|---|
| `0x18014e056` | `Elden Ring: Nightreign Seamless Co-op v1.1.3 by Yui` | none, so **mod version 1.1.3** |
| `0x18014e0be` | `Welcome to the seamless co-op mod for Elden Ring: Nightreign!\n[ This is a free mod ]\nIf you have paid …` | none |
| `0x18014e24a` | `This mod uses a separate save, and does not connect to the matchmaking servers.\nIt does not contain an anticheat.` | none |
| `0x18014e214` | `[string not found]` | lookups |
| `0x18014deaa` | `~ You may be running a pirated or cracked version of the game. …` | `0x18002900d` (other module) |
| `0x18014e23a` | `NETWORK` | `0x18002120b` (other module) |

## 6. Shipped `english.json`

9 keys. All 8 key strings found in the binary exist in it. `CREDITS` is in the file but not in the binary
(same as ersc.dll).

## 7. Tests

`tests/locale/message_repository_test.cpp`: 14 tests. The (category, id) values there are **fixtures**,
because the real table is not recoverable; every behaviour tested is one of the C rows above.
