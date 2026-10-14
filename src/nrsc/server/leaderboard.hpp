#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "nrsc/server/core_state.hpp"
#include "nrsc/server/steam_iface.hpp"

// Steam leaderboard client of server_manager (docs/server_manager.md 2.3, functions 0x18003d420..0x18003d760).
namespace nrsc::server {

inline constexpr std::string_view kLeaderboardName = "yui_nexus_3_seamless_deep_night_leaderboard_000";  // C
inline constexpr int kSortDescending = 2;       // k_ELeaderboardSortMethodDescending (C: 0x18003d420)
inline constexpr int kDisplayNumeric = 1;       // k_ELeaderboardDisplayTypeNumeric (C)
inline constexpr int kUploadForceUpdate = 2;    // k_ELeaderboardUploadScoreMethodForceUpdate (C: 0x18003d88a)

inline constexpr std::int32_t kStateFinding = 2;       // set by 0x18003d420 (C)
inline constexpr std::int32_t kStateDownloading = 4;   // set by 0x18003d5e0 (C)

// Element of the sorted threshold vector (+0x30..+0x38). Elements are 8 bytes; only the u32 at +4 is read (the u32 at
// +0 is never read by the readable code: U).
struct Threshold {
    std::uint32_t unknown0 = 0;
    std::uint32_t value = 0;
};

struct TierResult {
    std::int32_t tier = 0;
    std::uint32_t value = 0;
};

// The tier loop of 0x18003d7c5..0x18003d85d (C). tier = index of the first threshold strictly greater than `value`
// (unsigned compare), or thresholds.size() if none. The code also replaces the stored value with a representative
// one: 0 for tier 0, thresholds[0]/2 for tier 1, else (thresholds[tier-1] + thresholds[tier-2]) / 2 (32-bit wrap,
// logical shift). An empty vector gives tier 0, value 0.
TierResult compute_tier(std::uint32_t value, const std::vector<Threshold>& thresholds);

class Leaderboard {
public:
    Leaderboard(CoreState& core, ISteamUserStats& stats) : core_(core), stats_(stats) {}

    // 0x18003d420 (C): FindOrCreateLeaderboard(name, Descending, Numeric); state = 2.
    void start_find();
    // The find CCallResult body is not among the functions read; storing the handle (+0x60) is S, and the state
    // afterwards is U (left unchanged).
    void on_find_result(LeaderboardHandle handle) { handle_ = handle; }
    // 0x18003d5e0 (C): DownloadLeaderboardEntriesForUsers(handle, [own id], 1); state = 4. The result handle for the
    // entries is delivered to on_download_result.
    void start_download();
    // 0x18003d6a0 (C): for each of `count` entries, if the entry's steam id is ours, the 8 bytes at +0x4c
    // ({details[0], details[1]}) become {tier, value}. With count == 0 they are zeroed.
    void on_download_result(std::uint64_t entries_handle, std::uint32_t count);
    // 0x18003d760 (C): the accumulator/refresh step. Returns true when UploadLeaderboardScore was called.
    //  - neither pending flag: nothing.
    //  - add flag: clear it, value += accumulator, accumulator = 0; if refresh is not also pending, upload
    //    {tier, value} unconditionally with the stored tier.
    //  - refresh flag: clear it, recompute {tier, value} with compute_tier and store both; upload only if the value
    //    changed.
    bool upload_step();

    // Accessors for state fields (+0x68, +0x4c, +0x50, +0x54, +0x60).
    std::int32_t state() const { return state_; }
    std::int32_t tier() const { return tier_; }
    std::uint32_t value() const { return value_; }
    std::uint32_t accumulator() const { return accumulator_; }
    LeaderboardHandle handle() const { return handle_; }
    void set_value(std::uint32_t v) { value_ = v; }
    void set_tier(std::int32_t t) { tier_ = t; }
    void add_to_accumulator(std::uint32_t v) { accumulator_ += v; }
    std::vector<Threshold>& thresholds() { return thresholds_; }
    // Where thresholds and score increments come from is U (written by protected/game code).

private:
    CoreState& core_;
    ISteamUserStats& stats_;
    std::int32_t state_ = 0;
    std::int32_t tier_ = 0;
    std::uint32_t value_ = 0;
    std::uint32_t accumulator_ = 0;
    LeaderboardHandle handle_ = 0;
    std::vector<Threshold> thresholds_;
};

}  // namespace nrsc::server
