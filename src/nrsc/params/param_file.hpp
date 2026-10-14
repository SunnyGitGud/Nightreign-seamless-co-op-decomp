#pragma once

#include <cstddef>
#include <cstdint>

// nrsc\mod\param_repository\param_repository.cpp: row lookup in the game's loaded param files
// (FUN_180035630, ev_76b0077905c56f760b45e31cdb51b1f87832a88a714f1c3ca842d2af02401c70). See
// docs/param_repository.md §2.
//
// The functions read raw game memory. Every read is a memcpy, so the buffers need no alignment.
namespace nrsc::params {

inline constexpr std::uint32_t kNoRow = 0xffffffffu;

// Out-parameter of FUN_180035630: {id, row}. `id` stays kNoRow and `row` null when nothing is found.
struct RowRef {
    std::uint32_t id = kNoRow;
    const std::byte* row = nullptr;

    explicit operator bool() const noexcept { return row != nullptr; }
};

// Look up row `id` in one loaded param file. `file` points at the param header. The 16 bytes before it
// must be readable too:
//   file - 0x10  int32   size used to place the index (S: the file size)
//   file - 0x0c  int32   number of index entries
// The sorted index {uint32 id, int32 slot} sits at file + ((size + 15) & ~15) and is binary-searched
// with an unsigned compare. A slot of -1 means no row. Header byte +0x2d (low 7 bits) selects where
// the 64-bit row offset is read from, and the row is file + offset:
//   2 -> file + slot*4 + 0x34
//   3 -> file + slot*4 + 0x44
//   4 -> (+0x2e & 2) ? file + slot*0x18 + 0x48 : file + slot*4 + 0x44
//   5 -> as 4 if bit 7 of +0x2d is set, else as 3
// Any other format means not found. Negative ids are never looked up. Offsets are computed in 32 bits,
// as in the original.
RowRef find_row(const std::byte* file, std::int32_t id);

// The SoloParamRepository part of FUN_180035630. Table entries are 0x48 bytes from +0x80. An entry is used
// only when its +0x80 and +0x88 are non-null. The file is *(*(entry[+0x88] + 0x80) + 0x80).
// A null repository is fatal: FatalError("SoloParamRepository", param_repository.cpp, 6). The check
// for a negative id comes first, as in the original.
RowRef find_row_in_repository(const std::byte* repository, std::int32_t param_index, std::int32_t id);

}  // namespace nrsc::params
