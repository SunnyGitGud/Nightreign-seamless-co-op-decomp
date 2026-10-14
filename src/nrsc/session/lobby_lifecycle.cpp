#include "nrsc/session/lobby_lifecycle.hpp"

namespace nrsc::session {

void LobbyLifecycle::request_join_data() {
    if (!steam_.RequestLobbyData(state_.join_lobby())) {
        state_.clear(kJoinPending);
        state_.clear_join_target();
    }
}

void LobbyLifecycle::join_accepted() { steam_.JoinLobby(state_.join_lobby()); }

bool LobbyLifecycle::host_start() {
    if (state_.has(kInLobby) || state_.core.lobby_create_pending) return false;
    state_.core.lobby_create_pending = true;
    steam_.CreateLobby(kLobbyTypeFriendsOnly, kLobbyMaxMembers);
    return true;
}

void LobbyLifecycle::leave() {
    if (state_.core.lobby_id != 0) steam_.LeaveLobby(state_.core.lobby_id);
}

}  // namespace nrsc::session
