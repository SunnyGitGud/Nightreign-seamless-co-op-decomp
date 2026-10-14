# nrsc.dll dantelion\memory

Source path: `nrsc\dantelion\memory\memory.cpp` (`.rdata 0x18014c6a3`), with one reference at `0x180021611`.
Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.

## 1. Where it is

The memory code is **inlined at the end of the hook installer** `FUN_18001edb0` (`.text 0x18001edb0..0x180021749`,
10.6 KB; `evidence/pseudocode/0x18001edb0.c`, `ev_468cd6057f6cc5ce…`). Everything before it in that function is
`hooks.cpp`: a series of trampolines (count not yet established) (the `48 B8 … FF E0` stub writer, "out of memory" at `hooks.cpp` 0x2f6/0x367) and
exports resolved by XOR-decoded names through `0x161ad8`/`0x161b38` (for example one whose
stack literal reads `AddVecto` `redExcep` `…` `ler`, very likely `AddVectoredExceptionHandler`, **S**). That part belongs to the hooks module and is not covered here.

## 2. Behaviour (C)

At `0x180021204..0x180021626`:

1. `table = *(ctx + 0xb8)`: `table[0]` is an array of UTF-16 names and `table[1]` a parallel array of `u64` sizes.
2. Linear search `i = 0..28` (`cmp r12, 0x1c; ja → fatal`) for the name equal to `L"NETWORK"` (`.rdata 0x18014e23a`).
   Each name is copied into a `std::wstring`; equality = same length and the same characters.
3. Not found → `sprintf(buf[0x100], "Reference to engine memory block component \"%ws\" not found", L"NETWORK")`,
   fatal at `memory.cpp` line **0x13**.
4. Found → `VirtualProtect(sizes, 0x1d, PAGE_EXECUTE_READWRITE, &old)`; `sizes[i] = 0x1e00000` (**30 MiB**);
   `VirtualProtect(sizes, 0x1d, old, &old)`. Then the installer returns.

The protect length is **29 bytes**, not 29 × 8 (`mov edx, 0x1d` at `0x18002140d`/`0x180021431`). This works because
protection is per page; it would fail only if `sizes[i]` sat on a later page than `sizes[0]` (**C** as observed; that this
was intended is **U**).

What the NETWORK block is used for and its original size are **U** (the table belongs to the game). The 30 MiB value
fits a co-op mod that needs more network buffer space (**W**).

## 3. Reconstruction

`src/nrsc/dantelion/memory.*`, `tests/dantelion/memory_test.cpp` (5 tests). The table and `VirtualProtect` are injected.
`%ws` narrowing of non-ASCII characters is not reproduced (**W**); the component names are ASCII.
