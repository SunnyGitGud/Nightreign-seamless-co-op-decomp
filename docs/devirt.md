# nrsc.dll devirtualisation (work package 3)

Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown. Binary read-only, only ever executed in Unicorn
(`devirt/.venv`, fake APIs, no host access). Results are in `devirt/out/v2/`; `devirt/out/*.json` are older runs and are not edited.

## 0. Provenance and v1 baseline (recovered from notes)

The v1 files `out/0x180064120.json` and `out/0x180040630.json` were **replaced by a different Claude session's later runs** while it
was working on the same tool; they were not lost by this work package's worker. Original v1 results, recovered from the hand-off notes:

| Function | v1 result |
|---|---|
| `0x180040630` | returned=false, `UC_ERR_WRITE_UNMAPPED` at rip `0x1801c4969`, after a VM exit to `0x1800ad9b0` (`__chkstk`) with ret_to `0x1807b93d5` |
| `0x180064120` | returned=false, `UC_ERR_READ_UNMAPPED` at rip `0x1801b331e`, addr `0x1000000fff28` |
| `0x180061140` | returned=false, stopped at `0x1801cb5b6`, no error (instruction budget) |

Both v1 faults were harness artefacts: `__chkstk` was stubbed with a fresh object (the VM uses `rax` as an `rsp` delta), and symbolic
objects had no room for negative offsets (`0x180064120` reads `a0-0xd8`).

## 1. Method and tools

| File | Role |
|---|---|
| `tools/boundary.py` | v1 harness, also edited concurrently by the other session (kept working; one allocator fix by this worker) |
| `tools/boundary2.py` | this work package's fork. Adds: Python stubs for `operator new/delete`, `malloc/free`, `__chkstk` (heap with named allocations `heapN[size]+off`), per-target `--native`/`--stub`, fake clock (QPC/QPF/GetSystemTimeAsFileTime/Sleep advance time), string capture of pointer arguments (`strs`), `--poke PATH=VALUE`, `--arg-struct`, `--init`, writes only under `out/v2/` or scratch |
| `tools/wp3.py` | scenario harness: build concrete heap object graphs, run a VM function on them, `explore()` plants objects on null-dereference faults, `brief()` |
| `scenarios/s_*.py` | per-function scenarios (0x180033210, 0x18008e110, 0x180061140) |
| `tools/iat_usage.py` | call-site argument extraction for every IAT slot (code calls through the IAT at `0x180160d98`, 0x868 above the import lookup table that `iat.py` indexes) |
| `tools/iat_resolve.py` | the IAT decisions with evidence, validation, `out/v2/iat_resolved.json` |

Semantics come from **trace-based reasoning with input variation** (concrete object graphs, parameter sweeps, bisection of thresholds), not
from lifting the Themida handlers. Miasm lifting of the handlers was not attempted (cost/benefit: the observable boundary was enough).
Everything that crosses out of `.themida` (calls into `.text`, API calls, reads/writes of scenario memory and globals) is logged.

Harness facts learned (**C**): the PE entry `0x1801b4043` is Themida's stub; the original CRT entry is `_DllMainCRTStartup` at `0x1800e6f68`
(`cmp edx,1 ... __security_init_cookie 0x1800e6fa8`). Running it first with `--init` ends at a fetch of rip `0x1` after 19 events
(`out/v2/0x180040630.fake_game_init_attempt.json`), so it is not usable yet; it is not needed for the results below.

## 2. Per-function results

| Function | Size | Returns now? | What it is | Grade | Likely source |
|---|---|---|---|---|---|
| `0x18008e050` | 179 | yes (rax=0 with default stubs; reaches `Curl_open` with ok stub returns) | `curl_easy_init()` | C | libcurl 7.76.1 |
| `0x18008e110` | 74 | yes | `curl_easy_perform(CURL*)` | C | libcurl 7.76.1 |
| `0x180033210` | 46 | yes | CURLOPT_WRITEFUNCTION callback into a 0x30e8-byte buffer | C (behaviour), S (intent) | mod, server_manager / update check |
| `0x180064120` | 605 | yes (3-4 events) | tiny key-match accessor in a closure vtable | C (behaviour), W (class) | mod (hooks closures / std::function-like) |
| `0x180061140` | 8025 | **yes, on the real startup state, every tick of every scenario** (2.10) | FD4 task run slot: party net sync (data send to members, 1 s heartbeat, F4 pause toggle) | C (behaviour in emulation), S (purpose) | server_manager |
| `0x1800221e0` | 13742 | yes (v1 and v2 symbolic) | installer of 17 more hooks (call-site, jmp, MinHook) | C | hooks.cpp |
| `0x180040630` | 28443 | **yes once its running flag is cleared** (2.9, 2.10); never on its own: only the thread itself holds the flag | mod main-thread routine | C (emulation) | mod init |

### 2.1 `0x18008e050` = `curl_easy_init` (C)

```c
CURL *curl_easy_init(void) {
    if (!initialized) {                       // g 0x18017cefc
        initialized = 1;                      // global_init(CURL_GLOBAL_DEFAULT = 3, memoryfuncs)
        Curl_cmalloc  = 0x18010195c;  Curl_cfree   = 0x180100afc;   // writes at 0x18017b5b0..0x18017b5d0
        Curl_crealloc = 0x180101dc4;  Curl_cstrdup = 0x1800fdae4;  Curl_ccalloc = 0x1800ffae4;
        if (!Curl_ssl_init())          /* 0x1800a23c0 */ goto fail;
        if (Curl_win32_init(3))        /* 0x1800ac180 */ goto fail;
        if (Curl_resolver_global_init()) /* 0x1800805c0 */ goto fail;
        ...
      fail: initialized--; return NULL;
    }
    if (Curl_open(&data))              /* 0x18009c780 */ return NULL;
    return data;
}
```
Evidence: `out/v2/0x18008e050.easy_init_ok_returns.json` (call order and the global writes), the three failing variants (ssl_init=0 gives
`initialized` 1->0 and NULL). In the main trace its result is the CURL* for `0x18008b660` (`curl_easy_setopt`). The native `0x18008dfb0(3)` that
the VM calls just before is `curl_global_init(CURL_GLOBAL_ALL)`.

### 2.2 `0x18008e110` = `curl_easy_perform` (C)

Concrete tests (`out/v2/0x18008e110.scenarios.json`): `NULL` -> `0x2b` (CURLE_BAD_FUNCTION_ARGUMENT); `*data->set.errorbuffer = 0` (+0x1e0);
`data->multi` (+0xd0) set -> `failf(data, "easy handle already used in multi handle")` (string at `0x180152fb0`) and `0x2` (CURLE_FAILED_INIT);
otherwise `data->multi_easy` (+0xd8) is created with `0x1800a5510(1, 3)` (`curl_multi_init` shape) and the multi loop follows (`easy_perform`).

