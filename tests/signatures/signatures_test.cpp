// nrsc\signatures\signatures.cpp: pattern parser (FUN_18002bf30), scanner (FUN_18002c340, FUN_18007cb60),
// resolve modes (FUN_18007c8f0), RTTI vtable lookup (FUN_18007d380/FUN_18007d520), table data and builder.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/signatures/module_image.hpp"
#include "nrsc/signatures/pattern.hpp"
#include "nrsc/signatures/resolver.hpp"
#include "nrsc/signatures/signature_table.hpp"

using namespace nrsc::signatures;
using nrsc::FatalError;

namespace {

constexpr std::uint64_t kBase = 0x140000000ull;

using Bytes = std::vector<std::uint8_t>;

Pattern P(std::string_view text) {
    auto p = parse_pattern(text);
    EXPECT_TRUE(p.has_value()) << text;
    return p.value_or(Pattern{});
}

Bytes B(std::initializer_list<int> v) {
    Bytes out;
    for (int x : v) out.push_back(static_cast<std::uint8_t>(x));
    return out;
}

// Image of `size` zero bytes with `bytes` placed at `at`.
ModuleImage image_with(std::size_t size, std::size_t at, const Bytes& bytes) {
    Bytes data(size, 0);
    std::copy(bytes.begin(), bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(at));
    return ModuleImage(kBase, std::move(data));
}

void put32(Bytes& d, std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); }
void put64(Bytes& d, std::size_t at, std::uint64_t v) { std::memcpy(d.data() + at, &v, 8); }

template <typename F>
FatalError expect_fatal(F&& f) {
    try {
        f();
    } catch (const FatalError& e) {
        return e;
    }
    ADD_FAILURE() << "expected FatalError";
    return FatalError("", "", 0);
}

}  // namespace

// ---------------------------------------------------------------- parser

TEST(SignaturePattern, ExactBytesAndWildcards) {
    const Pattern p = P("48 8b ?? 05 ? 0F");
    EXPECT_EQ(p.value, B({0x48, 0x8B, 0, 0x05, 0, 0x0F}));
    EXPECT_EQ(p.mask, B({0xFF, 0xFF, 0, 0xFF, 0, 0xFF}));
    EXPECT_EQ(p.effective_length, 6u);
}

TEST(SignaturePattern, NibbleWildcards) {
    const Pattern p = P("4? ?B ??");
    EXPECT_EQ(p.value, B({0x40, 0x0B, 0x00}));
    EXPECT_EQ(p.mask, B({0xF0, 0x0F, 0x00}));
    EXPECT_EQ(p.effective_length, 2u);  // trailing ?? is not compared
}

TEST(SignaturePattern, SingleDigitIsExactByte) {
    const Pattern p = P("A 5 ?");
    EXPECT_EQ(p.value, B({0x0A, 0x05, 0x00}));
    EXPECT_EQ(p.mask, B({0xFF, 0xFF, 0x00}));
}

TEST(SignaturePattern, ExtraAndTrailingSpacesAreIgnored) {
    // "BA 04  00 00 00" and "... 4C 8B 00 " appear in the real table.
    const Pattern a = P("BA 04  00 00 00");
    EXPECT_EQ(a.value, B({0xBA, 4, 0, 0, 0}));
    const Pattern b = P("4C 8B 00 ");
    EXPECT_EQ(b.value, B({0x4C, 0x8B, 0}));
    EXPECT_EQ(b.mask.size(), 3u);
}

TEST(SignaturePattern, MaskSuffix) {
    const Pattern p = P("48&F0 5&F 3C&0F");
    EXPECT_EQ(p.value, B({0x40, 0x05, 0x0C}));
    EXPECT_EQ(p.mask, B({0xF0, 0x0F & 0xFF, 0x0F}));
    // "5&F": single hex digit mask is 0x0F, so the value 05 stays 05 and the mask is 0F.
    EXPECT_EQ(p.value[1], 0x05);
}

