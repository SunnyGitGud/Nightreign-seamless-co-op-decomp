#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "nrsc/config/settings.hpp"

// nrsc\mod\param_repository\param_repository.cpp: "YuiNexusRegulationManager", the multiplayer scaling
// rows. See docs/param_repository.md.
//
//   FUN_1800295e0  hook detour: calls the original, then marks ready (+0x10) and builds once (+0x11)
//   FUN_1800364b0  build one override set (ev_76b0077905c56f76…)
//   FUN_1800357b0  override lookup used by the two row hooks (ev_864f112ff70f9fd5…)
//   FUN_180029140  per-player-count effect choice (ev_f527d0c17587716e…)
//   FUN_1800297c0  param-0xf row hook: override row first, else the original getter
//
// The game-side parts (hook targets, the getter and effect calls) are not here. Rows come from a
// RowSource, and the effect choice returns a decision instead of calling the game.
namespace nrsc::params {

// Param indices in SoloParamRepository as used by the code. The names are inferred (W): 0xf is read as
// a 0x410-byte row with rate floats, so probably SpEffectParam. 0x4f is unknown (U).
inline constexpr std::int32_t kSpEffectParam = 0x0f;
inline constexpr std::int32_t kScalingParam = 0x4f;

inline constexpr std::size_t kSpEffectRowSize = 0x410;
inline constexpr int kScaledVariants = 5;  // player counts 2..6

// Field offsets written in each generated 0x410-byte row (0x1800367f7..0x180036882).
inline constexpr std::array<std::size_t, 8> kHealthFields = {0x10, 0x108, 0x10c, 0x110, 0x114, 0x2b4, 0x340, 0x344};
inline constexpr std::array<std::size_t, 5> kDamageFields = {0x38, 0x3c, 0x40, 0x44, 0x1e0};
inline constexpr std::size_t kPostureField = 0x25c;

inline constexpr float kZeroHealthDelta = 0.15f;  // .rdata 0x180145dc0
inline constexpr float kMinPosture = 0.1f;        // .rdata 0x180145dc4

// The argument FUN_180029140 passes to the game call at manager-hooks +0x58 when it clears the effect.
inline constexpr std::int32_t kClearArgument = 0x11a;

// The 0x18-byte record stored for param 0x4f. `kind` is copied from byte +0x10 of the source 0x4f row.
// spEffect_ids[i] is the generated row for player count i + 2.
struct ScalingRecord {
    std::uint8_t kind = 0;
    std::array<std::int32_t, kScaledVariants> spEffect_ids{};
};
static_assert(sizeof(ScalingRecord) == 0x18);

// Return value of FUN_1800357b0: {id, 2, pointer}. What the constant 2 means is unknown.
struct OverrideRef {
    std::uint32_t id = 0;
    std::uint32_t tag = 2;
    const std::byte* data = nullptr;
};

// Row access, as FUN_180035630 does it (see param_file.hpp): returns the row or null.
using RowSource = std::function<const std::byte*(std::int32_t param_index, std::int32_t row_id)>;

// The player-count arithmetic of FUN_180029140: ((end - begin) >> 4) * 0xeeeeeeef in 32 bits, which is
// an exact division by 0xf0 (the entry size) when the span is a multiple of it.
std::uint32_t player_count_from_span(std::size_t bytes);

// Per-player-count value for one generated row: ((preference * delta) * n) + 1, all in float.
float scaled_value(float preference, float delta, int n);

class RegulationManager {
public:
    RegulationManager(config::Preferences preferences, std::uint32_t first_row_id);

    // +0x18: the 0x4f row ids to scale. The code that fills it is not readable (U).
    std::vector<std::int32_t> target_ids;

    // FUN_1800295e0 after the original call: set ready (+0x10). The first time only (+0x11), create an empty
    // override map for param 0xf in set 0 and run build(0) and build(1).
    void on_regulation_loaded(const RowSource& rows);

    // FUN_1800364b0. For each target id t: row S = rows(0x4f, t) (skip if null); A = rows(0xf, S+4),
    // B = rows(0xf, S+8) (skip if either is null). Deltas d = B - A at +0x10/+0x38/+0x25c, with a health
    // delta of exactly 0 replaced by 0.15, are stored per t. Then 5 copies of A get fresh ids from the
    // counter (+0x14, post-increment) and the scaled fields, and go into set 0 / param 0xf. A record goes
    // into set `set` / param 0x4f. Inserts never overwrite (emplace). If set 0 has no param-0xf map,
    // this is fatal: FatalError("YuiNexusRegulationManager", param_repository.cpp, 0xb5).
    void build(int set, const RowSource& rows);

    // FUN_1800357b0: nothing until ready; otherwise sets[set][param][id].
    std::optional<OverrideRef> find_override(int set, std::int32_t param_index, std::uint32_t id) const;

    std::optional<float> health_delta(std::int32_t target) const;   // map +0x30
    std::optional<float> damage_delta(std::int32_t target) const;   // map +0x70
    std::optional<float> posture_delta(std::int32_t target) const;  // map +0xb0

    bool ready() const noexcept { return ready_; }
    std::uint32_t next_row_id() const noexcept { return next_row_id_; }

private:
    using RowMap = std::unordered_map<std::uint32_t, const std::byte*>;
    using SetMap = std::unordered_map<std::int32_t, RowMap>;

    config::Preferences preferences_;  // *(this + 8) +0x30/+0x34/+0x38
    bool ready_ = false;               // +0x10
    bool built_ = false;               // +0x11
    std::uint32_t next_row_id_;        // +0x14
    std::unordered_map<std::int32_t, float> health_delta_, damage_delta_, posture_delta_;
    std::array<SetMap, 2> sets_;  // +0xf0, +0x130
    // The original leaks its allocations; here the manager owns them.
    std::deque<std::array<std::byte, kSpEffectRowSize>> rows_;
    std::deque<ScalingRecord> records_;
};

// FUN_180029140 without the game calls.
enum class EffectAction {
    None,   // fewer than 2 players, or no record for this row: nothing is done
    Clear,  // call +0x58(…, kClearArgument) and set the target's +0x238 to 0
    Apply,  // apply spEffect_id (calls +0x60, +0x68, +0x70)
};

struct EffectDecision {
    EffectAction action = EffectAction::None;
    std::int32_t spEffect_id = -1;
    // Apply only: when the record kind is 2, the player count is stored at the target's +0x238.
    std::optional<std::uint32_t> store_player_count;
};

// `alternate_set` is ((*flags & 5) == 1) in the original and selects override set 1.
EffectDecision choose_scaling_effect(const RegulationManager& manager, bool alternate_set,
                                     std::uint32_t scaling_row_id, std::uint32_t player_count);

}  // namespace nrsc::params
