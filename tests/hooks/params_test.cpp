// Params group detours (WP9): called on fake game objects and fake originals.
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <vector>

#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/signatures/module_image.hpp"

using namespace nrsc::hooks;
using namespace nrsc::hooks::params;

namespace {

ParamsState& fresh(const char* ext = "co2") {
    nrsc::config::Settings s;
    s.save_file_extension = ext;
    return reset_params_state(s);
}

// ---- fake originals ----
int g_calls = 0;
const char16_t* fake_text(void*, std::uint32_t, std::uint32_t, std::uint32_t id) {
    ++g_calls;
    return id == 1 ? u"game" : nullptr;
}
const char16_t* g_assign_ptr = nullptr;
std::uint64_t g_assign_len = 0;
void* fake_assign(void* self, const char16_t* p, std::uint64_t n) {
    ++g_calls;
    g_assign_ptr = p;
    g_assign_len = n;
    return self;
}
std::uint64_t fake_regbnd(void*, void*, void*) {
    ++g_calls;
    return 77;
}
void* fake_row(void* out, std::uint32_t) {
    ++g_calls;
    return out;
}
bool g_item_result = false;
bool fake_item(void*) {
    ++g_calls;
    return g_item_result;
}

struct Rows {
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::byte>> rows;
    void add(std::int32_t param, std::int32_t id, std::size_t size) { rows[{param, id}].assign(size, std::byte{0}); }
    void put_f(std::int32_t param, std::int32_t id, std::size_t off, float v) { std::memcpy(rows[{param, id}].data() + off, &v, 4); }
    void put_i(std::int32_t param, std::int32_t id, std::size_t off, std::int32_t v) { std::memcpy(rows[{param, id}].data() + off, &v, 4); }
    nrsc::params::RowSource source() {
        return [this](std::int32_t p, std::int32_t i) -> const std::byte* {
            auto it = rows.find({p, i});
            return it == rows.end() ? nullptr : it->second.data();
        };
    }
};

// Builds one scaling target 100 (rows 1000/1001), generated ids start at 0x7fff0000.
void setup_scaling(ParamsState& s, Rows& r, std::uint8_t kind = 2) {
    r.add(nrsc::params::kSpEffectParam, 1000, nrsc::params::kSpEffectRowSize);
    r.add(nrsc::params::kSpEffectParam, 1001, nrsc::params::kSpEffectRowSize);
    for (std::size_t off : {std::size_t{0x10}, std::size_t{0x38}, nrsc::params::kPostureField}) {
        r.put_f(nrsc::params::kSpEffectParam, 1000, off, 1.0f);
        r.put_f(nrsc::params::kSpEffectParam, 1001, off, 1.5f);
    }
    r.add(nrsc::params::kScalingParam, 100, 0x20);
    r.put_i(nrsc::params::kScalingParam, 100, 4, 1000);
    r.put_i(nrsc::params::kScalingParam, 100, 8, 1001);
    r.rows[{nrsc::params::kScalingParam, 100}][0x10] = std::byte{kind};
    s.regulation.target_ids = {100};
    s.row_source = r.source();
}

// Recording game helpers for #7.
struct Calls {
    int removed = 0, applied = 0, posted = 0, refreshed = 0;
    std::int32_t remove_id = 0, apply_id = 0;
    void* remove_arg = nullptr;
    void* apply_chr = nullptr;
    void* refresh_arg = nullptr;
} g_effect;
void rec_remove(void* l, std::int32_t id) { ++g_effect.removed; g_effect.remove_id = id; g_effect.remove_arg = l; }
void rec_apply(void* chr, std::int32_t id, void*, std::int32_t) { ++g_effect.applied; g_effect.apply_id = id; g_effect.apply_chr = chr; }
void rec_post(void*, float) { ++g_effect.posted; }
void rec_refresh(void* o, std::int32_t) { ++g_effect.refreshed; g_effect.refresh_arg = o; }

struct FakeChr {
    alignas(8) std::uint8_t bytes[0x300]{};
};

}  // namespace