TEST(SignaturePattern, MaskIsAppliedToWildcardNibbles) {
    const Pattern p = P("4?&3C");
    EXPECT_EQ(p.mask, B({0x30}));
    EXPECT_EQ(p.value, B({0x40 & 0x30}));
}

TEST(SignaturePattern, Repeat) {
    const Pattern p = P("90#3 ??#2 11");
    EXPECT_EQ(p.value, B({0x90, 0x90, 0x90, 0, 0, 0x11}));
    EXPECT_EQ(p.mask, B({0xFF, 0xFF, 0xFF, 0, 0, 0xFF}));
    const Pattern q = P("AB&F0#2");
    EXPECT_EQ(q.value, B({0xA0, 0xA0}));
    EXPECT_EQ(q.mask, B({0xF0, 0xF0}));
}

TEST(SignaturePattern, Rejects) {
    for (const char* bad : {"ZZ", "4G", "48 -", "#", "48#", "48#0", "48#x", "48&", "48&G", "48 #3", "&FF", "48\t8B"})
        EXPECT_FALSE(parse_pattern(bad).has_value()) << bad;
    EXPECT_FALSE(parse_pattern("90#2000000").has_value());  // implementation limit (W)
}

TEST(SignaturePattern, EmptyAndSpacesOnlyAreEmptyPatterns) {
    for (const char* t : {"", "   "}) {
        const auto p = parse_pattern(t);
        ASSERT_TRUE(p.has_value());
        EXPECT_TRUE(p->empty());
        EXPECT_FALSE(find_first(*p, Bytes(16, 0)).has_value());
    }
}

TEST(SignaturePattern, EveryTablePatternParses) {
    for (const SignatureEntry& e : kSignatureEntries) {
        if (e.kind == EntryKind::Rtti) continue;
        const auto p = parse_pattern(e.text);
        ASSERT_TRUE(p.has_value()) << e.text;
        EXPECT_GT(p->effective_length, 0u) << e.text;
    }
    EXPECT_TRUE(parse_pattern(kInstallerPatternA).has_value());
    EXPECT_TRUE(parse_pattern(kInstallerPatternB).has_value());
}

// ---------------------------------------------------------------- scanner

TEST(SignatureScanner, FindFirstAndAll) {
    Bytes d(64, 0);
    d[10] = 0xE8; d[11] = 1; d[40] = 0xE8; d[41] = 2;
    const Pattern p = P("E8 ??");
    EXPECT_EQ(find_first(p, d), 10u);
    EXPECT_EQ(find_all(p, d), (std::vector<std::size_t>{10, 40}));
    EXPECT_FALSE(find_first(P("E8 03"), d).has_value());
}

TEST(SignatureScanner, OverlappingMatchesAdvanceByOne) {
    const Bytes d = B({0xAA, 0xAA, 0xAA, 0xAA, 0x00});
    EXPECT_EQ(find_all(P("AA AA"), d), (std::vector<std::size_t>{0, 1, 2}));
}

TEST(SignatureScanner, WindowBounds) {
    const Bytes d = B({0, 0, 0, 0x12, 0x34});
    EXPECT_EQ(find_first(P("12 34"), d), 3u);  // match ends exactly at the window end
    EXPECT_FALSE(find_first(P("12 34"), std::span<const std::uint8_t>(d).first(4)).has_value());
    EXPECT_EQ(find_first(P("00 12"), std::span<const std::uint8_t>(d).subspan(2)), 0u);
    EXPECT_FALSE(find_first(P("00 00 00 12 34 56"), d).has_value());  // longer than the window
    EXPECT_EQ(find_first(P("12"), std::span<const std::uint8_t>(d).subspan(3, 1)), 0u);
}

TEST(SignatureScanner, TrailingWildcardsStillNeedWindowRoom) {
    // effective_length is 1 but the pattern is 2 bytes long: the window must hold both.
    const Bytes d = B({0, 0, 0x11});
    EXPECT_FALSE(find_first(P("11 ??"), d).has_value());
    EXPECT_EQ(find_first(P("00 ??"), d), 0u);
    EXPECT_EQ(find_all(P("00 ??"), d), (std::vector<std::size_t>{0, 1}));
}

