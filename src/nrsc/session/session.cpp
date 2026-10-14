#include "nrsc/session/session.hpp"

#include <algorithm>
#include <cstring>

namespace nrsc::session {

namespace {
constexpr std::uint32_t kEnterSuccess = 1;  // EChatRoomEnterResponse::k_EChatRoomEnterResponseSuccess
}

Session::Session(SteamInterfaces steam, SteamId own_id, hooks::net::NetState& net, PartyTracker::Setter party_setter,
                 SessionConfig cfg)
    : steam_(steam),
      net_(net),
      peers_(server::kPeerPoolSize),
      bus_(state_.core, peers_, steam.messages),
      join_(state_, steam.matchmaking),
      lifecycle_(state_, steam.lobbies),
      lobby_(state_.core, peers_, steam.matchmaking,
             [this](SteamId id) {  // forwarded peer-left event; runs under the mutex (called from on_lobby_chat_update)
                 frame_.forget_peer(id);
             }),
      frame_(state_, peers_, bus_, net, steam.sessions),
      party_(state_, peers_, std::move(party_setter)) {
    state_.core.own_id = own_id;
    state_.core.channel = cfg.receive_channel;
    state_.core.max_len = cfg.max_len;
    frame_.timers().rate1 = cfg.handshake_rate;
    frame_.timers().rate2 = cfg.ping_rate;

    // Startup handler table (devirt 2.9): 0 sub-header, 1 broadcast (bodies U: forwarded to optional handlers), 2 ignored,
    // 3 peer channel. Id 3's body is W: the receivers' queue logic lives in protected code; the packet is
    // [3][channel][0][u32 len][payload], so the payload is queued on (sender, channel) for NetState::pop_received.
    server::register_startup_handlers(
        bus_,
        [this](server::Peer& p, std::span<const std::byte> pkt, std::uint32_t rem) { if (sub_header_) sub_header_(p, pkt, rem); },
        [this](server::Peer& p, std::span<const std::byte> pkt, std::uint32_t rem) { if (broadcast_) broadcast_(p, pkt, rem); },
        [this](server::Peer& p, std::span<const std::byte> pkt, std::uint32_t rem) {
            if (pkt.size() < 7) return;
            std::uint32_t len;
            std::memcpy(&len, pkt.data() + 3, sizeof len);
            len = std::min<std::uint32_t>({len, rem, static_cast<std::uint32_t>(pkt.size() - 7)});
            net_.enqueue_received(p.steam_id, static_cast<std::uint8_t>(pkt[1]), pkt.subspan(7, len));
        });
}

Session::~Session() {
    std::lock_guard lk(mutex_);
    if (net_.bus == &bus_) net_.detach();  // never leave dangling pointers in the hooks' NetState
}

void Session::attach_locked() { net_.attach(bus_, peers_); }

void Session::detach_locked(bool leave_steam) {
    if (leave_steam) lifecycle_.leave();
    if (net_.bus == &bus_) net_.detach();
    for (std::size_t i = 0; i < peers_.size(); ++i) server::PeerList::reset(peers_.slot(i));
    frame_.clear_pings();
    state_.clear(kInLobby);
    state_.clear(kLobbyCreated);
    state_.core.lobby_id = 0;
    state_.core.owner_id = 0;
    party_.update();  // in lobby is now false -> word cleared
}

// ---- callbacks ----
void Session::on_lobby_created(SteamId lobby, std::int32_t result) {
    std::lock_guard lk(mutex_);
    lobby_.OnLobbyCreated({result, lobby});
}

bool Session::on_lobby_enter(SteamId lobby, std::uint32_t response) {
    std::lock_guard lk(mutex_);
    if (response != kEnterSuccess) {  // W: failed JoinLobby consumes the join target
        lifecycle_.join_finished();
        return false;
    }
    const bool adopted = lobby_.OnLobbyEnter({lobby});
    if (adopted) {
        attach_locked();
        party_.update();
    }
    lifecycle_.join_finished();  // W
    return adopted;
}

void Session::on_lobby_chat_update(SteamId lobby, SteamId user, std::uint32_t change) {
    std::lock_guard lk(mutex_);
    lobby_.OnChatUpdate({lobby, user, change});
    party_.update();
}

void Session::on_session_request(SteamId remote) {
    std::lock_guard lk(mutex_);
    server::on_session_request(state_.core, steam_.messages, remote);
}

void Session::on_session_failed() { server::on_session_failed(); }

void Session::on_relay_network_status(std::uint32_t avail) {
    std::lock_guard lk(mutex_);
    server::on_relay_network_status(state_.core, avail);
}

void Session::on_game_lobby_join_requested(SteamId lobby, SteamId friend_id) {
    std::lock_guard lk(mutex_);
    join_.on_game_lobby_join_requested(lobby, friend_id);  // C
    lifecycle_.request_join_data();                         // W
}

void Session::on_lobby_data_update(SteamId lobby, SteamId member, bool success) {
    std::lock_guard lk(mutex_);
    if (join_.on_lobby_data_update(lobby, member, success) == JoinFlow::DataResult::Accepted)  // C
        lifecycle_.join_accepted();                                                           // W
}

// ---- frame ----
void Session::tick(float dt) {
    {
        std::lock_guard lk(mutex_);
        if (net_.bus == &bus_) {
            bus_.poll();  // W: the original's caller of 0x180063110 is not recovered
            frame_.tick(dt);
            party_.update();
        }
    }
    frame_.run_one_job();
}

// ---- user actions ----
bool Session::host_start_session() {
    std::lock_guard lk(mutex_);
    return lifecycle_.host_start();
}

void Session::leave_session() {
    std::lock_guard lk(mutex_);
    detach_locked(true);
}

// ---- observers ----
bool Session::in_lobby() const { std::lock_guard lk(mutex_); return state_.has(kInLobby); }
bool Session::party_active() const { std::lock_guard lk(mutex_); return party_.active(); }
std::uint64_t Session::flags() const { std::lock_guard lk(mutex_); return state_.flags(); }
std::uint64_t Session::join_error() const { std::lock_guard lk(mutex_); return state_.join_error; }
int Session::member_count() const { std::lock_guard lk(mutex_); return frame_.member_count(); }
int Session::active_without_bit3() const { std::lock_guard lk(mutex_); return frame_.active_without_bit3(); }
int Session::active_with_bit3() const { std::lock_guard lk(mutex_); return frame_.active_with_bit3(); }
std::int32_t Session::ping_ms(SteamId id) const { std::lock_guard lk(mutex_); return frame_.ping_ms(id); }
void Session::set_bit3(bool on) { std::lock_guard lk(mutex_); if (on) state_.set(kBit3); else state_.clear(kBit3); }
void Session::set_member_bytes_source(std::function<std::size_t()> f) {
    std::lock_guard lk(mutex_);
    frame_.set_member_bytes_source(std::move(f));
}
void Session::set_sub_header_handler(server::MessageHandler h) { std::lock_guard lk(mutex_); sub_header_ = std::move(h); }
void Session::set_broadcast_handler(server::MessageHandler h) { std::lock_guard lk(mutex_); broadcast_ = std::move(h); }
void Session::post_job(std::function<void()> job) { frame_.post_job(std::move(job)); }
void Session::set_rates(float h, float p) {
    std::lock_guard lk(mutex_);
    frame_.timers().rate1 = h;
    frame_.timers().rate2 = p;
}

}  // namespace nrsc::session
