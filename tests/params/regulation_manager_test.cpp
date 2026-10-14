// nrsc\mod\param_repository\param_repository.cpp, YuiNexusRegulationManager
// (FUN_1800295e0, FUN_1800364b0, FUN_1800357b0, FUN_180029140).
#include "nrsc/params/regulation_manager.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "nrsc/core/fatal_error.hpp"

using namespace nrsc::params;
using nrsc::FatalError;
using nrsc::config::Preferences;

namespace {

float f32(const std::byte* p, std::size_t off) {
    float v;
    std::memcpy(&v, p + off, 4);
    return v;
}

class FakeParams {
public:
    std::vector<std::byte>& add(std::int32_t param, std::int32_t id, std::size_t size) {
        return rows_[{param, id}] = std::vector<std::byte>(size);
    }
    void spEffect(std::int32_t id, float health, float damage, float posture) {
        auto& r = add(kSpEffectParam, id, kSpEffectRowSize);
        std::memcpy(r.data() + 0x10, &health, 4);
        std::memcpy(r.data() + 0x38, &damage, 4);
        std::memcpy(r.data() + kPostureField, &posture, 4);
        r[0x200] = std::byte{0xab};  // marker: the rest of the row is copied from the base row
    }
    void scaling(std::int32_t id, std::int32_t base, std::int32_t full, std::uint8_t kind) {
        auto& r = add(kScalingParam, id, 0x20);
        std::memcpy(r.data() + 4, &base, 4);
        std::memcpy(r.data() + 8, &full, 4);
        r[0x10] = std::byte{kind};
    }
    RowSource source() const {
        return [this](std::int32_t param, std::int32_t id) -> const std::byte* {
            const auto it = rows_.find({param, id});
            return it == rows_.end() ? nullptr : it->second.data();
        };
    }

private:
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::byte>> rows_;
};

ScalingRecord record_of(const OverrideRef& ref) {
    ScalingRecord r;
    std::memcpy(&r, ref.data, sizeof r);
    return r;
}

constexpr std::uint32_t kFirstId = 5000;

}  // namespace

TEST(RegulationManager, BuildsFiveScaledRowsPerPassFromTheDelta) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 1.5f, 1.25f, 1.125f);
    params.scaling(100, 1000, 1001, 2);

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());

    EXPECT_TRUE(m.ready());
    EXPECT_EQ(m.next_row_id(), kFirstId + 10);  // build(0) and build(1)
    EXPECT_FLOAT_EQ(*m.health_delta(100), 0.5f);
    EXPECT_FLOAT_EQ(*m.damage_delta(100), 0.25f);
    EXPECT_FLOAT_EQ(*m.posture_delta(100), 0.125f);

    for (std::uint32_t k = 0; k < 10; ++k) {  // both passes put their rows into set 0
        const auto row = m.find_override(0, kSpEffectParam, kFirstId + k);
        ASSERT_TRUE(row) << k;
        EXPECT_EQ(row->id, kFirstId + k);
        EXPECT_EQ(row->tag, 2u);
        const float n = static_cast<float>(k % 5 + 1);
        for (const auto off : kHealthFields) EXPECT_FLOAT_EQ(f32(row->data, off), 1.0f + 0.5f * n) << off;
        for (const auto off : kDamageFields) EXPECT_FLOAT_EQ(f32(row->data, off), 1.0f + 0.25f * n) << off;
        EXPECT_FLOAT_EQ(f32(row->data, kPostureField), 1.0f + 0.125f * n);
        EXPECT_EQ(row->data[0x200], std::byte{0xab});
    }
    EXPECT_FALSE(m.find_override(1, kSpEffectParam, kFirstId));

    const auto r0 = m.find_override(0, kScalingParam, 100);
    const auto r1 = m.find_override(1, kScalingParam, 100);
    ASSERT_TRUE(r0 && r1);
    EXPECT_EQ(record_of(*r0).kind, 2);
    EXPECT_EQ(record_of(*r0).spEffect_ids, (std::array<std::int32_t, 5>{5000, 5001, 5002, 5003, 5004}));
    EXPECT_EQ(record_of(*r1).spEffect_ids, (std::array<std::int32_t, 5>{5005, 5006, 5007, 5008, 5009}));
}

TEST(RegulationManager, PreferencesScaleTheDelta) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 1.5f, 1.5f, 1.5f);
    params.scaling(100, 1000, 1001, 0);

    Preferences prefs;
    prefs.health_scaling = 2.0f;
    prefs.damage_scaling = 0.5f;
    prefs.posture_scaling = 0.0f;
    RegulationManager m(prefs, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());

    const auto row = m.find_override(0, kSpEffectParam, kFirstId + 2);  // n = 3
    ASSERT_TRUE(row);
    EXPECT_FLOAT_EQ(f32(row->data, 0x10), 1.0f + 2.0f * 0.5f * 3);
    EXPECT_FLOAT_EQ(f32(row->data, 0x38), 1.0f + 0.5f * 0.5f * 3);
    EXPECT_FLOAT_EQ(f32(row->data, kPostureField), 1.0f);
}

TEST(RegulationManager, ZeroHealthDeltaBecomesPointFifteen) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 1.0f, 1.0f, 1.0f);
    params.scaling(100, 1000, 1001, 0);

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());

    EXPECT_FLOAT_EQ(*m.health_delta(100), 0.15f);
    EXPECT_FLOAT_EQ(*m.damage_delta(100), 0.0f);  // only health gets the substitute
    const auto row = m.find_override(0, kSpEffectParam, kFirstId + 4);
    EXPECT_FLOAT_EQ(f32(row->data, 0x10), 1.0f + 0.15f * 5);
    EXPECT_FLOAT_EQ(f32(row->data, 0x38), 1.0f);
}