### 2.3 `0x180033210` (C behaviour)

```c
size_t write_cb(char *ptr, size_t size, size_t nmemb, struct buf *ud) {   // buf = new char[0x30e8], zeroed; ud+8 = data
    size_t n = size * nmemb;
    (void)ud->len;                 // read, never used
    ud->len = n;                   // NOT += : each chunk overwrites
    memcpy(ud->data, ptr, n);      // no bound check against 0x30e0 here
    return n;
}
```
8 inputs in `out/v2/0x180033210.scenarios.json`. It is installed by `0x180040630` as option `0x4e2b` (CURLOPT_WRITEFUNCTION) together with
`0x2712` (URL, **U** which string), `0x72` (CURLOPT_MAXFILESIZE = `0x30e0`) and `0x2711` (WRITEDATA = the 0x30e8 buffer). The size limit is enforced by libcurl
(MAXFILESIZE), not by the callback. A one-shot fetch whose result is used by the "Update required" check (see 2.7) (**W**).

### 2.4 `0x180064120` (C behaviour, W class)

```c
void *slot4(void *this_plus_0xd8, struct { uintptr_t key; } *a1) {
    Inner *in = *(Inner **)((char *)this_plus_0xd8 - 0xd8);
    return a1->key == *(uintptr_t *)(in + 0x130) ? *(void **)in : NULL;
}
```
Six input combinations (`out/v2/0x180064120.scenarios.txt`). Slot 4 of the vtable at `0x180147898` (`.rdata 0x1801478b8`), in a family of paired vtables, so it
is a `std::function` / closure vtable method (a `_Target`-style "return the stored callable if the key matches"). Source module not decidable (**U**).

### 2.5 `0x180061140` (S structure)

Run slot (vtable `0x180147b40`, slot at `0x180147b50`) of a server_manager task whose `this+0x30` is the owner (docs/server_manager.md 2.4). A concrete graph makes it
return with 50 events (rax 0):

```c
int run(Task *this, Args *a) {
    Owner *o = this->owner;                // +0x30 ; NULL -> return 0 (v1 zero-fill run)
    Core  *c = *(Core **)o;                // o[0]
    Timers *x = c->timers;                 // c+0x80
    x->t148 += a->dt;                      // a+8 float; read then written
    if (x->t148 >= 1.0f && x->t14c >= 2.0f/3.0f) x->t148 = 0;     // C: bisected, threshold 0.66666 (const 1.0 at 0x180145d70)
    for (Elem **p = begin; p != end; ++p)  // begin/end = vector at c[0]->+0x28 (two qwords)
        if ((*p)->flag128 == 1) {          // C (E0/E1 sweep): flag==2 or 0 -> nothing
            (*p)->flag128 = 0x11; (*p)->field3c = 1;
            0x18003f2c0(c[0], *p);         // event fan-out / "send" helper; 0x180040370(begin, end) is the vector search/erase step (W)
        }
    x->t150 += a->dt;  ... x->t154 ...      // second timer
    Y = c+0x88: vector begin/end read (empty in scenario)
    x->t170 = 0; x->t174 = 0; x->t190 = 0  // resets under conditions (W)
    c+0x28 -> +0x28 -> [0];  c+0x90;  c+0x8 -> +0x20 -> [0];  c+0x20 -> +0x10 -> [0];  g 0x180183018 = 0
}
```
Reached exits: `0x180040370(vec.begin, vec.end)` and `0x18003f2c0(c[0], elem)` exactly when an element's byte `+0x128` equals 1 (C: 4 flag settings tested). With the exit stubbed to
return a fresh object the `0x180040370` search never reaches `end` and the loop spins, which is why such runs hit the instruction cap; with the real helper it is a search over the same vector (W).
`0x18003f030` (named in server_manager.md) was not reached (U, other branches: Y vector non-empty, `X+0x170/0x190` conditions).

### 2.6 `0x1800221e0` (C): installer of 17 additional hooks

Called from the hook installer `0x18001edb0` at `0x180020ce5` as `thunk_FUN_18061e518(ctx)`. For each hook it allocates a 0x40-byte closure
`{vtbl, ctx, detour@+0x10, self@+0x38}`, writes the 0x20-byte arena thunk (`48 B8 <closure> 66 48 0F 6E E8 49 BB <invoker> 41 FF E3`), then either patches the
target (VirtualProtect RWX, `E8`/`E9` rel32, restore, registry add `0x18002cad0`) or calls MinHook `MH_CreateHook` `0x1800068d0`. Table (`out/v2/0x1800221e0.hooks_table.txt`):

| # | sig-table offset | kind | detour | invoker | orig slot |
|---|---|---|---|---|---|
| 0 | +0x278 | call-site E8 | `0x180026200` | `0x1800260b0` | ctx+0x78 ->+0x20 |
| 1 | +0x288 | call-site E8 | `0x180026230` | `0x180028460` | ctx+0x60 ->+0x48 |
| 2 | +0x290 | jmp E9 | `0x180026250` | `0x1800288b0` | none |
| 3 | +0x298 | MinHook | `0x180026310` | `0x1800284d0` | ctx+0x90 ->+0x48 |
| 4 | +0x2a0 | MinHook | `0x180026370` | `0x180028740` | ctx+0x90 ->+0x50 |
| 5 | +0x2a8 | MinHook | `0x1800264e0` | `0x1800285a0` | ctx+0x90 ->+0x58 |
| 6 | +0x2b0 | call-site E8 | `0x1800265f0` | `0x180028530` | none |
| 7 | +0x2b8 | call-site E8 | `0x180026690` | | none |
| 8 | +0x2c0 | call-site E8 | `0x1800266c0` | | ctx+0x68 ->+0x38 |
| 9 | +0x2c8 | call-site E8 | `0x180026700` | | ctx+0x90 ->+0x60 |
| 10 | +0x2e0 | jmp E9 | `0x180026c50` | | none |
| 11 | +0x2e8 | jmp E9 | `0x180026e50` | | none |
| 12 | +0x280 | call-site E8 | `0x180027090` | | none |
| 13 | +0x2f0 | jmp E9 | `0x180027310` | | none |
| 14 | +0x358 | call-site E8 | `0x180027870` | | ctx+0x90 ->+0x68 |
| 15 | +0x388 | call-site E8 | `0x180027dc0` | | none |
| 16 | +0x5d0 | **1-byte immediate patch** (corrected 2026-10-15 from the live capture): `cmp eax, 0x11` → `cmp eax, 0x2f` (17 → 47) at rva `0x4ff6f7`; the slot points at the immediate (pattern `8D 46 FA 83 F8 11`, offset 5). The earlier "jmp E9 → `0x180028030`" reading was wrong (C: `evidence/runtime/20261015-012952_…/snapshots/in-game/patches.csv`) | | | |

