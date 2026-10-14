#include "nrsc/dantelion/block_table_locator.hpp"

#include <string>

namespace nrsc::dantelion {

namespace {

// UTF-16 string at `va` inside the image, up to 64 characters; nullopt if it runs out of the image.
std::optional<std::u16string> read_name(const signatures::ModuleImage& image, std::uint64_t va) {
    std::u16string s;
    for (std::size_t i = 0; i < 64; ++i) {
        if (!image.contains(va + i * 2, 2)) return std::nullopt;
        const auto lo = image.bytes()[va + i * 2 - image.base()];
        const auto hi = image.bytes()[va + i * 2 + 1 - image.base()];
        const char16_t c = static_cast<char16_t>(lo | (hi << 8));
        if (c == 0) return s;
        s.push_back(c);
    }
    return std::nullopt;
}

}  // namespace

std::optional<BlockTableAddresses> locate_block_table(const signatures::ModuleImage& image,
                                                      const signatures::SignatureTable& table) {
    return validate_block_table(image, table.at(kSlotBlockNames));
}

std::optional<BlockTableAddresses> validate_block_table(const signatures::ModuleImage& image, std::uint64_t names) {
    if (names == 0 || (names & 7) != 0) return std::nullopt;
    if (!image.contains(names, kBlockArrayLength * 16)) return std::nullopt;
    bool network = false;
    for (std::size_t i = 0; i < kBlockArrayLength; ++i) {
        const auto ptr = image.read_u64(names + i * 8);
        if (!ptr) return std::nullopt;
        const auto name = read_name(image, *ptr);
        if (!name || name->empty()) return std::nullopt;
        if (i == 0 && *name != u"MAIN") return std::nullopt;
        if (*name == kNetworkBlock) network = true;
    }
    if (!network) return std::nullopt;
    return BlockTableAddresses{names, names + kSizesFromNames};
}

const MemoryBlockTable* find_block_table(const signatures::ModuleImage& image,
                                         const signatures::SignatureTable& table) {
    const auto a = locate_block_table(image, table);
    if (!a) return nullptr;
    static MemoryBlockTable result;
    result.names = reinterpret_cast<const char16_t* const*>(static_cast<std::uintptr_t>(a->names));
    result.sizes = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(a->sizes));
    return &result;
}

}  // namespace nrsc::dantelion