TEST(ParamsStubs, Hooks2And3ReturnTrue) {
    EXPECT_TRUE(detour_eos_begin(nullptr, 1));
    EXPECT_TRUE(detour_eos_begin(nullptr, 2));
    EXPECT_TRUE(detour_leave_offline_flag(nullptr));
}

TEST(ParamsText, HandlerWinsThenMessagesThenOriginal) {
    ParamsState& s = fresh();
    s.original_text_lookup = reinterpret_cast<void*>(&fake_text);
    g_calls = 0;
    // original only
    EXPECT_EQ(std::u16string(detour_text_lookup(nullptr, 0, 5, 1)), u"game");
    EXPECT_EQ(g_calls, 1);
    EXPECT_EQ(detour_text_lookup(nullptr, 0, 5, 2), nullptr);

    // nrsc messages
    auto locale = std::make_shared<nrsc::locale::Locale>("en", nullptr);
    locale->load_json(R"({"HELLO":"hi"})");
    s.messages = std::make_shared<nrsc::locale::MessageRepository>(
        locale, std::vector<nrsc::locale::MessageKey>{{5, 1, "HELLO"}});
    g_calls = 0;
    EXPECT_EQ(std::u16string(detour_text_lookup(nullptr, 0, 5, 1)), u"hi");
    EXPECT_EQ(g_calls, 0);                                  // mod text replaces the game's
    EXPECT_EQ(std::u16string(detour_text_lookup(nullptr, 0, 6, 1)), u"game");  // not in the key table: original
    EXPECT_EQ(g_calls, 1);

    // handler for category 5 takes precedence over the messages
    s.text_handlers[5] = [](void*, std::uint32_t, std::uint32_t, std::uint32_t) { return u"handler"; };
    EXPECT_EQ(std::u16string(detour_text_lookup(nullptr, 0, 5, 1)), u"handler");
    EXPECT_EQ(std::u16string(detour_text_lookup(nullptr, 0, 6, 1)), u"game");
}

TEST(ParamsSave, ReplacesSl2LiteralWithConfiguredExtension) {
    ParamsState& s = fresh("abc");
    s.original_save_assign = reinterpret_cast<void*>(&fake_assign);
    int self = 0;
    g_calls = 0;
    EXPECT_EQ(detour_save_extension(&self, u".sl2", 4), &self);
    EXPECT_EQ(g_calls, 1);
    EXPECT_EQ(std::u16string(g_assign_ptr, g_assign_len), u".abc");
    EXPECT_EQ(g_assign_len, 4u);
    s.save_extension = u"co2";  // protected code may store the text without the dot (U)
    detour_save_extension(&self, u".sl2", 4);
    EXPECT_EQ(std::u16string(g_assign_ptr, g_assign_len), u"co2");
}

TEST(ParamsRows, RegbndLoadedCallsOriginalFirstThenBuildsOnce) {
    ParamsState& s = fresh();
    Rows rows;
    setup_scaling(s, rows);
    s.original_regbnd_loaded = reinterpret_cast<void*>(&fake_regbnd);
    g_calls = 0;
    EXPECT_FALSE(s.regulation.ready());
    EXPECT_EQ(detour_regbnd_loaded(nullptr, nullptr, nullptr), 77u);
    EXPECT_EQ(g_calls, 1);
    EXPECT_TRUE(s.regulation.ready());
    const std::uint32_t next = s.regulation.next_row_id();
    EXPECT_EQ(next, 0x7fff0000u + 10);
    detour_regbnd_loaded(nullptr, nullptr, nullptr);  // second load: no second build
    EXPECT_EQ(s.regulation.next_row_id(), next);
}

TEST(ParamsRows, RowGetterServesOverrideElseOriginal) {
    ParamsState& s = fresh();
    Rows rows;
    setup_scaling(s, rows);
    s.original_param_row = reinterpret_cast<void*>(&fake_row);
    s.original_regbnd_loaded = reinterpret_cast<void*>(&fake_regbnd);
    detour_regbnd_loaded(nullptr, nullptr, nullptr);

    alignas(8) std::uint8_t out[0x10]{};
    g_calls = 0;
    EXPECT_EQ(detour_param_row(out, 123), out);  // miss: original
    EXPECT_EQ(g_calls, 1);

    const std::uint32_t id = 0x7fff0000 + 2;
    EXPECT_EQ(detour_param_row(out, id), out);
    EXPECT_EQ(g_calls, 1);  // hit: original not called
    const std::byte* row;
    std::uint32_t got_id;
    std::memcpy(&row, out + layout::kRowRefRow, sizeof row);
    std::memcpy(&got_id, out + layout::kRowRefId, 4);
    EXPECT_EQ(got_id, id);
    EXPECT_EQ(out[layout::kRowRefTag], 2);
    float health;
    std::memcpy(&health, row + 0x10, 4);
    EXPECT_FLOAT_EQ(health, 1.0f + 0.5f * 3);  // n = 3
}

