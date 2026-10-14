// Optional check against the real game: NRSC_GAME_EXE=<nightreign.exe v1.03.3>. Builds the signature table from the file,
// generates every HookSpec of the three groups and checks each site in the (unrelocated) image: `expect` bytes match,
// call/jmp sites hold E8/E9, vtable slots hold a pointer into the image, inline and entry targets are inside an executable
// section. It does not patch anything.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <set>

#include "nrsc/hooks/groups.hpp"
#include "nrsc/signatures/module_image.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace {

using namespace nrsc;

bool in_exec_section(const signatures::ModuleImage& img, std::uint64_t va) {
    for (const auto& s : img.sections()) {
        if ((s.characteristics & 0x20000000u) && va >= img.base() + s.rva && va < img.base() + s.rva + s.virtual_size) return true;
    }
    return false;
}

TEST(HooksRealBinary, EverySpecMatchesTheGameImage) {
    const char* exe = std::getenv("NRSC_GAME_EXE");
    if (!exe || !*exe) GTEST_SKIP() << "NRSC_GAME_EXE not set";
    const signatures::ModuleImage img = signatures::load_pe_image(exe);
    const signatures::SignatureTable table = signatures::SignatureTable::build(img);
    config::Settings settings;
    settings.save_file_extension = "co2";

    std::size_t total = 0;
    std::ofstream dump;
    if (const char* out = std::getenv("NRSC_HOOK_SITES_OUT")) dump.open(out);  // name,kind,site for cross-checks
    std::set<std::uintptr_t> sites;
    for (auto build : {&hooks::groups::world, &hooks::groups::params, &hooks::groups::net}) {
        for (const hooks::HookSpec& s : build(table, settings)) {
            ++total;
            if (dump) dump << s.name << ',' << int(s.kind) << ',' << std::hex << s.site << std::dec << '\n';
            SCOPED_TRACE(s.name);
            ASSERT_TRUE(img.contains(s.site, 8)) << std::hex << s.site;
            EXPECT_TRUE(sites.insert(s.site).second) << "site hooked twice";
            const auto at = [&](std::size_t i) { return img.bytes()[s.site - img.base() + i]; };
            for (std::size_t i = 0; i < s.expect.size(); ++i) EXPECT_EQ(at(i), s.expect[i]) << "expect byte " << i;
            switch (s.kind) {
            case hooks::Kind::CallSite: EXPECT_EQ(at(0), 0xE8); break;
            case hooks::Kind::JmpSite: EXPECT_EQ(at(0), 0xE9); break;
            case hooks::Kind::VtableSlot: {
                const auto v = img.read_u64(s.site);
                ASSERT_TRUE(v.has_value());
                EXPECT_TRUE(in_exec_section(img, *v)) << "slot value " << std::hex << *v;
                break;
            }
            case hooks::Kind::JmpEntry:
            case hooks::Kind::Inline:
            case hooks::Kind::BytePatch: EXPECT_TRUE(in_exec_section(img, s.site)); break;
            }
            if (s.kind != hooks::Kind::BytePatch) { EXPECT_NE(s.detour, nullptr); }
        }
    }
    EXPECT_EQ(total, 23u + 8u + 13u);  // world, params, net
}

}  // namespace
