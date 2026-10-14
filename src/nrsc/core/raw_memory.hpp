#pragma once
// Unaligned access to game and file structures by byte offset. Game structures are described by documented offsets
// (docs/hooks.md section 9, docs/param_repository.md), not by C++ types, so every field read or write goes through
// memcpy: no alignment or strict-aliasing assumptions, the same bytes as the original's plain mov.
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace nrsc::raw {

template <class T>
T load(const void* base, std::size_t offset = 0) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof value);
    return value;
}

template <class T>
void store(void* base, std::size_t offset, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof value);
}

inline const void* load_ptr(const void* base, std::size_t offset) { return load<const void*>(base, offset); }

}  // namespace nrsc::raw