TEST(SignatureScanner, NibbleMasks) {
    const Bytes d = B({0x48, 0x4F, 0x5F, 0x4A});
    EXPECT_EQ(find_all(P("4?"), d), (std::vector<std::size_t>{0, 1, 3}));
    EXPECT_EQ(find_all(P("?F"), d), (std::vector<std::size_t>{1, 2}));
}

TEST(SignatureScanner, AllWildcardPatternNeverMatches) {
    EXPECT_FALSE(find_first(P("?? ??"), Bytes(8, 1)).has_value());
}

TEST(SignatureScanner, PatternWithoutExactByteBeforeTrailingWildcards) {
    const Bytes d = B({0x12, 0x34, 0x56});
    EXPECT_EQ(find_first(P("?2 ?4"), d), 0u);
    EXPECT_EQ(find_first(P("?? 3?"), d), 0u);
}

// ---------------------------------------------------------------- resolve modes

TEST(SignatureResolve, Mode0IsMatchPlusOffset) {
    const ModuleImage img = image_with(0x100, 0x20, B({0x48, 0x89, 0x5C, 0x24}));
    SignatureResolver r(img);
    EXPECT_EQ(r.resolve("48 89 5C 24", 0, 0), kBase + 0x20);
    EXPECT_EQ(r.resolve("89 5C", 5, 0), kBase + 0x21 + 5);
    EXPECT_EQ(r.lookups_succeeded(), 2u);
}

TEST(SignatureResolve, Mode1CallTarget) {
    Bytes d(0x200, 0);
    d[0x40] = 0xE8;
    put32(d, 0x41, 0x100);   // call +0x100: target = 0x40 + 5 + 0x100
    d[0x45] = 0xC6;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    EXPECT_EQ(r.resolve("E8 ?? ?? ?? ?? C6", 0, 1), kBase + 0x145);
}

TEST(SignatureResolve, Mode1NegativeDisplacementAndOffset) {
    Bytes d(0x200, 0);
    d[0x60] = 0x90;
    d[0x61] = 0xE8;
    put32(d, 0x62, static_cast<std::uint32_t>(-0x40));
    d[0x66] = 0x77;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    // match at 0x60, offset 1 -> A = 0x61, target = 0x61 + 5 - 0x40
    EXPECT_EQ(r.resolve("90 E8 ?? ?? ?? ?? 77", 1, 1), kBase + 0x61 + 5 - 0x40);
}

TEST(SignatureResolve, Mode2RipRelative) {
    Bytes d(0x200, 0);
    d[0x80] = 0x48; d[0x81] = 0x8B; d[0x82] = 0x05;
    put32(d, 0x83, 0x50);
    d[0x87] = 0x85;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    EXPECT_EQ(r.resolve("48 8B 05 ?? ?? ?? ?? 85", 0, 2), kBase + 0x80 + 7 + 0x50);
    // negative offset: match at 0x81, A = 0x80
    EXPECT_EQ(r.resolve("8B 05 ?? ?? ?? ?? 85", -1, 2), kBase + 0x80 + 7 + 0x50);
}

TEST(SignatureResolve, FirstMatchWinsWhenAmbiguous) {
    Bytes d(0x100, 0);
    d[0x10] = 0xAB; d[0x80] = 0xAB;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    EXPECT_EQ(r.resolve("AB", 0, 0), kBase + 0x10);
}

TEST(SignatureResolve, NotFoundReportsCounterAndLine1000) {
    const ModuleImage img = image_with(0x100, 0x10, B({0x11, 0x22}));
    SignatureResolver r(img);
    EXPECT_EQ(r.resolve("11 22", 0, 0), kBase + 0x10);
    EXPECT_EQ(r.resolve("22", 0, 0), kBase + 0x11);
    const FatalError e = expect_fatal([&] { r.resolve("99 99", 0, 0); });
    EXPECT_STREQ(e.what(), "Failed to find signature: 2");
    EXPECT_EQ(e.source_file(), "nrsc\\signatures\\signatures.cpp");
    EXPECT_EQ(e.line(), 1000u);
    EXPECT_EQ(r.lookups_succeeded(), 2u);  // a failure does not increment
}

