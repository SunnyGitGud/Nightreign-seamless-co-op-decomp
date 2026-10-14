#include "nrsc/session/join_flow.hpp"

#include <string>

#include "nrsc/server/lobby.hpp"

namespace nrsc::session {

void JoinFlow::on_game_lobby_join_requested(SteamId lobby, SteamId friend_id) {
    state_.join_lobby() = lobby;
    state_.join_friend() = friend_id;
    state_.set(kJoinPending);
}

JoinFlow::DataResult JoinFlow::on_lobby_data_update(SteamId lobby, SteamId /*member*/, bool success) {
    if (lobby != state_.join_lobby()) return DataResult::Ignored;
    if (!state_.has(kJoinPending)) return DataResult::Ignored;
    state_.clear(kJoinPending);
    if (!success) {
        state_.clear_join_target();
        return DataResult::BadData;
    }
    // memcmp(..., 0x20) over a NUL-terminated key is exact string equality (as in server::Lobby::OnLobbyEnter).
    if (mm_.GetLobbyData(lobby, std::string(server::kLobbyTypeKey)) != server::kMasterLobby) {
        state_.clear_join_target();
        state_.join_error = kJoinErrorNoSeamless;
        return DataResult::NoSeamless;
    }
    if (mm_.GetLobbyData(lobby, std::string(server::kLobbyVersionKey)) != server::kModVersion) {
        state_.clear_join_target();
        state_.join_error = kJoinErrorWrongVersion;
        return DataResult::WrongVersion;
    }
    return DataResult::Accepted;
}

}  // namespace nrsc::session
