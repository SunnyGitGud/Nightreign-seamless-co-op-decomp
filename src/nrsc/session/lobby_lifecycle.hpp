#pragma once
// LobbyLifecycle: the standard Steam join/host/leave calls. ALL OF THIS IS W: the original's caller of JoinLobby and the
// code that starts a session are in protected code and were not recovered (phase3 1.3 "U: the actual JoinLobby", "the
// code that calls RequestLobbyData"). Kept in this one class so it can be replaced when the real flow is known.
#include "nrsc/session/session_state.hpp"
#include "nrsc/session/steam_session_iface.hpp"

namespace nrsc::session {

class LobbyLifecycle {
public:
    LobbyLifecycle(SessionState& state, ISteamLobbyControl& steam) : state_(state), steam_(steam) {}

    // After GameLobbyJoinRequested: ask Steam for the lobby data so 0x180064120 can validate it.
    // If the request cannot be sent the join target is dropped.
    void request_join_data();
    // After 0x180064120 accepted: JoinLobby(join_lobby).
    void join_accepted();
    // Host side: sets lobby_create_pending and calls CreateLobby. False if already in a lobby / creation pending.
    bool host_start();
    // LeaveLobby for the current lobby (no-op when none).
    void leave();
    // LobbyEnter finished (success or not): the join target is consumed.
    void join_finished() { state_.clear_join_target(); }

private:
    SessionState& state_;
    ISteamLobbyControl& steam_;
};

}  // namespace nrsc::session
