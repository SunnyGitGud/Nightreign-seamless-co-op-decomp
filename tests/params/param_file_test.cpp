// nrsc\mod\param_repository\param_repository.cpp, row lookup (FUN_180035630).
#include "nrsc/params/param_file.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <vector>

#include "nrsc/core/fatal_error.hpp"

using namespace nrsc::params;
using nrsc::FatalError;

namespace {

// A synthetic loaded param file: 0x10 pre-header bytes, then the file. The index is placed at
// align16(size) as the code expects; offsets are written wherever the test asks.
class ParamImage {
public:
    explicit ParamImage(std::uint32_t size = 0x400) : size_(size), buf_(0x10 + 0x800) { put32(-0x10, size); }

    std::byte* file() { return buf_.data() + 0x10; }

    void put32(std::ptrdiff_t at, std::uint32_t v) { std::memcpy(file() + at, &v, 4); }
    void put64(std::ptrdiff_t at, std::uint64_t v) { std::memcpy(file() + at, &v, 8); }
    void format(std::uint8_t b2d, std::uint8_t b2e = 0) {
        file()[0x2d] = std::byte{b2d};
        file()[0x2e] = std::byte{b2e};
    }
    // Sorted (id, slot) pairs.
    void index(const std::vector<std::pair<std::uint32_t, std::int32_t>>& entries) {
        put32(-0x0c, static_cast<std::uint32_t>(entries.size()));
        const std::uint32_t at = (size_ + 15) & ~15u;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            put32(at + i * 8, entries[i].first);
            put32(at + i * 8 + 4, static_cast<std::uint32_t>(entries[i].second));
        }
    }

private:
    std::uint32_t size_;
    std::vector<std::byte> buf_;
};

}  // namespace

TEST(ParamFile, Format4With64BitRowTableFindsEveryRow) {
    ParamImage p(0x3f9);  // not a multiple of 16: index at 0x400
    p.format(4, 2);
    p.index({{10, 2}, {20, 0}, {30, 1}, {0x7fffffff, 3}});
    for (int slot = 0; slot < 4; ++slot) p.put64(0x48 + slot * 0x18, 0x100 + slot * 0x20);

    const auto r10 = find_row(p.file(), 10);
    EXPECT_EQ(r10.id, 10u);
    EXPECT_EQ(r10.row, p.file() + 0x140);
    EXPECT_EQ(find_row(p.file(), 20).row, p.file() + 0x100);
    EXPECT_EQ(find_row(p.file(), 30).row, p.file() + 0x120);
    EXPECT_EQ(find_row(p.file(), 0x7fffffff).row, p.file() + 0x160);
}

TEST(ParamFile, MissingIdLeavesNoRow) {
    ParamImage p;
    p.format(4, 2);
    p.index({{10, 0}, {30, 1}});
    for (const int id : {0, 9, 11, 29, 31, 1000}) {
        const auto r = find_row(p.file(), id);
        EXPECT_EQ(r.id, kNoRow) << id;
        EXPECT_EQ(r.row, nullptr) << id;
    }
}

TEST(ParamFile, NegativeIdIsNeverLookedUp) {
    ParamImage p;
    p.format(4, 2);
    p.index({{10, 0}, {0x80000000u, 1}});  // the key exists, but the id is negative as int32
    p.put64(0x48 + 0x18, 0x100);
    EXPECT_FALSE(find_row(p.file(), static_cast<std::int32_t>(0x80000000u)));
}

TEST(ParamFile, EmptyIndexOrSlotMinusOne) {
    ParamImage p;
    p.format(4, 2);
    p.index({});
    EXPECT_FALSE(find_row(p.file(), 0));
    p.index({{5, -1}});
    EXPECT_FALSE(find_row(p.file(), 5));
}

TEST(ParamFile, FormatSelectsWhereTheOffsetIsRead) {
    struct Case { std::uint8_t b2d, b2e; std::ptrdiff_t at; };
    // slot 2: *4 -> 0x3c / 0x4c; *0x18 -> 0x78
    const Case cases[] = {
        {2, 0, 0x34 + 8},    {0x82, 2, 0x34 + 8},  // only the low 7 bits pick the case
        {3, 0, 0x44 + 8},    {3, 2, 0x44 + 8},
        {4, 0, 0x44 + 8},    {4, 2, 0x48 + 0x30},
        {5, 2, 0x44 + 8},    // bit 7 clear: as format 3
        {0x85, 2, 0x48 + 0x30}, {0x85, 0, 0x44 + 8},  // bit 7 set: as format 4
    };
    for (const auto& c : cases) {
        ParamImage p;
        p.format(c.b2d, c.b2e);
        p.index({{7, 2}});
        p.put64(c.at, 0x200);
        const auto r = find_row(p.file(), 7);
        EXPECT_EQ(r.id, 7u) << int(c.b2d) << "/" << int(c.b2e);
        EXPECT_EQ(r.row, p.file() + 0x200) << int(c.b2d) << "/" << int(c.b2e);
    }
}

TEST(ParamFile, UnknownFormatsFindNothing) {
    for (const std::uint8_t f : {0, 1, 6, 0x7f, 0x81}) {
        ParamImage p;
        p.format(f, 2);
        p.index({{7, 0}});
        p.put64(0x48, 0x200);
        EXPECT_FALSE(find_row(p.file(), 7)) << int(f);
    }
}

// --- SoloParamRepository walk -------------------------------------------------------------------

namespace {

struct Repository {
    std::vector<std::byte> table = std::vector<std::byte>(0x80 + 0x48 * 4);
    std::vector<std::byte> holder = std::vector<std::byte>(0x88);
    std::vector<std::byte> inner = std::vector<std::byte>(0x88);

    void bind(int index, const std::byte* file) {
        const std::byte* nonnull = table.data();
        const std::byte* h = holder.data();
        const std::byte* i = inner.data();
        std::memcpy(table.data() + 0x80 + index * 0x48, &nonnull, 8);
        std::memcpy(table.data() + 0x88 + index * 0x48, &h, 8);
        std::memcpy(holder.data() + 0x80, &i, 8);
        std::memcpy(inner.data() + 0x80, &file, 8);
    }
};

}  // namespace

TEST(ParamRepository, WalksToTheParamFile) {
    ParamImage p;
    p.format(4, 2);
    p.index({{42, 0}});
    p.put64(0x48, 0x180);
    Repository repo;
    repo.bind(2, p.file());
    EXPECT_EQ(find_row_in_repository(repo.table.data(), 2, 42).row, p.file() + 0x180);
    EXPECT_FALSE(find_row_in_repository(repo.table.data(), 1, 42));  // entry not populated
}

TEST(ParamRepository, NullHolderFindsNothing) {
    Repository repo;
    const std::byte* nonnull = repo.table.data();
    std::memcpy(repo.table.data() + 0x80, &nonnull, 8);  // +0x80 set, +0x88 null
    EXPECT_FALSE(find_row_in_repository(repo.table.data(), 0, 1));
}

TEST(ParamRepository, NullRepositoryIsFatalLine6) {
    try {
        find_row_in_repository(nullptr, 0xf, 1);
        FAIL() << "expected FatalError";
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "SoloParamRepository");
        EXPECT_EQ(e.source_file(), "nrsc\\mod\\param_repository\\param_repository.cpp");
        EXPECT_EQ(e.line(), 6u);
    }
    EXPECT_NO_THROW(find_row_in_repository(nullptr, 0xf, -1));  // the id check comes first
}
