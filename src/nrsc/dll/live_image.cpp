#include "nrsc/dll/live_image.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nrsc::dll {

namespace {

std::vector<signatures::ImageSection> read_sections(const IMAGE_NT_HEADERS64* nt) {
    std::vector<signatures::ImageSection> sections;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        signatures::ImageSection s;
        const auto* name = reinterpret_cast<const char*>(sec[i].Name);
        s.name.assign(name, strnlen(name, IMAGE_SIZEOF_SHORT_NAME));
        s.rva = sec[i].VirtualAddress;
        s.virtual_size = sec[i].Misc.VirtualSize;
        s.raw_size = sec[i].SizeOfRawData;
        s.characteristics = sec[i].Characteristics;
        sections.push_back(std::move(s));
    }
    return sections;
}

std::vector<std::uint8_t> copy_readable_pages(const std::uint8_t* base, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t off = 0; off < size;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(base + off, &mbi, sizeof mbi)) break;
        const std::size_t left_in_region = mbi.RegionSize - ((base + off) - static_cast<const std::uint8_t*>(mbi.BaseAddress));
        const std::size_t region = std::min<std::size_t>(left_in_region, size - off);
        const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0;
        if (readable) std::memcpy(bytes.data() + off, base + off, region);
        off += region;
    }
    return bytes;
}

}  // namespace

signatures::ModuleImage live_image(HMODULE module) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    std::vector<signatures::ImageSection> sections = read_sections(nt);
    std::vector<std::uint8_t> bytes = copy_readable_pages(base, nt->OptionalHeader.SizeOfImage);
    return signatures::ModuleImage(reinterpret_cast<std::uint64_t>(base), std::move(bytes), std::move(sections));
}

}  // namespace nrsc::dll
