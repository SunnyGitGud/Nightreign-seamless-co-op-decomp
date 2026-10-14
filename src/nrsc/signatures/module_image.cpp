#include "nrsc/signatures/module_image.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace nrsc::signatures {

namespace {
template <typename T>
std::optional<T> read_le(const ModuleImage& img, std::uint64_t va) noexcept {
    if (!img.contains(va, sizeof(T))) return std::nullopt;
    T v;
    std::memcpy(&v, img.bytes().data() + (va - img.base()), sizeof v);  // little-endian host assumed (x86-64)
    return v;
}

template <typename T>
T file_read(std::span<const std::uint8_t> f, std::size_t off) {
    if (off > f.size() || f.size() - off < sizeof(T)) throw std::runtime_error("PE: truncated file");
    T v;
    std::memcpy(&v, f.data() + off, sizeof v);
    return v;
}
}  // namespace

std::optional<std::uint32_t> ModuleImage::read_u32(std::uint64_t va) const noexcept { return read_le<std::uint32_t>(*this, va); }
std::optional<std::int32_t> ModuleImage::read_i32(std::uint64_t va) const noexcept { return read_le<std::int32_t>(*this, va); }
std::optional<std::uint64_t> ModuleImage::read_u64(std::uint64_t va) const noexcept { return read_le<std::uint64_t>(*this, va); }

ModuleImage map_pe_image(std::span<const std::uint8_t> file) {
    if (file_read<std::uint16_t>(file, 0) != 0x5A4D) throw std::runtime_error("PE: bad DOS signature");
    const std::size_t nt = file_read<std::uint32_t>(file, 0x3C);
    if (file_read<std::uint32_t>(file, nt) != 0x00004550) throw std::runtime_error("PE: bad NT signature");
    const std::size_t section_count = file_read<std::uint16_t>(file, nt + 6);
    const std::size_t optional_size = file_read<std::uint16_t>(file, nt + 20);
    const std::size_t opt = nt + 24;
    if (file_read<std::uint16_t>(file, opt) != 0x20B) throw std::runtime_error("PE: not PE32+");
    const std::uint64_t base = file_read<std::uint64_t>(file, opt + 24);
    const std::uint32_t image_size = file_read<std::uint32_t>(file, opt + 56);
    const std::uint32_t headers_size = file_read<std::uint32_t>(file, opt + 60);
    if (image_size == 0 || image_size > 0x40000000u) throw std::runtime_error("PE: implausible SizeOfImage");

    std::vector<std::uint8_t> image(image_size, 0);
    const std::size_t hdr = std::min<std::size_t>({headers_size, file.size(), image_size});
    std::memcpy(image.data(), file.data(), hdr);

    std::vector<ImageSection> sections;
    for (std::size_t i = 0; i < section_count; ++i) {
        const std::size_t o = opt + optional_size + i * 40;
        char name[9] = {};
        if (o > file.size() || file.size() - o < 40) throw std::runtime_error("PE: truncated section table");
        std::memcpy(name, file.data() + o, 8);
        ImageSection s;
        s.name = name;
        s.virtual_size = file_read<std::uint32_t>(file, o + 8);
        s.rva = file_read<std::uint32_t>(file, o + 12);
        s.raw_size = file_read<std::uint32_t>(file, o + 16);
        const std::uint32_t raw_ptr = file_read<std::uint32_t>(file, o + 20);
        s.characteristics = file_read<std::uint32_t>(file, o + 36);
        std::size_t n = s.virtual_size ? std::min(s.raw_size, s.virtual_size) : s.raw_size;
        if (s.rva > image_size) throw std::runtime_error("PE: section outside image");
        n = std::min<std::size_t>(n, image_size - s.rva);
        if (raw_ptr > file.size()) n = 0;
        n = std::min<std::size_t>(n, file.size() - std::min<std::size_t>(raw_ptr, file.size()));
        if (n) std::memcpy(image.data() + s.rva, file.data() + raw_ptr, n);
        sections.push_back(std::move(s));
    }
    return ModuleImage(base, std::move(image), std::move(sections));
}

ModuleImage load_pe_image(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    const auto size = static_cast<std::size_t>(in.tellg());
    std::vector<std::uint8_t> file(size);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error("cannot read " + path);
    return map_pe_image(file);
}

}  // namespace nrsc::signatures
