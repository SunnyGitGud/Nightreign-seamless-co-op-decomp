#pragma once
// The scanned module: a base address plus the bytes of its mapped image, and a small read-only PE mapper.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nrsc::signatures {

struct ImageSection {
    std::string name;
    std::uint32_t rva = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t characteristics = 0;
};

// Stands in for the table's [base, size] pair (table[0], table[1]); the original takes them from the running game
// module, written by protected code (docs/signatures.md section 2: image range = whole mapped image, grade W).
class ModuleImage {
public:
    ModuleImage(std::uint64_t base, std::vector<std::uint8_t> bytes, std::vector<ImageSection> sections = {})
        : base_(base), bytes_(std::move(bytes)), sections_(std::move(sections)) {}

    std::uint64_t base() const noexcept { return base_; }
    std::uint64_t size() const noexcept { return bytes_.size(); }
    std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    const std::vector<ImageSection>& sections() const noexcept { return sections_; }

    bool contains(std::uint64_t va, std::uint64_t length = 1) const noexcept {
        return va >= base_ && length <= size() && va - base_ <= size() - length;
    }
    std::optional<std::uint32_t> read_u32(std::uint64_t va) const noexcept;
    std::optional<std::int32_t> read_i32(std::uint64_t va) const noexcept;
    std::optional<std::uint64_t> read_u64(std::uint64_t va) const noexcept;

private:
    std::uint64_t base_;
    std::vector<std::uint8_t> bytes_;
    std::vector<ImageSection> sections_;
};

// Maps a PE32+ file the way the loader would, without relocation or imports: headers at RVA 0, each section's
// raw data at its RVA (min(raw size, virtual size) bytes), everything else zero. Image size = SizeOfImage.
// Throws std::runtime_error on a malformed file. Not part of nrsc.dll (test and tooling helper).
ModuleImage map_pe_image(std::span<const std::uint8_t> file);
ModuleImage load_pe_image(const std::string& path);

}  // namespace nrsc::signatures