TEST(SignatureResolve, NotFoundWithZeroSuccesses) {
    const ModuleImage img = image_with(0x40, 0, {});
    SignatureResolver r(img);
    const FatalError e = expect_fatal([&] { r.resolve("12", 0, 1); });
    EXPECT_STREQ(e.what(), "Failed to find signature: 0");
}

TEST(SignatureResolve, UnparsablePatternIsNotFound) {
    const ModuleImage img = image_with(0x40, 0, B({0x12}));
    SignatureResolver r(img);
    const FatalError e = expect_fatal([&] { r.resolve("12 ZZ", 0, 0); });
    EXPECT_STREQ(e.what(), "Failed to find signature: 0");
    EXPECT_EQ(e.line(), 1000u);
}

TEST(SignatureResolve, InvalidModeIsFatalLine993) {
    const ModuleImage img = image_with(0x40, 0x8, B({0x12, 0x34}));
    SignatureResolver r(img);
    for (int mode : {3, 7, -1}) {
        const FatalError e = expect_fatal([&] { r.resolve("12 34", 0, mode); });
        EXPECT_STREQ(e.what(), "Not a valid signature type");
        EXPECT_EQ(e.source_file(), "nrsc\\signatures\\signatures.cpp");
        EXPECT_EQ(e.line(), 993u);
    }
    EXPECT_EQ(r.lookups_succeeded(), 0u);
}

TEST(SignatureResolve, InvalidModeWithoutMatchReportsNotFoundFirst) {
    // The scan runs before the mode switch (asm 0x18007c997 then 0x18007c9ad).
    const ModuleImage img = image_with(0x40, 0, {});
    SignatureResolver r(img);
    const FatalError e = expect_fatal([&] { r.resolve("12 34", 0, 9); });
    EXPECT_EQ(e.line(), 1000u);
}

// ---------------------------------------------------------------- find-all

namespace {
ModuleImage three_hits() {
    Bytes d(0x100, 0);
    for (std::size_t at : {0x10u, 0x30u, 0x70u}) { d[at] = 0xC3; d[at + 1] = 0x5A; }
    return ModuleImage(kBase, std::move(d));
}
}  // namespace

TEST(SignatureFindAll, ExactlyFull) {
    const ModuleImage img = three_hits();
    SignatureResolver r(img);
    std::array<std::uint64_t, 3> slots{};  // the 3-slot entry 0x180073610 (0x438/0x440/0x448, line 695)
    r.find_all("C3 5A", 3, slots, 695);
    EXPECT_EQ(slots[0], kBase + 0x13);
    EXPECT_EQ(slots[1], kBase + 0x33);
    EXPECT_EQ(slots[2], kBase + 0x73);
    EXPECT_EQ(r.lookups_succeeded(), 0u);  // find-all does not touch the counter
}

TEST(SignatureFindAll, OverflowIsFatalWithPerEntryLine) {
    const ModuleImage img = three_hits();
    SignatureResolver r(img);
    std::array<std::uint64_t, 2> slots{};
    const FatalError e = expect_fatal([&] { r.find_all("C3 5A", 0, slots, 488); });
    EXPECT_STREQ(e.what(), "incompatible game version: inconsistent pattern result");
    EXPECT_EQ(e.source_file(), "nrsc\\signatures\\signatures.cpp");
    EXPECT_EQ(e.line(), 488u);
    EXPECT_EQ(slots[0], kBase + 0x10);  // slots were filled before the fatal
    const FatalError e2 = expect_fatal([&] { r.find_all("C3 5A", 0, slots, 840); });
    EXPECT_EQ(e2.line(), 840u);
}

TEST(SignatureFindAll, ThreeSlotsOverflowOnFourth) {
    Bytes d(0x100, 0);
    for (std::size_t at : {0x10u, 0x20u, 0x30u, 0x40u}) d[at] = 0xC3;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    std::array<std::uint64_t, 3> slots{};
    const FatalError e = expect_fatal([&] { r.find_all("C3", 3, slots, 695); });
    EXPECT_EQ(e.line(), 695u);
}

