#include "nrsc/params/regulation_manager.hpp"

#include <cstring>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::params {

namespace {

constexpr const char* kSourceFile = "nrsc\\mod\\param_repository\\param_repository.cpp";

template <typename T>
T load(const std::byte* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

void store(std::byte* p, float v) { std::memcpy(p, &v, sizeof v); }

std::optional<float> find(const std::unordered_map<std::int32_t, float>& m, std::int32_t k) {
    const auto it = m.find(k);
    if (it == m.end()) return std::nullopt;
    return it->second;
}

}  // namespace

std::uint32_t player_count_from_span(std::size_t bytes) {
    return static_cast<std::uint32_t>(bytes >> 4) * 0xeeeeeeefu;
}

float scaled_value(float preference, float delta, int n) {
    const float product = preference * delta;
    const float scaled = product * static_cast<float>(n);
    return scaled + 1.0f;
}

RegulationManager::RegulationManager(config::Preferences preferences, std::uint32_t first_row_id)
    : preferences_(preferences), next_row_id_(first_row_id) {}

void RegulationManager::on_regulation_loaded(const RowSource& rows) {
    ready_ = true;
    if (built_) return;
    built_ = true;
    sets_[0].try_emplace(kSpEffectParam);
    build(0, rows);
    build(1, rows);
}

void RegulationManager::build(int set, const RowSource& rows) {
    const auto spEffect_overrides = sets_[0].find(kSpEffectParam);
    if (spEffect_overrides == sets_[0].end())
        throw FatalError("YuiNexusRegulationManager", kSourceFile, 0xb5);

    RowMap records;
    for (const std::int32_t target : target_ids) {
        const std::byte* source = rows(kScalingParam, target);
        if (source == nullptr) continue;
        const std::byte* base = rows(kSpEffectParam, load<std::int32_t>(source + 4));
        if (base == nullptr) continue;
        const std::byte* full = rows(kSpEffectParam, load<std::int32_t>(source + 8));
        if (full == nullptr) continue;

        float d_health = load<float>(full + 0x10) - load<float>(base + 0x10);
        const float d_damage = load<float>(full + 0x38) - load<float>(base + 0x38);
        const float d_posture = load<float>(full + kPostureField) - load<float>(base + kPostureField);
        if (d_health == 0.0f) d_health = kZeroHealthDelta;  // cmpeqss/andps/andnps/orps
        health_delta_[target] = d_health;
        damage_delta_[target] = d_damage;
        posture_delta_[target] = d_posture;

        ScalingRecord& record = records_.emplace_back();
        record.kind = static_cast<std::uint8_t>(source[0x10]);
        for (int i = 0; i < kScaledVariants; ++i) {
            const std::uint32_t id = next_row_id_++;
            auto& row = rows_.emplace_back();
            std::memcpy(row.data(), base, kSpEffectRowSize);

            const int n = i + 1;
            const float health = scaled_value(preferences_.health_scaling, d_health, n);
            const float damage = scaled_value(preferences_.damage_scaling, d_damage, n);
            const float posture_raw = scaled_value(preferences_.posture_scaling, d_posture, n);
            const float posture = kMinPosture > posture_raw ? kMinPosture : posture_raw;  // maxss
            for (const std::size_t off : kHealthFields) store(row.data() + off, health);
            for (const std::size_t off : kDamageFields) store(row.data() + off, damage);
            store(row.data() + kPostureField, posture);

            record.spEffect_ids[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(id);
            spEffect_overrides->second.try_emplace(id, row.data());
        }
        records.try_emplace(static_cast<std::uint32_t>(target), reinterpret_cast<const std::byte*>(&record));
    }
    sets_[static_cast<std::size_t>(set)].try_emplace(kScalingParam, std::move(records));
}

std::optional<OverrideRef> RegulationManager::find_override(int set, std::int32_t param_index,
                                                            std::uint32_t id) const {
    if (!ready_) return std::nullopt;
    const SetMap& overrides = sets_[static_cast<std::size_t>(set)];
    const auto param = overrides.find(param_index);
    if (param == overrides.end()) return std::nullopt;
    const auto row = param->second.find(id);
    if (row == param->second.end()) return std::nullopt;
    return OverrideRef{id, 2, row->second};
}

std::optional<float> RegulationManager::health_delta(std::int32_t t) const { return find(health_delta_, t); }
std::optional<float> RegulationManager::damage_delta(std::int32_t t) const { return find(damage_delta_, t); }
std::optional<float> RegulationManager::posture_delta(std::int32_t t) const { return find(posture_delta_, t); }

EffectDecision choose_scaling_effect(const RegulationManager& manager, bool alternate_set,
                                     std::uint32_t scaling_row_id, std::uint32_t player_count) {
    const auto found = manager.find_override(alternate_set ? 1 : 0, kScalingParam, scaling_row_id);
    if (player_count < 2 || !found) return {};

    EffectDecision clear{EffectAction::Clear, -1, std::nullopt};
    if (found->data == nullptr || player_count - 2 > 4) return clear;
    ScalingRecord record;
    std::memcpy(&record, found->data, sizeof record);
    const std::int32_t id = record.spEffect_ids[player_count - 2];
    if (id == -1) return clear;

    EffectDecision apply{EffectAction::Apply, id, std::nullopt};
    if (record.kind == 2) apply.store_player_count = player_count;
    return apply;
}

}  // namespace nrsc::params