The detours match the "protected-installed detours" of docs/hooks.md section 5 (`0x180026830`, `0x180026b30`, `0x180027090`, ... ); the sig-table offsets
join with the signatures work package. 146 `VirtualProtect` calls (73 patch sites). Orders/targets are fixed by data in the signature table (**C**); what each hook does is
in the detours (hooks WP).

### 2.7 `0x180040630`: mod main-thread routine, cannot return (S)

Callers: wrapper `0x1800405c0` (called by thread proc `0x180001360`, started by `modengine_ext_init` `0x180001220` via `_beginthreadex`; the wrapper builds
`{1, GetModuleHandleA("nightreign.exe"), arg1, arg2}`). Observed, in order (C, `out/v2/0x180040630.update_required_path.json`): `__chkstk` for 0x19b8; five
`new[0x97d18]` arrays of 1009 x 0x268-byte elements (ctor `0x180064c60`) with their vector wrappers (`0x18003ee30`, 0x18011b4e0 = memcpy); settings loader `0x180037c50`;
`0x18008e294(0x38/0x48)` objects; 8x `SteamAPI_RegisterCallback` (ids `0x201, 0x1f8, 0x1fa, 0x4e3, 0x4e4, 0x501, 0x14d, 0x1f9`); `curl_global_init(3)`, `curl_easy_init`, setopts
(URL, write callback, MAXFILESIZE 0x30e0, WRITEDATA), `curl_easy_perform`; 4x map emplace `0x180063570`; `GetModuleHandleA("nightreign.exe")`, `VirtualQuery`, `GetSystemInfo`,
`K32GetModuleInformation` (signature-scan prep); hook installer `0x18001edb0`; ready-flag notify `0x18008fdbc` (the flag passed by `modengine_ext_init`'s wait); then a **polling loop**
(100 x `SteamAPI_GetHSteamUser/Pipe` + `GetModuleHandleA` + `Sleep`). With the fake all-`int3` game, signature lookup fails and the routine reaches the fatal path: `MessageBoxA`
"Update required" (caption), then process termination (jump to garbage, rip `0x1db5a6425354880`).

Why it cannot return in the sandbox: (1) its normal role is the lifetime of the mod's thread (init, then waiting on game/Steam readiness; no return seen in
600 M instructions with stubs, where the time functions were stubbed it spins 41,000 exits); (2) with the fake game the first failing signature leads to a fatal exit; (3) a
faithful run needs the real `nightreign.exe` image mapped as data plus real signature hits (hooks/signatures WPs), a real Steam interface table, and a fake filesystem for the settings file.
Mapping the 118 MB exe as data inside Unicorn was **not done**. The earlier faults were all harness bugs: null `vector<uint32_t>` after `0x18001ecc0` (needs it natively), unconstrained
time (needs the fake clock), settings loader `0x180037c50` natively (needs a file API; stubbed instead), stdio stream table (FLS/TLS; `--native all` faults at `0x180110b22`).

Settings: `0x180037c50(&out, a0+0x10)` is the already-documented `nrsc_settings.ini` parser; it runs with `GetModuleFileNameA` first.
Also the source of the "Update required" text: `text` is formatted with `%i` (version), exit follows (**S**, `caption_w` in the JSON).

### 2.7a Runtime confirmation (2026-10-15)

The live capture (`evidence/runtime/20261015-live_capture_analysis/FINDINGS.md`) confirms the protected installer's table: 16 of 17 hooks
are live jump hooks whose closure vtable and `+0x10` detour match the table exactly (C). Hook 16 is the immediate patch above.

### 2.8 Real-image run (2026-10-15)

Tool `tools/realimage.py` (imports `boundary2.py`, does not edit it). Results in `out/v3/` (`run_*.json`, `summary.json`). Unicorn only, no network: `curl_easy_perform`
is faked, the URL strings below are decoded data and were never contacted. Run as `--native-all --init --stub 0x180037c50 --stub 0x18002d310 --stub 0x1800906a0 (ret 1)`; about 50 s each.

**Setup (C unless noted).** `nightreign.exe` v1.03.3 is mapped read/write, non-executable, at `0x140000000` with the `module_image.cpp` layout; a fetch into it would be logged and turned into `ret` (never happened). `FUN_18002c340` (first-match scan) and the RTTI slot loop of `FUN_18007d380` are accelerated by Python hooks with the same semantics; the builder `FUN_1800655a0` and the installers run as emulated code. The CRT `DllMain` now runs to completion (`--init` returns TRUE) after faking `GetProcAddress`/`LoadLibraryEx` (per-name stub addresses), `GetCommandLine*`, and `NtGetNextThread` (no more threads). Earlier failure (`Failed to find signature: 0`) was a harness bug: the VM reads `PEB.ImageBaseAddress` and the PE header to fill `table[0]/[1]`; PEB was zero. With `PEB+0x10 = 0x140000000` the table gets base `0x140000000`, size `0x7c5d600`.

**1. Signature scan (C).** 705 calls of `FUN_18002c340`; all 146 find1/inline entries return exactly the first match listed in `evidence/signatures/signatures.json` (146/146), the 8 no-match calls are the terminators of the find-all searches, the counter at `table+0x10` reaches 0x92 = 146. The builder returns, no fatal path.

**2. curl (C for options, S for use of the data).** Three fetches in a row, each `curl_global_init(3)` once, `curl_easy_init`, then four setopts (URL `0x2712`, WRITEFUNCTION `0x4e2b` = `0x180033210`, MAXFILESIZE `0x72` = `0x30e0`, WRITEDATA `0x2711` = new[0x30e8]), `curl_easy_perform`. No other options (no HTTPHEADER, proxy, timeout, user agent, SSL flags) are set:

| # | URL (decoded from the VM string) |
|---|---|
| 1 | `https://raw.githubusercontent.com/LukeYui/NightreignSeamlessCoopRelease/refs/heads/main/VERSION` |
| 2 | `https://raw.githubusercontent.com/yuiamoroll/NightreignSeamlessCoopRelease/refs/heads/main/SUPERNIGHTLORD2` |
| 3 | `https://raw.githubusercontent.com/yuiamoroll/NightreignSeamlessCoopRelease/refs/heads/main/ENDLESSNIGHT` |

