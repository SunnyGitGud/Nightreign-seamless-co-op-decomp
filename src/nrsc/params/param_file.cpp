#include "nrsc/params/param_file.hpp"


#include "nrsc/core/fatal_error.hpp"
#include "nrsc/core/raw_memory.hpp"

namespace nrsc::params {

namespace {

constexpr const char* kSourceFile = "nrsc\\mod\\param_repository\\param_repository.cpp";

using raw::load;

}  // namespace

RowRef find_row(const std::byte* file, std::int32_t id) {
    RowRef out;
    if (id < 0) return out;

    const std::int32_t count = load<std::int32_t>(file - 0x0c);
    if (count < 1) return out;
    const std::uint32_t index_offset = (load<std::uint32_t>(file - 0x10) + 0xfu) & 0xfffffff0u;
    const std::byte* index = file + index_offset;
    const auto wanted = static_cast<std::uint32_t>(id);

    // 0x1800356c0: lo/hi are int32, compare is unsigned (jbe).
    std::int32_t lo = 0;
    std::int32_t hi = count - 1;
    std::int32_t mid;
    for (;;) {
        mid = (lo + hi) / 2;
        const auto key = load<std::uint32_t>(index + static_cast<std::uint32_t>(mid) * 8u);
        if (key == wanted) break;
        if (key > wanted) hi = mid - 1;
        else lo = mid + 1;
        if (hi < lo) return out;
    }
    const auto slot = load<std::uint32_t>(index + static_cast<std::uint32_t>(mid) * 8u + 4u);
    if (slot == 0xffffffffu) return out;

    const auto format = static_cast<std::uint8_t>(file[0x2d]);
    std::uint32_t offset_at;
    switch (format & 0x7f) {  // jump table 0x18003579c
    case 2:
        offset_at = slot * 4u + 0x34u;
        break;
    case 5:
        if (!(format & 0x80)) {
            offset_at = slot * 4u + 0x44u;
            break;
        }
        [[fallthrough]];
    case 4:
        offset_at = (static_cast<std::uint8_t>(file[0x2e]) & 2) ? slot * 0x18u + 0x48u : slot * 4u + 0x44u;
        break;
    case 3:
        offset_at = slot * 4u + 0x44u;
        break;
    default:
        return out;
    }
    out.id = wanted;
    out.row = file + load<std::uint64_t>(file + offset_at);
    return out;
}

RowRef find_row_in_repository(const std::byte* repository, std::int32_t param_index, std::int32_t id) {
    if (id < 0) return {};
    if (repository == nullptr) throw FatalError("SoloParamRepository", kSourceFile, 6);

    const std::byte* entry = repository + static_cast<std::uint32_t>(param_index * 0x48);
    if (load<const std::byte*>(entry + 0x80) == nullptr) return {};
    const auto* holder = load<const std::byte*>(entry + 0x88);
    if (holder == nullptr) return {};
    const auto* file = load<const std::byte*>(load<const std::byte*>(holder + 0x80) + 0x80);
    return find_row(file, id);
}

}  // namespace nrsc::params