TEST(ParamsScaling, AppliesIdForPlayerCountAndStoresCount) {
    ParamsState& s = fresh();
    Rows rows;
    setup_scaling(s, rows);
    s.original_regbnd_loaded = reinterpret_cast<void*>(&fake_regbnd);
    detour_regbnd_loaded(nullptr, nullptr, nullptr);
    s.effects = {&rec_remove, &rec_apply, &rec_post, &rec_refresh};

    FakeChr chr;
    void* owner_slot = &g_effect;  // **(chr+0x1b8)
    void* owner_ptr = &owner_slot;
    std::memcpy(chr.bytes + layout::kChrOwner, &owner_ptr, sizeof owner_ptr);
    std::uint32_t row_ref[4] = {100, 0, 0, 0};

    for (unsigned players : {2u, 6u}) {
        g_effect = {};
        s.member_vector_bytes = [players] { return std::size_t{players} * 0xf0; };
        detour_scaling_effect(chr.bytes, row_ref);
        EXPECT_EQ(g_effect.applied, 1) << players;
        EXPECT_EQ(g_effect.apply_id, static_cast<std::int32_t>(0x7fff0000 + players - 2));
        EXPECT_EQ(g_effect.apply_chr, chr.bytes);
        EXPECT_EQ(g_effect.posted, 1);
        EXPECT_EQ(g_effect.refreshed, 1);
        EXPECT_EQ(g_effect.refresh_arg, owner_slot);
        std::uint32_t stored;
        std::memcpy(&stored, chr.bytes + layout::kChrScalingCount, 4);
        EXPECT_EQ(stored, players);  // record kind 2
    }
}

TEST(ParamsScaling, SingleOrUnknownRowDoesNothingSevenPlayersClears) {
    ParamsState& s = fresh();
    Rows rows;
    setup_scaling(s, rows);
    s.original_regbnd_loaded = reinterpret_cast<void*>(&fake_regbnd);
    detour_regbnd_loaded(nullptr, nullptr, nullptr);
    s.effects = {&rec_remove, &rec_apply, &rec_post, &rec_refresh};
    FakeChr chr;
    std::uint32_t row_ref[4] = {100, 0, 0, 0};

    g_effect = {};
    s.member_vector_bytes = [] { return std::size_t{0xf0}; };  // one player
    detour_scaling_effect(chr.bytes, row_ref);
    EXPECT_EQ(g_effect.applied + g_effect.removed, 0);

    s.member_vector_bytes = [] { return std::size_t{3 * 0xf0}; };
    std::uint32_t unknown[4] = {999, 0, 0, 0};  // no override for this row id
    detour_scaling_effect(chr.bytes, unknown);
    EXPECT_EQ(g_effect.applied + g_effect.removed, 0);

    void* list_ptr = reinterpret_cast<void*>(0x1234);
    std::memcpy(chr.bytes + layout::kChrSpEffectList, &list_ptr, sizeof list_ptr);
    std::uint32_t big = 7;  // 7 players: beyond the generated rows -> clear
    std::memcpy(chr.bytes + layout::kChrScalingCount, &big, 4);
    s.member_vector_bytes = [] { return std::size_t{7 * 0xf0}; };
    detour_scaling_effect(chr.bytes, row_ref);
    EXPECT_EQ(g_effect.removed, 1);
    EXPECT_EQ(g_effect.remove_id, 0x11a);
    EXPECT_EQ(g_effect.remove_arg, reinterpret_cast<void*>(0x1234));  // the pointer stored at chr+0x190 (asm 0x1800292b3)
    std::uint32_t stored = 1;
    std::memcpy(&stored, chr.bytes + layout::kChrScalingCount, 4);
    EXPECT_EQ(stored, 0u);
}

