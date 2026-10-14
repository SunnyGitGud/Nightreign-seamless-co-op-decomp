#pragma once
// JoinFlow: the two recovered join callbacks (phase3 1.3), nothing else.
#include <cstdint>

#include "nrsc/server/steam_iface.hpp"
#include "nrsc/session/session_state.hpp"

namespace nrsc::session {

class JoinFlow {
public:
    enum class DataResult { Ignored, Accepted, BadData, NoSeamless, WrongVersion };

    JoinFlow(SessionState& state, server::ISteamMatchmaking& mm) : state_(state), mm_(mm) {}

    // 0x1800643c0 (C): join_lobby = lobby, join_friend = friend, flags |= kJoinPending.
    void on_game_lobby_join_requested(SteamId lobby, SteamId friend_id);

    // 0x180064120 (C), exactly the table of phase3 1.3. `member` is not read.
    //  lobby != join_lobby or kJoinPending clear -> Ignored (no write); then kJoinPending is cleared;
    //  !success -> BadData (join target cleared, +0x140 untouched); lobby type != master -> NoSeamless (target cleared,
    //  error 0xE0C1); version != "1.1.3" -> WrongVersion (error 0xE0C2); else Accepted (target kept).
    DataResult on_lobby_data_update(SteamId lobby, SteamId member, bool success);

private:
    SessionState& state_;
    server::ISteamMatchmaking& mm_;
};

}  // namespace nrsc::session