TEST(SignatureFindAll, FewerMatchesLeaveTheRestZeroAndZeroMatchesAreSilent) {
    const ModuleImage img = three_hits();
    SignatureResolver r(img);
    std::array<std::uint64_t, 4> slots{};
    r.find_all("C3 5A", 0, slots, 0);
    EXPECT_EQ(slots[2], kBase + 0x70);
    EXPECT_EQ(slots[3], 0u);
    std::array<std::uint64_t, 2> none{};
    EXPECT_NO_THROW(r.find_all("DE AD", 0, none, 521));
    EXPECT_EQ(none[0], 0u);
    EXPECT_EQ(none[1], 0u);
}

TEST(SignatureFindAll, EntryWithoutFatalLineIgnoresExtraMatches) {
    Bytes d(0x100, 0);
    for (std::size_t at = 0x10; at < 0x10 + 6; ++at) d[at] = 0xC3;
    const ModuleImage img(kBase, d);
    SignatureResolver r(img);
    std::array<std::uint64_t, 4> slots{};
    EXPECT_NO_THROW(r.find_all("C3", 0, slots, 0));  // 0x180073d5f: 4 slots, no fatal path
    EXPECT_EQ(slots[3], kBase + 0x13);
}

// ---------------------------------------------------------------- RTTI

namespace {

constexpr std::size_t kImg = 0x1000;

struct RttiImage {
    Bytes data = Bytes(kImg, 0);

    void type_descriptor(std::size_t at, const char* name) {
        std::memcpy(data.data() + at + 0x10, name, std::strlen(name) + 1);
    }
    void col(std::size_t at, std::uint32_t td_rva, std::uint32_t self_rva, std::uint32_t signature = 1) {
        put32(data, at, signature);
        put32(data, at + 0x0c, td_rva);
        put32(data, at + 0x14, self_rva);
    }
    void slot(std::size_t at, std::uint64_t col_va) { put64(data, at, col_va); }
    ModuleImage image() const { return ModuleImage(kBase, data); }
};

}  // namespace

TEST(SignatureRtti, ValidColGivesVtableAfterSlot) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200);
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), kBase + 0x308);
}

TEST(SignatureRtti, WrongPSelfIsRejected) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x208);  // pSelf does not point back at the COL
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), 0u);
}

TEST(SignatureRtti, WrongSignatureIsRejected) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200, 0);  // x86 COL signature
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), 0u);
}

TEST(SignatureRtti, NameWithoutDotQuestionPrefixIsRejected) {
    RttiImage m;
    m.type_descriptor(0x100, "AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200);
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable("AVFoo@CS@@"), 0u);
}

TEST(SignatureRtti, MissingNameAndDifferentNameAreSilentZero) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200);
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    SignatureResolver r(img);
    EXPECT_EQ(r.find_vtable(".?AVBar@CS@@"), 0u);
    EXPECT_EQ(r.find_vtable(".?AVFoo@CS@"), 0u);   // prefix of the real name
    EXPECT_EQ(r.find_vtable(".?AVFoo@CS@@X"), 0u);
}

TEST(SignatureRtti, LowestSlotWinsAndSkipsInvalidOnesBeforeIt) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200);
    m.col(0x240, 0x100, 0x240);
    m.col(0x280, 0x100, 0x2c0);  // invalid, lowest slot points here
    m.slot(0x300, kBase + 0x280);
    m.slot(0x400, kBase + 0x240);
    m.slot(0x500, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), kBase + 0x408);
}

TEST(SignatureRtti, PointersOutsideTheImageAreIgnored) {
    RttiImage m;
    m.type_descriptor(0x100, ".?AVFoo@CS@@");
    m.col(0x200, 0x100, 0x200);
    m.slot(0x300, kBase + kImg - 0x10);  // COL would run past the end
    m.slot(0x308, kBase - 8);
    m.slot(0x310, 0xFFFFFFFFFFFFFFF0ull);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), 0u);
}