TEST(ParamsLoot, SettingByteOrOriginalOrOwnerBits) {
    ParamsState& s = fresh();
    s.original_item_visible = reinterpret_cast<void*>(&fake_item);
    std::array<std::uint8_t, layout::kSettingsBlockSize> block{};
    s.settings_block = block.data();
    alignas(8) std::uint8_t item[0x100]{};

    g_item_result = false;
    EXPECT_FALSE(detour_item_visible(item));  // mode 0: just the original
    g_item_result = true;
    EXPECT_TRUE(detour_item_visible(item));

    block[layout::kSettingsSharedLoot] = 1;  // config byte: always visible, original not needed
    g_item_result = false;
    g_calls = 0;
    EXPECT_TRUE(detour_item_visible(item));
    EXPECT_EQ(g_calls, 0);
    block[layout::kSettingsSharedLoot] = 0;

    block[layout::kSettingsMode] = 1;  // (W & 5) == 1
    EXPECT_FALSE(detour_item_visible(item));
    item[layout::kItemFlagsHi] = 0x80;
    EXPECT_TRUE(detour_item_visible(item));
    item[layout::kItemFlagsHi] = 0;
    item[layout::kItemFlagsLo] = 1;
    EXPECT_TRUE(detour_item_visible(item));
    block[layout::kSettingsMode] = 5;  // bit 2 as well: (W & 5) == 5, owner test off
    EXPECT_FALSE(detour_item_visible(item));
}

TEST(ParamsSpecs, SitesKindsAndOriginals) {
    ParamsState& s = fresh();
    const auto specs = build_specs([](unsigned off) { return std::uintptr_t{0x140000000} + off * 0x100; }, s);
    ASSERT_EQ(specs.size(), 8u);
    auto addr = [](unsigned off) { return std::uintptr_t{0x140000000} + off * 0x100; };
    struct Want { unsigned off; Kind kind; void** original; };
    const Want want[] = {
        {kSlotEosBegin, Kind::JmpEntry, nullptr},
        {kSlotLeaveOfflineCall, Kind::CallSite, nullptr},
        {kSlotTextLookup, Kind::Inline, &s.original_text_lookup},
        {kSlotSaveExtension, Kind::CallSite, &s.original_save_assign},
        {kSlotScalingEffect, Kind::JmpEntry, nullptr},
        {kSlotRegbndLoaded, Kind::CallSite, &s.original_regbnd_loaded},
        {kSlotParamRowGetter, Kind::Inline, &s.original_param_row},
        {kSlotMapItemVisible, Kind::Inline, &s.original_item_visible},
    };
    for (std::size_t i = 0; i < specs.size(); ++i) {
        EXPECT_EQ(specs[i].site, addr(want[i].off)) << specs[i].name;
        EXPECT_EQ(specs[i].kind, want[i].kind) << specs[i].name;
        EXPECT_EQ(specs[i].original, want[i].original) << specs[i].name;
        EXPECT_NE(specs[i].detour, nullptr) << specs[i].name;
        if (specs[i].kind == Kind::CallSite) {
            EXPECT_EQ(specs[i].expect, std::vector<std::uint8_t>{0xE8});
        }
    }
    EXPECT_EQ(specs[0].detour, reinterpret_cast<void*>(&detour_eos_begin));
    EXPECT_EQ(specs[1].detour, reinterpret_cast<void*>(&detour_leave_offline_flag));
    EXPECT_EQ(specs[0].expect.size(), 18u);
    EXPECT_EQ(specs[7].expect.size(), 13u);
    EXPECT_EQ(specs[7].expect.back(), 0xAA);
}


// ---------------------------------------------------------------------------------------------------------------
// wire(): state that the original's protected code fills (docs/wiring_params.md)
// ---------------------------------------------------------------------------------------------------------------
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

struct FakeSlots {
    std::map<unsigned, std::uint64_t> v;
    std::function<std::uint64_t(unsigned)> reader() {
        return [this](unsigned o) { auto it = v.find(o); return it == v.end() ? std::uint64_t{0} : it->second; };
    }
};

