# nrsc.dll cs modules: world_chr_man, bloodstain_man, auto_control_api

Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. Where the code is

All three are **hook detours** in the detour bank `.text 0x180026000..0x1800296ff` (`hooks.cpp`; the trampoline writer
`FUN_180026970` with "out of memory" at `hooks.cpp` 0x2f6/0x367 is also there). Their addresses are stored only by
protected code (for example `.themida 0x1808b6c04` → `FUN_180026830` and `0x1808b6c28` → `FUN_180026b30`), so the game
functions they replace are **U**. Each detour reaches the game through a context block (`param_1 + 0x70/0xa8/0x30`)
whose layout is set up in protected code.

| Module | Function | Evidence | Reconstruction |
|---|---|---|---|
| world_chr_man | `FUN_180026830` (PlayerIns), `FUN_180026b30` (NecroBuddy) | `ev_98e4828f51b29f17…` | `cs/world_chr_man.*` |
| bloodstain_man | `FUN_180027090` | `ev_6539e76aa5b08c41…` | `cs/bloodstain_man.*` |
| auto_control_api | `FUN_180028c20` | `ev_6539e76aa5b08c41…`, formatter `ev_8081374fb98b69ed…` | `cs/auto_control_api.*` |

## 2. world_chr_man — CSChrSet allocators (C)

Both detours do this:

1. WorldChrMan = `**(ctx + 0x20)`; if it is null → fatal `("WorldChrManImp", world_chr_man.cpp, 0x41 | 0x5e)`.
2. `header+0x08` = `*(*(WorldChrMan + 0x9910) + 0x120)` **+ 0** (PlayerIns) or **+ 2** (NecroBuddy).
3. `header+0x0c` = one 64-bit store, `0x6_ffffffff` or `0x78_ffffffff`: so `+0x0c = -1` and `+0x10 = capacity`.
4. `entries = allocator->vtbl[0x50](capacity * 0x10, 8)`, where allocator = `**(ctx + 0x28)`. If it is null → fatal
   `("Cannot create container for CSChrSet.PlayerIns!!" | "…NecroBuddy!!", …, 0x4f | 0x6c)`.
5. The entries are zeroed (each is `{u64, u16 @+8, u8 @+0xa}` in a 0x10-byte stride), then `header+0x18 = entries`.

| Set | Capacity | Index offset |
|---|---|---|
| PlayerIns | **6** | 0 |
| NecroBuddy | **120** (0x78) | 2 |

That six player slots exceed Nightreign's normal three is **S**: it matches the 2–6 player scaling in
`param_repository.md`. The game's own capacities are not in this file.

## 3. bloodstain_man — `FUN_180027090` (C)

`allocator = ctx(+0xa8)->[1](arg)`. If it is null → fatal with an **empty** message (`.rdata 0x18014dea8` is `""`) at
`bloodstain_man.cpp` line 0xe. Then `allocator->vtbl[0x68](arg)`, `block = allocator->vtbl[0x50](0x78, 0x10)`
(**not** null-checked), and the block is initialised and returned:

| Offset | Value |
|---|---|
| `+0x00..+0x17` | 0 |
| `+0x18` | `-1` (u64) |
| `+0x20..+0x2f` | 0 |
| `+0x30..+0x5f` | six × `0x00000000ffffffff` |
| `+0x60` | 0 |
| `+0x68` | **6** |
| `+0x70` | 0 |

Six slots again; reading them as one per player is **S**.

## 4. auto_control_api — `FUN_180028c20` (C)

With `c = ctx(+0x30)`:

1. For each task object at `c+0x2f8, +0x300, +0x308, +0x310`: if `vtbl[0x28]()` (is-registered) returns false, call `vtbl[0x18]()`.
2. If `game = **(c+8 → +0x20)` is non-null, `game+0x174e8` is non-null, `s = *(c+0x20 → [2])` is non-null and
   `s+0x58 == 0`, then:
   - call `(c+0x20)[3](1)`.
   - look up `"SignIn"` in the game's method table `*(c+0x20)[0]` (MSVC `unordered_map<std::string, …>`, FNV-1a
     over the bytes; the name is XOR-decoded: `0x96ac6f1c75e80e56 ^ 0x96ac01551b8f6705`).
   - if it is missing → fatal, with the message formatted into 0x100 bytes:
     `Method "SignIn" is not available. If you're using ModEngine, please make sure seamless co-op is loaded early. The game will now exit.`
     (`auto_control_api.cpp` line 0x14).
   - otherwise call the method with `0`. Then, if `*(c+0x58) + 0x48` is non-null, pass the UTF-16 notice
     `~ You may be running a pirated or cracked version of the game. … ~` (`.rdata 0x18014deaa`) to
     `(c+0x50)[7]`, with first argument `*(**(c+0x50)) + 0x878`.
3. Tail-call the original (`c+0x28 → +0x40`).

Note: the gate in step 2 is not one-shot in this function. Whether `s+0x58` changes after the call is **U**.
Truncation of an over-long method name is **W** (the reconstruction uses `snprintf`; the CRT variant behind
`FUN_1800eb278` was not identified).

## 5. Open (U)

- The hook targets, and the contents of the context blocks.
- The task objects at `c+0x2f8..+0x310` (what they are).
- What `game+0x174e8`, `s+0x58` and `*(c+0x58)+0x48` mean. The last one decides whether the notice is shown.
