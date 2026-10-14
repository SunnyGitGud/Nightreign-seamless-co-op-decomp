#include "nrsc/server/leaderboard.hpp"

#include <array>
#include <string>

namespace nrsc::server {

TierResult compute_tier(std::uint32_t value, const std::vector<Threshold>& t) {
    if (t.empty()) return {0, 0};
    std::size_t i = 0;
    while (i < t.size() && value >= t[i].value) ++i;
    TierResult r{std::int32_t(i), 0};
    if (i == 1)
        r.value = t[0].value >> 1;
    else if (i >= 2)
        r.value = std::uint32_t(t[i - 1].value + t[i - 2].value) >> 1;
    return r;
}

void Leaderboard::start_find() {
    stats_.FindOrCreateLeaderboard(std::string(kLeaderboardName), kSortDescending, kDisplayNumeric);
    state_ = kStateFinding;
}

void Leaderboard::start_download() {
    const std::array<SteamId, 1> users{core_.own_id};
    stats_.DownloadLeaderboardEntriesForUsers(handle_, users);
    state_ = kStateDownloading;
}

void Leaderboard::on_download_result(std::uint64_t entries_handle, std::uint32_t count) {
    if (count == 0) {
        tier_ = 0;
        value_ = 0;
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        LeaderboardEntry e;
        if (!stats_.GetDownloadedLeaderboardEntry(entries_handle, std::int32_t(i), e)) continue;
        if (e.steam_id != core_.own_id) continue;
        tier_ = e.details[0];
        value_ = std::uint32_t(e.details[1]);
    }
}

bool Leaderboard::upload_step() {
    if (!core_.leaderboard_add_pending && !core_.leaderboard_refresh_pending) return false;
    std::int32_t tier = tier_;
    std::uint32_t value = value_;
    if (core_.leaderboard_add_pending) {
        core_.leaderboard_add_pending = false;
        value_ += accumulator_;
        accumulator_ = 0;
        tier = tier_;
        value = value_;
        if (!core_.leaderboard_refresh_pending) {
            const std::array<std::int32_t, 2> details{tier, std::int32_t(value)};
            stats_.UploadLeaderboardScore(handle_, kUploadForceUpdate, details);
            return true;
        }
    }
    core_.leaderboard_refresh_pending = false;
    const std::uint32_t old = value_;
    const TierResult r = compute_tier(old, thresholds_);
    tier_ = r.tier;
    value_ = r.value;
    if (old == r.value) return false;
    const std::array<std::int32_t, 2> details{r.tier, std::int32_t(r.value)};
    stats_.UploadLeaderboardScore(handle_, kUploadForceUpdate, details);
    return true;
}

}  // namespace nrsc::server