std::uint64_t addr(const void* p) { return reinterpret_cast<std::uint64_t>(p); }

fs::path temp_dir(const char* name) {
    fs::path d = fs::temp_directory_path() / (std::string("nrsc_wparams_") + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// A SoloParamRepository with one param (index 0x4f) holding ids {10, 20, 30(no row), 40}.
struct FakeRepo {
    alignas(16) std::uint8_t bytes[0x400]{};
    alignas(16) std::uint8_t file_block[0x400]{};
    std::uint8_t* file = nullptr;
    alignas(16) std::uint8_t repo[0x48 * 0x50 + 0x100]{};
    std::uint8_t holder[0x100]{}, inner[0x100]{};
    FakeRepo() {
        file = file_block + 0x20;  // header at file-0x10: size, file-0x0c: count
        const std::uint32_t size = 0x100, count = 4;
        std::memcpy(file - 0x10, &size, 4);
        std::memcpy(file - 0x0c, &count, 4);
        file[0x2d] = 4;  // format 4, row table 0x18 bytes when [0x2e]&2
        file[0x2e] = 2;
        std::uint8_t* index = file + 0x100;
        const std::uint32_t entries[4][2] = {{10, 0}, {20, 1}, {30, 0xffffffffu}, {40, 2}};
        std::memcpy(index, entries, sizeof entries);
        for (std::uint32_t slot = 0; slot < 3; ++slot) {  // row i at file+0x180+i*0x20
            const std::uint64_t off = 0x180 + slot * 0x20;
            std::memcpy(file + slot * 0x18 + 0x48, &off, 8);
        }
        std::uint8_t* entry = repo + 0x4f * 0x48;  // find_row_in_repository reads entry+0x80 and entry+0x88
        std::uint64_t one = 1, hp = addr(holder), ip = addr(inner), fp = addr(file);
        std::memcpy(entry + 0x80, &one, 8);
        std::memcpy(entry + 0x88, &hp, 8);
        std::memcpy(holder + 0x80, &ip, 8);
        std::memcpy(inner + 0x80, &fp, 8);
    }
};

}  // namespace

TEST(ParamsWire, UnresolvedSlotsKeepVanilla) {
    ParamsState& s = fresh();
    FakeSlots slots;  // everything zero
    nrsc::config::Settings cfg;
    wire_from_slots(slots.reader(), cfg, fs::path("/nonexistent/dir"));
    EXPECT_EQ(s.effects.remove_speffect, nullptr);
    EXPECT_EQ(s.effects.apply_speffect, nullptr);
    EXPECT_FALSE(s.scaling_wired);
    EXPECT_EQ(s.member_vector_bytes(), 0u);
    EXPECT_EQ(s.row_source(0x4f, 10), nullptr);
    EXPECT_FALSE(static_cast<bool>(s.target_source));
    EXPECT_EQ(s.messages, nullptr);  // no locale directory: game text
    // the session word starts solo (0); only the session layer sets 0x21 (hooks::set_party_session_active)
    EXPECT_FALSE(s.alternate_set());
    EXPECT_EQ(s.settings_block[layout::kSettingsSharedLoot], 0);
}

TEST(ParamsWire, FirstGeneratedRowIdIsLiveValue) {
    ParamsState& s = fresh();
    EXPECT_EQ(s.regulation.next_row_id(), 0x7fff0000u);
    EXPECT_EQ(s.regulation.next_row_id() + 80 * 10, 0x7fff0320u);  // live manager +0x14
}

TEST(ParamsWire, ModeWordDrivesAlternateSetAndLoot) {
    ParamsState& s = fresh();
    EXPECT_FALSE(s.alternate_set());  // before wire: zero block
    FakeSlots slots;
    wire_from_slots(slots.reader(), {}, "/nonexistent");
    std::uint32_t w;
    std::memcpy(&w, s.settings_block, 4);
    EXPECT_EQ(w, 0u);  // solo after wiring
    const std::uint32_t party = 0x21;  // what set_party_session_active(true) writes
    std::memcpy(const_cast<std::uint8_t*>(s.settings_block), &party, 4);
    EXPECT_TRUE(s.alternate_set());
    const std::uint32_t five = 5;  // bit 2 set: predicate false
    std::memcpy(const_cast<std::uint8_t*>(s.settings_block), &five, 4);
    EXPECT_FALSE(s.alternate_set());
    // P3 owner-bit test is active with the party session word
    wire_from_slots(slots.reader(), {}, "/nonexistent");
    std::memcpy(const_cast<std::uint8_t*>(s.settings_block), &party, 4);
    s.original_item_visible = reinterpret_cast<void*>(&fake_item);
    g_item_result = false;
    alignas(8) std::uint8_t item[0x100]{};
    EXPECT_FALSE(detour_item_visible(item));
    item[layout::kItemFlagsHi] = 0x80;
    EXPECT_TRUE(detour_item_visible(item));
}

TEST(ParamsWire, MemberVectorFromSessionManagerGlobal) {
    ParamsState& s = fresh();
    alignas(16) std::uint8_t mgr[0x100]{};
    std::uint64_t begin = 0x1000, end = 0x1000 + 3 * 0xf0;
    std::memcpy(mgr + 0x78, &begin, 8);
    std::memcpy(mgr + 0x80, &end, 8);
    std::uint64_t global = 0;  // the game global holding the manager pointer
    FakeSlots slots;
    slots.v[kSlotSessionManager] = addr(&global);
    wire_from_slots(slots.reader(), {}, "/nonexistent");
    EXPECT_EQ(s.member_vector_bytes(), 0u);  // singleton not created yet
    global = addr(mgr);
    EXPECT_EQ(s.member_vector_bytes(), 3u * 0xf0);
    EXPECT_EQ(nrsc::params::player_count_from_span(s.member_vector_bytes()), 3u);
    EXPECT_FALSE(s.scaling_wired);  // helpers and repository still missing
}

TEST(ParamsWire, EffectHelpersFromSlots) {
    ParamsState& s = fresh();
    FakeSlots slots;
    slots.v[kSlotRemoveSpEffect] = addr(reinterpret_cast<void*>(&rec_remove));
    slots.v[kSlotApplySpEffect] = 0x1403c80a0;  // never called here
    slots.v[kSlotPostApply] = addr(reinterpret_cast<void*>(&rec_post));
    slots.v[kSlotRefresh] = addr(reinterpret_cast<void*>(&rec_refresh));
    slots.v[kSlotSessionManager] = 0x10;
    slots.v[kSlotParamRepository] = 0x10;
    wire_from_slots(slots.reader(), {}, "/nonexistent");
    EXPECT_EQ(reinterpret_cast<void*>(s.effects.remove_speffect), reinterpret_cast<void*>(&rec_remove));
    EXPECT_EQ(reinterpret_cast<void*>(s.effects.post_apply), reinterpret_cast<void*>(&rec_post));
    EXPECT_EQ(reinterpret_cast<void*>(s.effects.refresh), reinterpret_cast<void*>(&rec_refresh));
    EXPECT_NE(s.effects.apply_speffect, nullptr);
    EXPECT_TRUE(s.scaling_wired);
}

TEST(ParamsWire, RowSourceAndTargetsFromRepositoryGlobal) {
    ParamsState& s = fresh();
    FakeRepo r;
    std::uint64_t repo_global = addr(r.repo);
    FakeSlots slots;
    slots.v[kSlotParamRepository] = addr(&repo_global);
    wire_from_slots(slots.reader(), {}, "/nonexistent");
    ASSERT_TRUE(static_cast<bool>(s.target_source));
    EXPECT_EQ(s.target_source(), (std::vector<std::int32_t>{10, 20, 40}));  // slot -1 skipped
    EXPECT_EQ(s.row_source(0x4f, 10), reinterpret_cast<const std::byte*>(r.file + 0x180));
    EXPECT_EQ(s.row_source(0x4f, 40), reinterpret_cast<const std::byte*>(r.file + 0x1c0));
    EXPECT_EQ(s.row_source(0x4f, 30), nullptr);
    EXPECT_EQ(s.row_source(0x4f, 99), nullptr);
    repo_global = 0;  // repository not created yet: no rows, no fatal error
    EXPECT_EQ(s.row_source(0x4f, 10), nullptr);
    EXPECT_TRUE(s.target_source().empty());
}

TEST(ParamsWire, RegbndLoadedFillsTargetsOnceThenBuilds) {
    ParamsState& s = fresh();
    s.original_regbnd_loaded = reinterpret_cast<void*>(&fake_regbnd);
    Rows r;
    // targets 100 and 200 each have rows; the same id list a real enumerator would return
    r.add(nrsc::params::kSpEffectParam, 1000, nrsc::params::kSpEffectRowSize);
    r.add(nrsc::params::kSpEffectParam, 1001, nrsc::params::kSpEffectRowSize);
    for (std::int32_t t : {100, 200}) {
        r.add(nrsc::params::kScalingParam, t, 0x20);
        r.put_i(nrsc::params::kScalingParam, t, 4, 1000);
        r.put_i(nrsc::params::kScalingParam, t, 8, 1001);
    }
    s.row_source = r.source();
    int calls = 0;
    s.target_source = [&] { ++calls; return std::vector<std::int32_t>{100, 200}; };
    EXPECT_EQ(detour_regbnd_loaded(nullptr, nullptr, nullptr), 77u);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(s.regulation.target_ids.size(), 2u);
    EXPECT_TRUE(s.regulation.ready());
    EXPECT_EQ(s.regulation.next_row_id(), 0x7fff0000u + 20);  // 2 targets x 10 ids
    detour_regbnd_loaded(nullptr, nullptr, nullptr);
    EXPECT_EQ(calls, 1);
}

TEST(ParamsWire, MessagesLoadedFromLocaleDir) {
    const fs::path dir = temp_dir("locale");
    fs::create_directories(dir / "locale");
    {
        std::ofstream(dir / "locale" / "english.json") << R"({"HELLO":"hi"})";
    }
    ParamsState& s = fresh();
    FakeSlots slots;
    wire_from_slots(slots.reader(), {}, dir);
    ASSERT_NE(s.messages, nullptr);
    EXPECT_FALSE(s.messages->lookup(5, 1).has_value());  // key table is U: nothing overridden
    // with a (fixture) key table the repository resolves text
    auto repo = load_message_repository(dir / "locale", {{5, 1, "HELLO"}});
    ASSERT_NE(repo, nullptr);
    EXPECT_EQ(repo->lookup(5, 1).value(), u"hi");
}

TEST(ParamsWire, BrokenOrMissingLocaleIsVanilla) {
    const fs::path dir = temp_dir("badlocale");
    EXPECT_EQ(load_message_repository(dir), nullptr);  // directory exists, no english.json
    std::ofstream(dir / "english.json") << "{ not json";
    EXPECT_EQ(load_message_repository(dir), nullptr);
}

// Optional: NRSC_GAME_EXE=<nightreign.exe v1.03.3>. The wired slots resolve to the addresses seen live (H+0x58..0x70,
// manager +0x170, session manager global) and the helpers have the prologue the detour's call shape assumes.
#include <cstdlib>
TEST(ParamsWireRealBinary, SlotsResolveToTheLiveAddresses) {
    const char* exe = std::getenv("NRSC_GAME_EXE");
    if (!exe || !*exe) GTEST_SKIP() << "NRSC_GAME_EXE not set";
    const auto image = nrsc::signatures::load_pe_image(exe);
    const auto table = nrsc::signatures::SignatureTable::build(image);
    EXPECT_EQ(table.at(kSlotRemoveSpEffect), 0x140504c40u);
    EXPECT_EQ(table.at(kSlotApplySpEffect), 0x1403c80a0u);
    EXPECT_EQ(table.at(kSlotPostApply), 0x1403e0ab0u);
    EXPECT_EQ(table.at(kSlotRefresh), 0x140428bf0u);
    EXPECT_EQ(table.at(kSlotSessionManager), 0x143c21ec8u);
    EXPECT_EQ(table.at(kSlotParamRepository), 0x143c28880u);
    EXPECT_EQ(table.at(kSlotScalingEffect), 0x1403db250u);
}