TEST(SignatureRtti, TypeDescriptorOutsideTheImageIsIgnored) {
    RttiImage m;
    m.col(0x200, 0x7fffffff, 0x200);
    m.slot(0x300, kBase + 0x200);
    const ModuleImage img = m.image();
    EXPECT_EQ(SignatureResolver(img).find_vtable(".?AVFoo@CS@@"), 0u);
}

// ---------------------------------------------------------------- PE mapper

TEST(SignatureModuleImage, MapsHeadersAndSections) {
    Bytes f(0x400, 0);
    f[0] = 'M'; f[1] = 'Z';
    put32(f, 0x3C, 0x80);
    put32(f, 0x80, 0x4550);
    const std::size_t coff = 0x84;
    f[coff + 2] = 1;                       // one section
    f[coff + 16] = 0xF0;                   // SizeOfOptionalHeader = 0xF0
    const std::size_t opt = 0x98;
    f[opt] = 0x0B; f[opt + 1] = 0x02;      // PE32+
    put64(f, opt + 24, kBase);
    put32(f, opt + 56, 0x3000);            // SizeOfImage
    put32(f, opt + 60, 0x200);             // SizeOfHeaders
    const std::size_t sec = opt + 0xF0;
    std::memcpy(f.data() + sec, ".text", 5);
    put32(f, sec + 8, 0x1000);             // VirtualSize
    put32(f, sec + 12, 0x1000);            // VirtualAddress
    put32(f, sec + 16, 0x100);             // SizeOfRawData
    put32(f, sec + 20, 0x300);             // PointerToRawData
    f[0x300] = 0xE8; f[0x3FF] = 0x77;
    const ModuleImage img = map_pe_image(f);
    EXPECT_EQ(img.base(), kBase);
    EXPECT_EQ(img.size(), 0x3000u);
    ASSERT_EQ(img.sections().size(), 1u);
    EXPECT_EQ(img.sections()[0].name, ".text");
    EXPECT_EQ(img.bytes()[0], 'M');
    EXPECT_EQ(img.bytes()[0x1000], 0xE8);
    EXPECT_EQ(img.bytes()[0x10FF], 0x77);
    EXPECT_EQ(img.bytes()[0x1100], 0);
    EXPECT_EQ(img.read_u32(kBase + 0x3C), 0x80u);
    EXPECT_FALSE(img.read_u32(kBase + 0x2FFE).has_value());
    EXPECT_THROW(map_pe_image(Bytes(16, 0)), std::runtime_error);
}

// ---------------------------------------------------------------- table data

namespace {
nlohmann::json load_evidence() {
    std::ifstream in(std::string(NRSC_EVIDENCE_DIR) + "/signatures/signatures.json");
    EXPECT_TRUE(in.good());
    return nlohmann::json::parse(in);
}
std::uint64_t hex(const std::string& s) { return std::stoull(s, nullptr, 16); }
}  // namespace

TEST(SignatureTable, EntryCountsAndKinds) {
    std::size_t counts[4] = {};
    for (const SignatureEntry& e : kSignatureEntries) ++counts[static_cast<int>(e.kind)];
    EXPECT_EQ(signature_entries().size(), 168u);
    EXPECT_EQ(counts[static_cast<int>(EntryKind::Find1)], 120u);
    EXPECT_EQ(counts[static_cast<int>(EntryKind::Inline)], 26u);
    EXPECT_EQ(counts[static_cast<int>(EntryKind::FindAll)], 7u);
    EXPECT_EQ(counts[static_cast<int>(EntryKind::Rtti)], 15u);
    // 168 table lookups plus the two hook-installer patterns.
    EXPECT_EQ(signature_entries().size() + 2, 170u);
    EXPECT_FALSE(kInstallerPatternA.empty());
    EXPECT_FALSE(kInstallerPatternB.empty());
}