Handling of the body (C for the call sequence, `out/v3/run_*.json`): `strlen` (`0x180102f50`) of `buf+8`, copy into a `std::string`, construct an istringstream-like object (`0x180033240`), parse (`0x180033520`; for non-empty bodies also `0x180033990`, `0x180033cb0`), then `0x18011b3e0` (memcmp-shaped, 4 bytes) of the local string `"1.13"` against a 4-byte token of the body. With body `1.1.3` there is one compare, with `1.13` two compares both equal (rax 0), with `9.9.9` two compares, first rax `0xffffffff`. The write callback is called per chunk with `size=1` and overwrites (does not append). Empty body or perform error 7 skip the parse and still go on. **In none of 9 variants (empty, error 7, `hello world`, `1.1.3`, `1.1.3\n`, `1.13`, `1.13\n`, `9.9.9`, two chunks `1.1`+`.3`) did the "Update required" MessageBox appear or the later flow change**; all end identically (see 4). The three bodies are therefore at most informational or compared against stored values; what the second and third files are (names suggest data lists) is **U**. Exact token format and how a mismatch is handled are **U** (W: version compare against `1.13`).

**3. Init order after the scan (C).** builder `0x1800655a0` -> `0x180063900/0x180063a60` -> `0x18001ecc0` -> `GetModuleHandleA("nightreign.exe")`, `VirtualQuery(base)`, `GetSystemInfo`, `K32GetModuleInformation(-1, base)`, `VirtualQuery`, `GetModuleHandleA("OnlineFix64.dll")` (presence check, fake returns 0) -> 4x map emplace `0x180063570` -> `0x18001ecc0` x2 -> MinHook init (`HeapCreate`) -> hook arena: `VirtualQuery(0xc7d5d601)` = image end + arena size - 2 GiB, `VirtualAlloc(0x100000)` at a free hole (needs a fake address space; the first harness had none, so ctx+0x10 stayed 0 and the installer crashed in `0x1800289b0`) -> hook installer `0x18001edb0`: 267 `0x72->0xEB` patches then the call-site/jmp/vtable/MinHook hooks, each wrapped in `VirtualProtect` + `FlushInstructionCache` and a `CreateToolhelp32Snapshot`/`Thread32First/Next` thread walk; 462 writes into the game image in total (counted only, 267 of them the EB patches; contents not analysed) -> kernel32 hooks (`GetProcAddress` of `GetThreadContext`, `SetUnhandledExceptionFilter`, `AddVectoredExceptionHandler`) -> ready-flag notify `0x18008fdbc` -> `SteamAPI_GetHSteamUser/Pipe`, `GetModuleHandleA("steamclient64.dll")`, `CreateInterface("SteamClient020")`, then interface requests (SteamUtils010, STEAMAPPS_008, SteamUser021, USERSTATS 012, SteamFriends017, MatchMaking009, Networking005, NetworkingUtils003, NetworkingMessages002) -> locale/message repository loader (`nrsc/mod/message_repository/locale.h`, line 36).

**4. End of run (S).** The run does not return and does not yet reach a steady loop: it stops at the fatal `"Locale path \"` (caption `Critical Error`, then `Seamless Coop 1.1.3 - Fatal Error`, `locale.h` line 36), because the fake filesystem reports no locale directory (`GetFileAttributesExW`/`CreateFileW` return 1/garbage, `GetModuleFileNameW` is the Steam path). The earlier "polling loop" (100 x Steam + Sleep) happens only if `steamclient64.dll` is not found; with a fake handle it is skipped. Beyond the locale loader nothing is known (U).

**5. Signature-table reads (W for the VM side, S for native).** `out/v3/run_slots.json` (`slot_reads`): 173 of the 178 table offsets are read; the VM reads 133 of them (including +0x8..+0x58, bulk-like, likely a copy; the table object lives in the VM stack frame), native code reads 40 first, in order `+0x18, +0xe0, +0x68, +0x70, +0x80, +0xd8, +0x60, +0x88, +0xa8, +0xe8, +0x78, +0xa0, +0xc8, +0xd0, +0x228, +0x108..+0x178 ascending, +0x200, +0x238, +0x240, +0x210, +0x248, +0x250, +0x258, +0x268, +0x180` (installer order, matches hooks.md section 2 for the hooks it installs). Never read before the fatal: `+0x100, +0x1a0, +0x1e0, +0x230, +0x270, +0x320, +0x340, +0x4c8, +0x550` (some are read later by hooks, U).

**Harness fakes that matter** (and so are assumptions): free-address-space `VirtualQuery`/`VirtualAlloc`, `GetSystemInfo`, `K32GetModuleInformation`, `Thread32First/Next` = no threads, one universal Steam interface object whose every vtable slot returns itself, settings loader `0x180037c50` and crashpad `0x18002d310` stubbed. **Open:** filesystem fakes for the locale loader and settings (how far the run goes then); contents of the 462 game writes; meaning of URLs 2 and 3; exact version token format.

### 2.9 Real-image run past the locale loader (2026-10-15)

Tool `tools/realimage2.py` (imports `realimage.py`, which imports `boundary2.py`; none edited) and `tools/summarize_v4.py`. Unicorn only, no network, nothing native. Results in `out/v4/`:
`run_main.*` (80 M instruction cap, 437 s wall with three runs in parallel), `run_release.*` (flag cleared after 60 sleeps), `run_french.*` (Steam language "french", flag cleared after 20 sleeps), each with
`.events.jsonl` (every event, written incrementally, flushed every 20 s) and `.summary.json`, plus `summary.json` (merged). Command (minutes per run; the post-run heap analysis for the locale tables dominates; scan cache in the scratchpad):
`.venv/bin/python -I tools/realimage2.py --tag run_main --native-all --init --stub 0x18002d310 --stub 0x1800906a0 --stub-ret 0x1800906a0=1 [--lang french] [--release-after N]`.
Caps: `--max 80000000` instructions and `--wall 900` seconds, whichever first; SIGTERM also flushes. Grades C/S/W/U as before. "evno" = 0-based line in `run_main.events.jsonl`.