TEST(RegulationManager, PostureIsClampedToPointOne) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 1.5f, 1.0f, 0.5f);
    params.scaling(100, 1000, 1001, 0);

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());

    const float expected[] = {0.5f, 0.1f, 0.1f, 0.1f, 0.1f};  // 1 - 0.5n, at least 0.1
    for (int i = 0; i < 5; ++i)
        EXPECT_FLOAT_EQ(f32(m.find_override(0, kSpEffectParam, kFirstId + i)->data, kPostureField), expected[i]);
}

TEST(RegulationManager, MissingRowsSkipTheTarget) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.scaling(100, 1000, 1001, 0);  // full row 1001 missing
    params.scaling(101, 999, 1000, 0);   // base row 999 missing

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100, 101, 102};      // 102: no 0x4f row
    m.on_regulation_loaded(params.source());

    EXPECT_EQ(m.next_row_id(), kFirstId);
    EXPECT_FALSE(m.health_delta(100));
    EXPECT_FALSE(m.find_override(0, kScalingParam, 100));
}

TEST(RegulationManager, DuplicateTargetsGenerateRowsTwiceButKeepTheFirstRecord) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 2.0f, 2.0f, 2.0f);
    params.scaling(100, 1000, 1001, 0);

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100, 100};
    m.on_regulation_loaded(params.source());

    EXPECT_EQ(m.next_row_id(), kFirstId + 20);
    EXPECT_EQ(record_of(*m.find_override(0, kScalingParam, 100)).spEffect_ids[0], 5000);
    EXPECT_EQ(record_of(*m.find_override(1, kScalingParam, 100)).spEffect_ids[0], 5010);
}

TEST(RegulationManager, BuildsOnlyOnce) {
    FakeParams params;
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 2.0f, 2.0f, 2.0f);
    params.scaling(100, 1000, 1001, 0);

    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());
    m.on_regulation_loaded(params.source());
    EXPECT_EQ(m.next_row_id(), kFirstId + 10);
}

TEST(RegulationManager, NothingIsFoundBeforeReady) {
    const RegulationManager m(Preferences{}, kFirstId);
    EXPECT_FALSE(m.ready());
    EXPECT_FALSE(m.find_override(0, kSpEffectParam, kFirstId));
}

TEST(RegulationManager, BuildWithoutTheSpEffectMapIsFatal) {
    RegulationManager m(Preferences{}, kFirstId);
    try {
        m.build(0, FakeParams{}.source());
        FAIL() << "expected FatalError";
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "YuiNexusRegulationManager");
        EXPECT_EQ(e.line(), 0xb5u);
    }
}

// --- effect choice (FUN_180029140) ---------------------------------------------------------------

namespace {

RegulationManager built_manager(FakeParams& params, std::uint8_t kind) {
    params.spEffect(1000, 1.0f, 1.0f, 1.0f);
    params.spEffect(1001, 2.0f, 2.0f, 2.0f);
    params.scaling(100, 1000, 1001, kind);
    RegulationManager m(Preferences{}, kFirstId);
    m.target_ids = {100};
    m.on_regulation_loaded(params.source());
    return m;
}

}  // namespace

TEST(ScalingEffect, PicksTheRowForThePlayerCount) {
    FakeParams params;
    const auto m = built_manager(params, 2);
    for (std::uint32_t n = 2; n <= 6; ++n) {
        const auto d = choose_scaling_effect(m, false, 100, n);
        EXPECT_EQ(d.action, EffectAction::Apply);
        EXPECT_EQ(d.spEffect_id, static_cast<std::int32_t>(kFirstId + n - 2));
        EXPECT_EQ(d.store_player_count, n);  // kind 2
    }
    EXPECT_EQ(choose_scaling_effect(m, true, 100, 2).spEffect_id, static_cast<std::int32_t>(kFirstId + 5));
}

TEST(ScalingEffect, OtherKindsDoNotStoreTheCount) {
    FakeParams params;
    const auto m = built_manager(params, 1);
    const auto d = choose_scaling_effect(m, false, 100, 3);
    EXPECT_EQ(d.action, EffectAction::Apply);
    EXPECT_FALSE(d.store_player_count);
}

TEST(ScalingEffect, SoloOrUnknownRowDoesNothing) {
    FakeParams params;
    const auto m = built_manager(params, 2);
    EXPECT_EQ(choose_scaling_effect(m, false, 100, 0).action, EffectAction::None);
    EXPECT_EQ(choose_scaling_effect(m, false, 100, 1).action, EffectAction::None);
    EXPECT_EQ(choose_scaling_effect(m, false, 999, 3).action, EffectAction::None);
}

TEST(ScalingEffect, MoreThanSixPlayersClears) {
    FakeParams params;
    const auto m = built_manager(params, 2);
    EXPECT_EQ(choose_scaling_effect(m, false, 100, 7).action, EffectAction::Clear);
}

TEST(ScalingEffect, PlayerCountArithmetic) {
    EXPECT_EQ(player_count_from_span(0), 0u);
    EXPECT_EQ(player_count_from_span(0xf0), 1u);
    EXPECT_EQ(player_count_from_span(0xf0 * 6), 6u);
    EXPECT_EQ(scaled_value(1.0f, 0.5f, 3), 2.5f);
}