TEST(SignatureTable, TableOffsetsAreUniqueAndInRange) {
    std::set<unsigned> seen;
    std::size_t slots = 0;
    for (const SignatureEntry& e : kSignatureEntries) {
        ASSERT_GE(e.slot_count, 1);
        if (e.kind != EntryKind::FindAll) { EXPECT_EQ(e.slot_count, 1); }
        for (unsigned i = 0; i < e.slot_count; ++i, ++slots) {
            EXPECT_TRUE(seen.insert(e.slots[i]).second) << std::hex << e.slots[i];
            EXPECT_GE(e.slots[i], kFirstSlotOffset);
            EXPECT_LE(e.slots[i], kLastSlotOffset);
            EXPECT_EQ(e.slots[i] % 8, 0u);
        }
    }
    EXPECT_EQ(slots, 161u + 17u);  // 146 byte patterns + 15 RTTI, plus the 17 find-all slots
}

TEST(SignatureTable, ExecutionOrderAndCounter) {
    unsigned seq = 0, order = 0;
    for (const SignatureEntry& e : kSignatureEntries) {
        EXPECT_EQ(e.exec_order, order++);
        if (e.kind == EntryKind::Find1 || e.kind == EntryKind::Inline) {
            EXPECT_EQ(e.exec_seq, ++seq);
            EXPECT_EQ(e.fatal_line, 1000u);
            EXPECT_LE(e.mode, 2);
        } else {
            EXPECT_EQ(e.exec_seq, 0u);
        }
    }
    EXPECT_EQ(seq, 146u);
}

TEST(SignatureTable, FindAllSlotGroups) {
    std::map<std::uint64_t, const SignatureEntry*> by_call;
    for (const SignatureEntry& e : kSignatureEntries) by_call[e.lookup_call] = &e;
    const SignatureEntry& three = *by_call.at(0x180073610);
    EXPECT_EQ(three.slot_count, 3);
    EXPECT_EQ(three.slots[0], 0x438);
    EXPECT_EQ(three.slots[1], 0x440);
    EXPECT_EQ(three.slots[2], 0x448);
    EXPECT_EQ(three.fatal_line, 695u);
    const SignatureEntry& four = *by_call.at(0x180073d5f);
    EXPECT_EQ(four.slot_count, 4);
    EXPECT_EQ(four.fatal_line, 0u);
    const std::map<std::uint64_t, unsigned> lines = {{0x18007140d, 488}, {0x180071842, 521}, {0x180073104, 641},
                                                    {0x180074293, 803}, {0x180074569, 840}};
    for (auto [call, line] : lines) {
        EXPECT_EQ(by_call.at(call)->kind, EntryKind::FindAll);
        EXPECT_EQ(by_call.at(call)->slot_count, 2);
        EXPECT_EQ(by_call.at(call)->fatal_line, line);
    }
}

TEST(SignatureTable, MatchesEvidenceJson) {
    const nlohmann::json j = load_evidence();
    ASSERT_EQ(j["entries"].size(), 168u);
    std::map<std::uint64_t, const SignatureEntry*> by_call;
    for (const SignatureEntry& e : kSignatureEntries) by_call[e.lookup_call] = &e;
    ASSERT_EQ(by_call.size(), 168u);
    for (const auto& je : j["entries"]) {
        const auto it = by_call.find(hex(je["lookup_call"]));
        ASSERT_NE(it, by_call.end()) << je["lookup_call"];
        const SignatureEntry& e = *it->second;
        const std::string kind = je["kind"];
        EXPECT_EQ(kind, e.kind == EntryKind::Find1 ? "find1" : e.kind == EntryKind::Inline ? "inline"
                  : e.kind == EntryKind::FindAll ? "findall" : "rtti");
        const std::string text = kind == "rtti" ? je["rtti_name"].get<std::string>() : je["pattern"].get<std::string>();
        EXPECT_EQ(std::string(e.text), text);
        ASSERT_EQ(je["table_offsets"].size(), e.slot_count);
        for (unsigned i = 0; i < e.slot_count; ++i) EXPECT_EQ(hex(je["table_offsets"][i]), e.slots[i]);
        if (kind == "find1" || kind == "inline") {
            EXPECT_EQ(je["offset"].get<int>(), e.offset);
            EXPECT_EQ(je["mode"].get<int>(), e.mode);
            EXPECT_EQ(je["exec_seq"].get<int>(), e.exec_seq);
        }
        if (kind == "findall") { EXPECT_EQ(je["offset"].get<int>(), e.offset); }
    }
    for (const auto& ip : j["installer_patterns"]) {
        if (!ip.contains("lookup_call")) continue;
        if (ip["lookup_call"] == "0x18001f060") { EXPECT_EQ(ip["pattern"].get<std::string>(), std::string(kInstallerPatternA)); }
        if (ip["lookup_call"] == "0x18001f14b") { EXPECT_EQ(ip["pattern"].get<std::string>(), std::string(kInstallerPatternB)); }
    }
}