**Harness changes (C, assumptions).** (1) Read-only fake filesystem backed by `binary/SeamlessCoop/` (`nrsc.dll`, `nrsc_settings.ini`, `locale/english.json`, `crashpad/`, `crashdumps/`) under the game path
`C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING NIGHTREIGN\Game\` plus the real `nightreign.exe`; served through CreateFileA/W, ReadFile, GetFileSizeEx, SetFilePointerEx, GetFileType, GetFileTime,
GetFileInformationByHandle(+Ex classes 0/1/9/18), GetFileAttributes(Ex)A/W, FindFirstFile(Ex)/FindNext/FindClose, GetFullPathNameW, GetCurrentDirectory; CreateDirectory/DeleteFile/MoveFile/WriteFile and write-mode opens are refused
(logged in `fs.denied`; none occurred). (2) `MultiByteToWideChar` / `WideCharToMultiByte` are now real (UTF-8). (3) Steam: one object per requested interface version, 128 logged vtable stubs each, `CreateInterface` returns
SteamClient020, fixed SteamID 76561198012345678, BLoggedOn 1, app id 2622380, language `english` (or `--lang`), no lobbies (lobby/leaderboard calls return fresh non-zero call handles). (4) The settings loader and the locale loader run
as emulated code; only crashpad `0x18002d310` and `0x1800906a0` stay stubbed.

**Corrections to 2.8 (C).** (a) The fatal "Locale path" in 2.8 was caused by the harness' `MultiByteToWideChar` returning 1 (the `locale` path component was lost: the path handed to `GetFileAttributesExW` ended in `SeamlessCoop\`), not (only) by the missing directory.
(b) `realimage.py` read the thread argument from `0x180183f60`; `modengine_ext_init` actually passes the global at **`0x180182f60`** (`0x180001268`), and after the CRT `DllMain` it holds **`0x180000000`, the DLL's own HMODULE** (C). With it the wrapper block is
`{u32 1, GetModuleHandleA("nightreign.exe"), hModule, &ready_flag}` and the settings loader gets the DLL handle, so `GetModuleFileNameA` returns the DLL path and the ini is looked up in `SeamlessCoop\` (C). With 0 (as in 2.8) it asks for `Game\nrsc_settings.ini` and ends in the
"Failed to read settings" fatal (line 0x16) (C, reproduced).

#### Where the run ends (C for the emulation, S for the interpretation)

`run_main`: **steady loop, no return, no fatal.** After `evno 8058` the routine repeats forever `if (*(u32*)block == 1) Sleep(0x32)`: 5398 iterations in the 80 M cap, each iteration one 4-byte read of the wrapper block's first dword (value 1) and `Sleep(50)`, no other
API, no Steam call, no VM exit (last 400 events). `run_release` (dword cleared after 60 sleeps): the routine leaves the loop, runs the destructors of its local maps (`0x180049ae0`..`0x18004a510`, `0x180049c80/c20/bc0`, 4x `operator delete`, `0x18007da40`) and **returns**
(`stop_rip` = sentinel, rax `0xf`, which is not meaningful); it does not call `SteamAPI_UnregisterCallback`, does not touch Steam or destroy the four task objects (S: they are owned elsewhere). So the thread's lifetime is the lifetime of the stack-resident callback objects and of
the handler map, which is why it never returns on its own; the stop flag is set by the DLL side (who clears it is **U**; presumably `DLL_PROCESS_DETACH` / shutdown). Writes into the game image: 462 in 122 pages (counted, not analysed); no fetch into the game image (`game_exec` 0). No MessageBox, no denied write.

#### Milestones in `run_main` (evno)

| evno | What | Grade |
|---|---|---|
| 5324 | settings loader `0x180037c50` (opens `SeamlessCoop\nrsc_settings.ini`, parses, returns) | C |
| 5816-5831 | 8x `SteamAPI_RegisterCallback` (table below), callback objects on the thread stack | C |
| 5834-5905 | curl: 3 fetches (unchanged from 2.8) | C |
| 6028-6034 | 4x map emplace `0x180063570` (handler table below) | C |
| 6126, 6139, 6154, 6242 | the four task objects get their vtables (`0x180147b00/b40/b80/bc0`) | C |
| 6276-7432 | hook installer: 927 `VirtualProtect` calls | C |
| 7359-7435 | `steamclient64.dll`, `CreateInterface("SteamClient020")`, 9x `GetISteamGenericInterface`, then `GetSteamID`, `GetAppOwner`, `GetPersonaName`, `GetCurrentGameLanguage` | C |
| 7540 | locale loader call 1, name `english` | C |
| 7671 | locale loader call 2, name = result of `GetCurrentGameLanguage` | C |
| 7832-7948 | the 8 `MODMESSAGE_YUINEXUS*` strings read from `.rdata` (key table build) | C |
| 8049-8055 | task slot 6 (`0x180061130`, `this+0x30 = owner`) on the first three tasks | C |
| 8058 | first `Sleep(50)`: steady loop | C |

#### Message-handler registrations (`0x180063570`, 4 calls, C)

`rcx` = a `std::map<uint8_t, std::function<...>>` that lives in the thread's stack frame (`0x7ff0001ef1c8` in the emulation); `*r8` = key byte, `*r9` = the function pointer stored in a `std::function` impl (vtable `0x180147a80`, its call forwarder
`0x1800638d0` calls `fn(*a, *b, *c, int d)`, i.e. `(ctx, data, peer, remaining)` as in server_manager.md 2.2). Registered in this order, all from protected code:

| id | handler | What it is | Grade |
|---|---|---|---|
| 0 | `0x18003f4a0` | readable receiver with the 8-byte sub-header (u32 channel, u32 len at +7/+0xb, channel < 999) | C (address), S (role) |
| 1 | `0x180040130` | readable receiver for the channel-less broadcast queue (ctx+0x130) | C / S |
| 2 | `0x180004c80` | **no-op that returns true** (`mov al,1; ret`): message id 2 is accepted and ignored | C |
| 3 | `0x180040240` | readable receiver for the peer channel `< 9`; also the id the data sender `0x18003f030` and the control sender `0x18003f2c0` use | C / S |

No sender for ids 0, 1 and 2 exists in readable code (S; server_manager.md 2.2 knew only id 3). Whether the map is later copied into `ctx+0x78/+0x88` is **U** (no further emplace; the four emplaces all hit the same stack map).

#### Steam callbacks (`SteamAPI_RegisterCallback`, 8 calls, C)

IAT slot `0x180160a80` (label `{SteamAPI_RegisterCallback|SteamAPI_SteamUtils_v010}`) is `SteamAPI_RegisterCallback(pCallback, iCallback)` (C: argument shape). The objects are 8 consecutive 16-byte `CCallbackBase`s at `0x7ff0001ef448 + 0x10*i` in the startup thread's
frame (S: locals; `this - const` reaches the core at `owner+8`: lock at core+0x78, closed flag core+0xc4, `ISteamNetworkingMessages*` core+0x50, all matching server_manager.md 2.). The callback id is the call argument (the object's `+0xc` is still 0 at registration, the flag byte `+8` is 0).
Ids are the public SDK `k_iCallback` values (C for the numbers, S for the struct names).

| id | SDK struct (S) | vtable | Run slot | Readable? / what it does |
|---|---|---|---|---|
| 0x201 (513) | `LobbyCreated_t` | `0x180147710` | `0x180064a50` | readable (server_manager 2.1) |
| 0x1f8 (504) | `LobbyEnter_t` | `0x180147750` | `0x1800647b0` | readable |
| 0x1fa (506) | `LobbyChatUpdate_t` | `0x180147790` | `0x1800645b0` | readable |
| 0x4e3 (1251) | `SteamNetworkingMessagesSessionRequest_t` | `0x1801477d0` | `0x1800644c0` | takes the core lock, then calls `ISteamNetworkingMessages` vtbl+0x10 (`AcceptSessionWithUser`) with the request's identity (C asm, S name) |
| 0x4e4 (1252) | `SteamNetworkingMessagesSessionFailed_t` | `0x180147810` | `0x180001080` | `ret`: ignored (C) |
| 0x501 (1281) | `SteamRelayNetworkStatus_t` | `0x180147850` | `0x180064430` | stores the dword `m_eAvail` at core+0x70 (C asm, S name) |
| 0x14d (333) | `GameLobbyJoinRequested_t` | `0x180147890` | `0x1800643c0` | copies the 16 bytes {lobby id, friend id} to `X+0x130/+0x138` and sets bit 0x10 in `X[0]` (X = pointer at core+0x60); i.e. a "join requested" flag with the target lobby (C asm, S meaning) |
| 0x1f9 (505) | `LobbyDataUpdate_t` | `0x1801478b0` | **`0x180064120`** (VM, 605 B) | protected; this answers the open item "class owning vtable for `0x180064120`" from 2.2/5: it is the Run slot of the `LobbyDataUpdate_t` callback (C for the slot, S for the struct name) |

All eight share slot 0 `0x18004aa00` = `Run(void*, bool, SteamAPICall_t)`, which forwards to slot 1 (`Run(void*)`, the column above; C asm `mov rax,[rcx]; jmp [rax+8]`); slot 2 is a per-class `GetCallbackSizeBytes` (`mov eax, size; ret`, `0x18004aa10..aa70`; e.g. `0x20` for the first); slot 3 is the deleting destructor (e.g. `0x180064440` for id 0x501: restores the base vtable `0x180147870` and calls the IAT slot `0x1801612f8`, `SteamAPI_UnregisterCallback` (W), when flag bit 0 of the object is set). No `SteamAPI_RegisterCallResult` happens at startup.

#### server_manager wiring at startup (C for observations, S/W for roles)

1. **Peer pool = 5 `Peer` objects (C).** Five `new[0x97d18]` blocks; the queue ctor `0x180064c60` runs **5045 times = 5 x 1009** (1 at peer+0x130, 999 at peer+0x398 + k*0x268 (k < 999), 9 at peer+0x96770 + j*0x268), arguments `(this, storage, 0x1fffff, 0)`. 0x96770 + 9*0x268 = 0x97d18 = the block size, so the
layout in server_manager.md 2 is exact and the pool size (U in 4a row 7) is **5**. (`0x180064c60` is the per-queue constructor, not a server_manager constructor.) This is also why the heap grows to 0x2c9cd260 bytes.
2. **Four task objects (C).** Heap blocks of 0x38 (`0x180147b00` pump, `0x180147b80` ranking-db task, `0x180147bc0` params task) and 0x40 (`0x180147b40`, the VM task `0x180061140`); the vtable stores come from VM code (rip `0x1801f313c`, evno 6126/6139/6154/6242). Layout seen (S): `+0x00` vtable, `+0x20` = `0x98`,
`+0x28` = `0x140febc80` (a game-image address, RVA `0xfebc80`, same value in all; opaque), the VM task also has the float `30.0f` at `+0x3c`. Slot 6 of the first three vtables is `0x180061130` (`this+0x30 = owner`, C asm) and is called for them with the same `owner` (evno 8049-8055); `0x180147bc0` has a `ret` in slot 6 and gets no owner.
3. **Owner object (S).** One struct in the thread's frame (`0x7ff0001efea0`). Seen fields: `+0x00..+0x28` pointers to other frame locals (`+0x08` = the core at `0x7ff0001ef380`), `+0x30/+0x58` heap pointers, `+0x50 = 0x1fd68` (130408, the BEGINTHREEPLAYER game message id), `+0x78 = 0xb1d000052409` (low dword `0x52409`, the override id of localisation.md 4),
`+0xa8` = the VM task object, `+0xb0` = the locale repository (size-0xf8 heap object that is argument 1 of the locale loader). Full dump in `out/v4/summary.json` (`set_owner`).
4. The core sits at `owner+8` and carries the 8 callback objects at core+0xc8, +0xd8, +0xe8, +0xf8, +0x108, +0x118, +0x128, +0x138 in registration order (S: computed from the `this - const` accesses of the readable callbacks).
5. Nothing at startup calls the lobby, leaderboard, send or receive code (`0x18003d420`, `0x18003d5e0`, `0x18003d6a0`, `0x18003d760`, `0x18003f030`, `0x18003f2c0`, `0x180040370`, `0x180063110`, `0x180061140` all have count 0): those run from the game-side hooks/task scheduler. How the tasks get scheduled is **U**.

#### Steam interface use at startup (C)

All nine interfaces are requested through `SteamClient020` slot `0x60` (`GetISteamGenericInterface`) in this order: `SteamUtils010`, `STEAMAPPS_INTERFACE_VERSION008`, `SteamUser021`, `STEAMUSERSTATS_INTERFACE_VERSION012`, `SteamFriends017`, `SteamMatchMaking009`, `SteamNetworking005`,
`SteamNetworkingUtils003`, `SteamNetworkingMessages002`. Then exactly: `ISteamUser` +0x10 `GetSteamID`; `ISteamApps` +0xa0 `GetAppOwner`; `ISteamFriends` +0x0 `GetPersonaName`; `ISteamUtils` +0x78 (twice, slot 15, name W); `ISteamApps` +0x20 `GetCurrentGameLanguage`
(its result is the name for locale loader call 2, C: `english` -> `english`, `--lang french` -> `french`). Not called at startup: `BLoggedOn`, `GetAppID`, any Matchmaking / UserStats / Networking* method. So the fake SteamID, app id and "no lobby" choices do not influence this run (S); the SteamID/owner/persona are stored for later use (U where).

#### Locale (C unless noted)

- **Language selection and English-first order are now C.** The loader `0x18004cbb0` is called twice, both times from VM code: first with the literal `english` (evno 7540, ret `0x1807f6976`), then with `ISteamApps::GetCurrentGameLanguage()` (evno 7671, ret `0x180802587`). With language `french` the second call asks for `locale\french.json`,
`GetFileAttributesExW` fails, and the missing-with-fallback path runs (`GetModuleHandleExW(FROM_ADDRESS, 0x180052ef0)`, `CreateThread(0x1800f9104, ...)`, `CloseHandle` right away, no wait), after which startup continues normally and still returns/loops. The warning box itself runs on that thread, which the harness does not execute, so its text was not
captured (the caller does not wait for it; S). With `english` the file is read twice (once per call).
- **Key table (category, id) -> key (C, read from the live std::map<int, map<int, std::string>> nodes; the map layout is node+0x20 key, +0x28 value, inner map head held at outer_node+0x28):**

| category | id (dec / hex) | key |
|---|---|---|
| 200 | 336905 / 0x52409 | `MODMESSAGE_YUINEXUSPLAYERCOUNTREMAINING` |
| 200 | 130409 / 0x1fd69 | `MODMESSAGE_YUINEXUSBEGINONEPLAYER` |
| 200 | 130417 / 0x1fd71 | `MODMESSAGE_YUINEXUSBEGINTWOPLAYER` |
| 200 | 130408 / 0x1fd68 | `MODMESSAGE_YUINEXUSBEGINTHREEPLAYER` |
| 15000 | 0 | `MODMESSAGE_YUINEXUSNOSEAMLESS` |
| 15000 | 1 | `MODMESSAGE_YUINEXUSWRONGSSR` |
| 15000 | 2 | `MODMESSAGE_YUINEXUSPAUSED` |
| 15000 | 3 | `MODMESSAGE_YUINEXUSRESUMED` |

  Category 200 / id 0x52409 agrees with the override of localisation.md 4. Categories 200 ids are game message ids the override replaces; category 15000 ids 0-3 are mod-own messages (S). The keys are read once each from `.rdata` by two VM sites (evno 7832-7948, order BEGINONE, BEGINTWO, BEGINTHREE, PLAYERCOUNT, NOSEAMLESS, WRONGSSR, PAUSED, RESUMED; the two reads per key build the key table and its copy).
- **Fixed texts (C, same method), via `0x18000b5f0` (UTF-8 to wstring) and the map inserts `0x18004ac10/0x18004b0e0/0x18004b250/0x18004bea0/0x18004c0d0`:** (200, 401311 = 0x61f9f) = `Elden Ring: Nightreign Seamless Co-op v1.1.3 by Yui`; (203, 401401 = 0x61ff9) = the `Welcome to the seamless co-op mod...` text; (203, 401170 = 0x61f12) = `This mod uses a separate save, ...`.
  This resolves the placement U of localisation.md 5.
- **Triggered locale messages: none at startup (C).** `0x18000a1c0` (lookup) and `0x18001e1c0` (override) are never called in 80 M instructions; the key strings are only read to fill the table. Conditions are therefore **not observable** in this run. From the ids: the three BEGIN* texts replace game strings (category 200) and PLAYERCOUNTREMAINING is the `N / N` override;
  NOSEAMLESS / WRONGSSR are shown on a failed join (host without the seamless tag / different version, matching the readable lobby-tag code; W), PAUSED / RESUMED on a pause toggle (W). Which hook calls the lookup is **U**.

#### Settings and the "[ -- ]" secret

- The settings loader runs cleanly (rax 0, no fatal) on the real `nrsc_settings.ini`: out struct at `0x7ff0001ef2f8`: `+0x00 = 3`, `+0x04 = 1` (allow_invaders bit), `+0x30/+0x34/+0x38 = 1.0f x3` (scaling 100 -> 1.0), `+0x28 = 0xf`; the same 0x70 bytes are copied again at `+0x40`. Matches baseline.md 2 and `Preferences`. The save extension `co2` is accepted. (C)
- **The secret is built inside the settings loader and never leaves it in this run (C).** `"[ -- ]"` is not present in the file (not even XOR-free); the loader writes it (caller `0x1800394fe`/`0x18003d0eb`) into a 6-byte heap string (heap block 5107) and then into growing copies of 9, 13, 19 and 16 bytes (blocks 5108-5115, evno 5771-5809),
after a 16-byte buffer filled byte by byte by `0x1800393e0` (`e4 b9 9c 48 cd 02 23 97 0a c3 0f df 43 c4 7d a5`, W: key/IV or hash state) and a 0x80-byte table written by `0x180039599`. The only reads of the copies are inside the loader (memcpy from `0x1800394a3`). After the loader returns, none of the heap copies is read again
until the run ends (read watchers on all of them), and the out struct holds no pointer to them. So the secret is **not** consumed by startup code after the settings call (S for "no later use"; the stack copies at `0x7ff0001ee3f0`/`0x7ff0001edfd0` are reused by later calls, so reads of those addresses after the loader in `secret.reads` are stack noise). Where it ends up (it is presumably
folded into something the struct does not expose, e.g. a hash used by a hook) is **U**.

#### Remaining unknowns

- Who sets the stop flag, and what schedules the four tasks and calls their run slots (they are not called from this thread).
- What ids 0-2 are for (senders are protected), and whether the stack map is copied into the core (`+0x78/+0x88`).
- The 462 game-image writes, the three curl bodies (still no response modelled), the Warning box text on a missing language file, `ISteamUtils` slot 15 (called twice, args not shown), the meaning of owner fields `+0x30/+0x48/+0x70/+0x98`, of task fields `+0x20 = 0x98` / `+0x28 = game+0xfebc80`, and the secret's final consumer.
- Whether the real `GetModuleFileNameA(hModule)` / `GetFileAttributesEx` fidelity (UTF-8 vs ANSI path conversion `0x1800906a0`, stubbed to 1 = UTF-8) matters for non-ASCII install paths.

### 2.10 Task run slot `0x180061140` on the real state; stop flag of `0x180040630` (2026-10-16)

Tool `tools/realimage3.py` (imports `realimage2.py`, not edited), scenarios `scenarios/s5*.py`, results `out/v5/<scenario>.json` + `.events.jsonl`, `startup_blkrefs.json`.
Startup `0x180040630` runs as in 2.9 until its 3rd `Sleep(50)` (about 85 s), the registers are saved, and the process `fork()`s one child per scenario (copy-on-write, so every scenario starts from the same image).
A child plants values into the live objects, calls `vtable 0x180147b40 [+0x10]` = `0x180061140(task, data)` on a **second stack** (the main thread's frame holds core/owner/callbacks), `data+8` = float dt (1/30), N ticks, logging every read/write the VM does outside `.themida`, every exit, API, Steam call and packet. Game code is never executed: a fetch into `nightreign.exe` is logged (`game_call`) and returns 0.
Fix over realimage.py: Unicorn ignores a PC change made inside the fetch hook, so the driver stops and performs the return itself (the earlier "call to 0" fault in `k_press` was this).

**Scheduling (C).** Task slot `+0x18` = `0x180061040` = `jmp [this+0x28]` with `edx = [this+0x20]` (wiring_world.md); `+0x28` = `0x140febc80` (game `.text`, compares the group against `0x9b`), `+0x20` = `0x98`. The four tasks are registered with the game's task system in group `0x98`; `data` matches FD4TaskData (delta time at `+8`, S). This answers "what schedules the tasks".

**Result: `0x180061140` returns on every tick in all 31 scenarios** (rounds base, b-g; 30-120 ticks each). Branch conditions found (C for the emulation, S for names):

| Input (live object) | Effect |
|---|---|
| game globals `0x143c0f0a8` (slot 0x188), `0x143c21ec8` CSSessionManager (0x190), `0x143c21fc8` member owner (0x198), `0x143c078d0` GameDataMan (0x1b0), `0x143c13258` (0x1c8) | all null (as in the menu): only the timers run, nothing is sent |
| peer `+0x128` flag (bit 0 active) and peer `+0x8` SteamID, for the 5 pool peers | `recv_helper 0x180040370(peers.begin, peers.end, steamid, 1)` per active peer (a peer lookup, S) |
| timer `X+0x148 += dt`, reset at `>= 1.0f` (`0x180145d70`) | once per second: `send_control 0x18003f2c0` to every active peer = packet `03 01 00 01 00 00 00 b` (8 bytes, flags 9, Steam channel 26). `b` stayed 0 in all runs, also while paused (so it is not the F4 byte; source U) |
| `GetAsyncKeyState(0x73)` = **VK_F4**, polled once per tick (IAT `0x1801612b0`, which resolves the candidates `{EnumThreadWindows, GetAsyncKeyState, GetKeyboardState, GetSystemMetrics}` to **GetAsyncKeyState**, S: one int argument 0x73 is no valid SM_ index) | only the value `0x8001` (pressed + pressed-since-last-call) acts, `0x8000` (held) and `0x0001` do not: toggles a pause byte in the core frame (`0x7ff0001ef025`, 0->1, then 1->0) and calls the game function `0x14086a7a0` (signature slot 0xb8; uses the FD4Singleton at `0x143c149d8`, slot 0x1b8) with the locale repository `+0xb0` as 4th argument and a sequence value 1, 2 in it: the PAUSED/RESUMED on-screen message (S; text id not captured, the game code is not run) |
| CSSessionManager `+0x10` (dword) | `< 5`: data path runs (5 or 9 receive lookups per tick); `>= 5`: 2 lookups, no data send |
| member owner `+0x30/+0x38` = vector of 0xf0-byte entries, entry `+0x10` | **must hold a peer SteamID** (as a pointer nothing happens). Then `send_data 0x18003f030(core, steamid, buf, len)` every tick, twice per member: id 3 sub-channel **3**, 28-byte payload (packet `03 03 00 1c 00 00 00 ...`, 35 bytes) and id 3 sub-channel **4**, 504-byte payload (`03 04 00 f8 01 00 00 ...`, 511 bytes), flags 9, Steam channel 26, identity type 16 (SteamID). Payload built with the game function `0x14020bde0` (slot 0x60, called twice per tick, returns 0 here, so the payload is zeros except a dword 2 at payload +0x18 in sub-channel 3) |
| `WGame+0x9a50` dword != 0 with `+0x9a58` object (and `+0x9a58 -> +0x10`) | read and followed; no further boundary crossing seen (U) |
| `GameDataMan+0x8`, member `+0x100` (2nd entry +0x10) | read (pointers / SteamID) |

**Stop flag of `0x180040630` (C).** The wrapper `0x1800405c0` stores the constant 1 at its own `[rsp+0x20]` (`0x1800405df`) and passes only that address. At the park point the block address exists only in the thread's frame (`0x7ff0001ee590`, `0x7ff0001efd68`), in the VM context inside `.themida` and in `rdi`, nowhere in the heap or `.data` (`out/v5/startup_blkrefs.json`). So no other code can clear it: the routine is the lifetime of the mod thread and ends with the process. Forced (`resume_main`), it returns cleanly also after 35 ticks of full send/receive activity (`g_paused_hb`, `g_full_s4`, `base_resume`).

**Still open (U).** The meaning of the two data payloads (they come from game code that is not executed: sub-channel 3 = 28 B, sub-channel 4 = 504 B), the source of the heartbeat byte `b`, the exact text the F4 path shows, the `WGame+0x9a50` branch's purpose.

## 3. IAT ambiguity (`out/v2/iat_resolved.json`)

Before: **62** slots with more than one candidate (`iat_names.json`). After: **56 resolved uniquely** (32 C, 24 S), **5 best guess W**, **1 unresolved U** => 6 still ambiguous. Method: scan all 927 `call/jmp [IAT]` sites
(`out/v2/iat_usage.json`), read argument counts/constants/string widths (`std::wstring` SSO test `cmp [x+0x18], 7`, wide vs byte indexing of results), libcurl and UCRT source shapes, and the emulated VM trace
(HeapCreate(0,0,0), K32GetModuleInformation(…, 0x18), SteamAPI_RegisterCallback(obj, id)). The lld sort rule was re-checked per DLL after the choices (no order violations among named DLLs; WLDAP32 is by ordinal).
One slot (`0x1801607b0`, GetTimeFormatW) is **outside the old candidate list**: the old solver prefers prior names, so its lists are not exhaustive.
Best guesses: `0x180160560` CreateEventW, `0x180160700` GetFullPathNameW, `0x180160a48` GetAsyncKeyState, `0x180160ab0` ConvertStringSecurityDescriptorToSecurityDescriptorW, `0x180160ab8` CryptAcquireContextA. Unresolved: `0x180160b08` (OpenProcessToken > RegQueryValueExA > CryptSetKeyParam > CryptSetHashParam).

## 4. 0x180061140 fuzz result

60 random assignments of every scalar field read in the baseline (floats from {0,1,2,1.0,0.5,5.0,0.1,-NaN}, flags {0,1,2}, vector heads as pointers or 0) plus 0-3 elements: see `out/v2/0x180061140.fuzz_summary.json` (number of distinct behaviours) and `scenarios/s_61140.py`. Only the element-flag==1 branch produced new boundary crossings; several assignments (Y or C+0x28 chains made non-null with garbage) fault on the next pointer level, i.e. more of the object graph exists than planted.

## 5. Open unknowns

- ~~Real-game run of `0x180040630` and whether it ever returns~~: 2.8-2.10 (returns only when its flag is forced; nothing else can clear it).
- ~~Conditions that make `0x180061140` call `0x18003f030` / `0x180040370`~~: resolved in 2.10. Still U: payload meaning, heartbeat byte source.
- The URL and use of the curl fetch in `0x180040630`; whether the "Update required" check is version-based.
- Class owning vtable `0x180147898` (`0x180064120`).
- `--init` (DllMain CRT) still ends at rip 1; not needed so far.
- A faithful `--native all` run needs FLS/TLS-backed `_getptd`, a file API and the real stdio table.
