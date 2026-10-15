#pragma once
// Session: facade of the platform-independent session layer (docs/session.md). Owns SessionState, PeerList (5 slots),
// MessageBus, JoinFlow, LobbyLifecycle (W), server::Lobby, FrameTask and PartyTracker, attaches the bus/peers to
// hooks::net::NetState while a lobby is entered and detaches on leave.
//
// THREADING. Every public member takes one internal mutex, so the Steam callback dispatchers and tick() may be called
// from any single non-game thread. The hooks (game thread) read NetState without that mutex (NetState itself has none;
// server/* and hooks/* are not editable here): while attached, calls into NetState from the game thread race with
// tick()'s pop_received/enqueue_received. Recommended: call tick() and the dispatchers from the game thread, or accept that
// the received-queue map is only safe when the hooks do not touch it concurrently (open point).
// post_job() functions run WITHOUT the session mutex (after the locked part of tick()), so they may call Session.
// set_*_handler functions run inside bus.poll() with the mutex held and must not call back into Session.
#include <functional>
#include <mutex>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/hooks/session_word.hpp"
#include "nrsc/server/callbacks.hpp"
#include "nrsc/server/lobby.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/session/frame_task.hpp"
#include "nrsc/session/player_sync.hpp"
#include "nrsc/session/join_flow.hpp"
#include "nrsc/session/lobby_lifecycle.hpp"
#include "nrsc/session/party.hpp"
#include "nrsc/session/session_state.hpp"
#include "nrsc/session/steam_session_iface.hpp"

namespace nrsc::session {

struct SteamInterfaces {
    server::ISteamMatchmaking& matchmaking;
    server::ISteamNetworkingMessages& messages;
    ISteamLobbyControl& lobbies;
    ISteamSessionInfo& sessions;
};

struct SessionConfig {
    float handshake_rate = 1.0f;  // Hz, U default
    float ping_rate = 1.0f;       // Hz, U default
    int receive_channel = 0;      // core.channel (ctx+4), U
    std::uint32_t max_len = 0x400;  // core.max_len (ctx+0xc), U
};

class Session {
public:
    // `own_id` = local SteamID (core+8). `net` must be the live NetState (construct after hooks::groups::net()).
    Session(SteamInterfaces steam, SteamId own_id, hooks::net::NetState& net = hooks::net::net_state(),
            PartyTracker::Setter party_setter = hooks::set_session_word, SessionConfig cfg = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- Steam callback dispatch (ids: server::kRegisteredCallbacks) ----
    void on_lobby_created(SteamId lobby, std::int32_t result = 1);                         // 0x201
    // 0x1f8. `chat_room_enter_response` = LobbyEnter_t::m_EChatRoomEnterResponse (1 = success). Returns true when adopted.
    bool on_lobby_enter(SteamId lobby, std::uint32_t chat_room_enter_response = 1);        // 0x1f8
    void on_lobby_chat_update(SteamId lobby, SteamId user_changed, std::uint32_t state_change);  // 0x1fa
    void on_session_request(SteamId remote);        // 0x4e3, throws std::system_error if the core is closed (as the original)
    void on_session_failed();                       // 0x4e4, no-op
    void on_relay_network_status(std::uint32_t avail);  // 0x501
    void on_game_lobby_join_requested(SteamId lobby, SteamId friend_id);  // 0x14d
    void on_lobby_data_update(SteamId lobby, SteamId member, bool success);  // 0x1f9

    // Per-frame. Polls the bus, runs FrameTask phases 1-5 and the party tracker (only while attached), then runs one job.
    void tick(float dt);

    // ---- the game's own lobby calls, seen through nrsc's ISteamMatchmaking vtable hooks (docs/devirt.md 2.11, C) ----
    // CreateLobby (slot 13, VM detour 0x18002bc30): a Public lobby (the game hosting) becomes the Seamless lobby: the
    // create-pending flag is set and the type to forward is FriendsOnly. Any other type is forwarded unchanged.
    // Returns the lobby type to pass to the original. Calls made by this Session itself pass through unchanged.
    int on_game_create_lobby(int lobby_type);
    // LeaveLobby (slot 15, detour 0x18002bc70): leaving the current lobby closes the Steam session with every active
    // peer and resets the lobby state; the caller then runs the original. Calls made by this Session pass through.
    void on_game_leave_lobby(SteamId lobby);

    // ---- user actions (not called by the DLL any more: the trigger is the game's CreateLobby) ----
    bool host_start_session();   // CreateLobby; false if already in/creating a lobby
    void leave_session();        // LeaveLobby + detach + clear state

    // ---- observers / configuration ----
    bool in_lobby() const;
    bool party_active() const;
    std::uint64_t flags() const;
    std::uint64_t join_error() const;
    int member_count() const;
    int active_without_bit3() const;
    int active_with_bit3() const;
    std::int32_t ping_ms(SteamId id) const;
    void set_bit3(bool on);                          // U: who sets M bit3 in the original
    void set_member_bytes_source(std::function<std::size_t()> f);
    void set_sub_header_handler(server::MessageHandler h);   // message id 0 (U body)
    void set_broadcast_handler(server::MessageHandler h);    // message id 1 (U body)
    void post_job(std::function<void()> job);        // thread-safe
    void set_rates(float handshake_hz, float ping_hz);
    // The game side of the party data sync and the F4 pause (devirt 2.12); null = off. `game` must outlive the Session.
    void set_game_players(IGamePlayers* game, TextSource text);
    bool paused() const;

    // Unlocked access for tests / adapter wiring only.
    SessionState& state() { return state_; }
    server::PeerList& peers() { return peers_; }
    server::MessageBus& bus() { return bus_; }
    FrameTask& frame_task() { return frame_; }
    PlayerSync& player_sync() { return sync_; }

private:
    void attach_locked();
    void detach_locked(bool leave_steam);

    mutable std::mutex mutex_;
    SteamInterfaces steam_;
    hooks::net::NetState& net_;
    SessionState state_;
    server::PeerList peers_;
    server::MessageBus bus_;
    JoinFlow join_;
    LobbyLifecycle lifecycle_;
    server::Lobby lobby_;
    FrameTask frame_;
    PlayerSync sync_;
    PartyTracker party_;
    server::MessageHandler sub_header_, broadcast_;
};

}  // namespace nrsc::session