TEST(SignatureTable, BuildRunsEntriesInOrderAndReportsCounter) {
    // Entry 1 (find1, E8 ?? ?? ?? ?? C6 83 BD 01 00 00, mode 1) succeeds; entry 2 is RTTI (silent); entry 3 fails.
    ASSERT_EQ(kSignatureEntries[0].exec_seq, 1u);
    ASSERT_EQ(kSignatureEntries[1].kind, EntryKind::Rtti);
    Bytes d(0x1000, 0);
    const Bytes first = B({0xE8, 0x10, 0, 0, 0, 0xC6, 0x83, 0xBD, 0x01, 0, 0, 0});
    std::copy(first.begin(), first.end(), d.begin() + 0x100);
    const ModuleImage img(kBase, d);
    const FatalError e = expect_fatal([&] { SignatureTable::build(img); });
    EXPECT_STREQ(e.what(), "Failed to find signature: 1");
    EXPECT_EQ(e.line(), 1000u);
    EXPECT_EQ(e.source_file(), "nrsc\\signatures\\signatures.cpp");
}

TEST(SignatureTable, BuildOnEmptyImageFailsAtFirstEntryWithCounterZero) {
    const ModuleImage img(kBase, Bytes(0x100, 0));
    const FatalError e = expect_fatal([&] { SignatureTable::build(img); });
    EXPECT_STREQ(e.what(), "Failed to find signature: 0");
}

// ---------------------------------------------------------------- optional real binary

TEST(SignatureRealBinary, ResolvesEveryEntryLikeTheEvidence) {
    const char* exe = std::getenv("NRSC_GAME_EXE");
    if (!exe || !*exe) GTEST_SKIP() << "NRSC_GAME_EXE not set";
    const ModuleImage img = load_pe_image(exe);
    const SignatureTable table = SignatureTable::build(img);
    EXPECT_EQ(table.lookups_succeeded(), 146u);

    const nlohmann::json j = load_evidence();
    std::size_t matched = 0, mismatched = 0, total = 0;
    auto check = [&](unsigned off, std::uint64_t expect_rva, bool expect_present, const std::string& what) {
        ++total;
        const std::uint64_t got = table.at(off);
        const bool ok = expect_present ? (got && got - img.base() == expect_rva) : got == 0;
        if (ok) ++matched;
        else {
            ++mismatched;
            ADD_FAILURE() << what << " slot 0x" << std::hex << off << ": got rva 0x" << (got ? got - img.base() : 0)
                          << " expected 0x" << (expect_present ? expect_rva : 0);
        }
    };
    for (const auto& je : j["entries"]) {
        const std::string kind = je["kind"];
        const std::string call = je["lookup_call"];
        if (kind == "findall") {
            for (const auto& [slot, va] : je["resolved_slots"].items()) {
                const bool present = !va.is_null();
                check(static_cast<unsigned>(hex(slot)), present ? hex(va.get<std::string>()) - img.base() : 0, present, call);
            }
        } else {
            ASSERT_TRUE(je.contains("resolved_rva")) << call;
            check(static_cast<unsigned>(hex(je["table_offsets"][0])), hex(je["resolved_rva"]), true, call);
        }
    }
    std::printf("[ real exe ] %zu of %zu slots match the evidence, %zu mismatches\n", matched, total, mismatched);
}
