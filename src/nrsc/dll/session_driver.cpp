#include "nrsc/dll/session_driver.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
#include <exception>
#include <memory>
#include <string>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/dll/log.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/platform/steam/steam_adapter.hpp"
#include "nrsc/platform/steam/steam_callbacks.hpp"
#include "nrsc/session/session.hpp"

namespace nrsc::dll {
namespace {

using platform::steam::ISteamCallbackSink;
namespace ps = platform::steam;

// Lobby creation trigger (C, docs/devirt.md 2.11): the original vtable-hooks the game's ISteamMatchmaking object.
// Slot 13 CreateLobby -> VM detour 0x18002bc30: a Public lobby (the game hosting) sets the create-pending flag and is
// created as FriendsOnly; the LobbyCreated callback then tags it as the Seamless lobby. Slot 15 LeaveLobby -> 0x18002bc70.
constexpr unsigned kSlotCreateLobby = 13;
constexpr unsigned kSlotLeaveLobby = 15;

// ---- game side of the party data sync / F4 pause (devirt 2.12) ----
struct GameSlots {
    std::uint64_t serialize = 0, deserialize = 0, show_message = 0;           // fns 0x14020bde0, 0x14020dcb0, 0x14086a7a0
    std::uint64_t session_mgr = 0, member_owner = 0, game_data = 0;           // globals 0x143c21ec8, 0x143c21fc8, 0x143c078d0
    std::uint64_t world = 0;                                                  // global 0x143c0f0a8 (slot 0x188)
};
GameSlots g_slots;

template <class T>
T peek(std::uint64_t address) {
    T v{};
    if (address) std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof v);
    return v;
}

class GamePlayersWin final : public session::IGamePlayers {
public:
    bool world_ready() override {
        const auto w = peek<std::uint64_t>(g_slots.world);
        return w && peek<std::uint64_t>(w + 0x174e8);
    }
    std::optional<std::uint32_t> session_state() override {
        const auto mgr = peek<std::uint64_t>(g_slots.session_mgr);
        if (!mgr) return std::nullopt;
        return peek<std::uint32_t>(mgr + 0x10);
    }
    std::vector<session::PartyMember> members() override {
        std::vector<session::PartyMember> out;
        const auto owner = peek<std::uint64_t>(g_slots.member_owner);
        if (!owner) return out;
        const auto begin = peek<std::uint64_t>(owner + 0x30), end = peek<std::uint64_t>(owner + 0x38);
        if (!begin || end < begin || end - begin > 16 * session::kMemberRecordSize) return out;
        for (std::uint64_t r = begin; r + session::kMemberRecordSize <= end; r += session::kMemberRecordSize)
            out.push_back({peek<std::uint64_t>(r + 0x10), peek<std::int32_t>(r + 0xe8)});
        return out;
    }
    std::byte* local_player_data() override { return field(0x8); }
    std::byte* remote_player_table() override { return field(0x10); }
    void serialize(std::byte* pgd, std::byte* out) override {
        if (g_slots.serialize) reinterpret_cast<bool (*)(void*, void*)>(g_slots.serialize)(pgd, out);
    }
    void deserialize(std::byte* pgd, const std::byte* in) override {
        if (g_slots.deserialize) reinterpret_cast<bool (*)(void*, const void*)>(g_slots.deserialize)(pgd, in);
    }
    // Deliberate difference from the original, which acts on (GetAsyncKeyState(VK_F4) & 0x8001) == 0x8001: under Proton that
    // never happens (live session record 2026-10-16: M+0x195 never toggled; the Windows peer's F4 works). An up->down edge of
    // the 0x8000 bit, polled once per tick like the original, gives the same presses on Windows and also works under Wine.
    bool f4_pressed() override {
        const bool down = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        const bool pressed = down && !f4_was_down_;
        f4_was_down_ = down;
        return pressed;
    }
    void show_message(const std::u16string& text) override {
        if (!g_slots.show_message) return;
        const std::wstring w(text.begin(), text.end());
        alignas(16) static std::uint8_t handle[16];  // non-owning {pointer, id} result: nothing to release (devirt 2.13)
        reinterpret_cast<void* (*)(void*, const std::wstring*)>(g_slots.show_message)(handle, &w);
    }

private:
    bool f4_was_down_ = false;
    std::byte* field(std::uint64_t off) {
        const auto gdm = peek<std::uint64_t>(g_slots.game_data);
        return gdm ? reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(peek<std::uint64_t>(gdm + off))) : nullptr;
    }
};
GamePlayersWin g_game_players;

std::unique_ptr<session::Session> g_session;
std::unique_ptr<ps::SteamAdapter> g_steam;
bool g_steam_failed = false;
bool g_lobby_hooks_enabled = true;
LARGE_INTEGER g_last{}, g_freq{};

using CreateLobbyFn = std::uint64_t (*)(void* self, int lobby_type, int max_members);
using LeaveLobbyFn = void (*)(void* self, std::uint64_t lobby);
CreateLobbyFn g_original_create_lobby = nullptr;
LeaveLobbyFn g_original_leave_lobby = nullptr;

void (*g_original_run_callbacks)() = nullptr;

// Every callback is caught here: an exception must not unwind into steam_api64 / the game.
template <class F>
void guarded(const char* what, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        log_line("session: %s failed: %s", what, e.what());
    }
}

struct Sink final : ISteamCallbackSink {
    void on_lobby_created(const ps::LobbyCreatedEvent& e) override {
        log_line("steam: LobbyCreated result=%d lobby=%llu", e.result, (unsigned long long)e.lobby_id);
        if (g_session) guarded("LobbyCreated", [&] { g_session->on_lobby_created(e.lobby_id, e.result); });
    }
    void on_lobby_enter(const ps::LobbyEnterEvent& e) override {
        log_line("steam: LobbyEnter lobby=%llu response=%u", (unsigned long long)e.lobby_id, e.enter_response);
        if (g_session) guarded("LobbyEnter", [&] {
            const bool adopted = g_session->on_lobby_enter(e.lobby_id, e.enter_response);
            log_line("session: lobby %s", adopted ? "entered (net hooks attached)" : "not adopted");
        });
    }
    void on_lobby_chat_update(const ps::LobbyChatUpdateEvent& e) override {
        log_line("steam: LobbyChatUpdate user=%llu state=0x%x", (unsigned long long)e.user_changed, e.state_change);
        if (g_session) guarded("LobbyChatUpdate", [&] {
            g_session->on_lobby_chat_update(e.lobby_id, e.user_changed, e.state_change);
        });
    }
    void on_lobby_data_update(const ps::LobbyDataUpdateEvent& e) override {
        if (g_session) guarded("LobbyDataUpdate", [&] {
            g_session->on_lobby_data_update(e.lobby_id, e.member_id, e.success);
            if (const std::uint64_t err = g_session->join_error())
                log_line("session: join refused, error word 0x%llx (no Seamless lobby / wrong version)",
                         (unsigned long long)err);
        });
    }
    void on_game_lobby_join_requested(const ps::GameLobbyJoinRequestedEvent& e) override {
        log_line("steam: GameLobbyJoinRequested lobby=%llu friend=%llu", (unsigned long long)e.lobby_id,
                 (unsigned long long)e.friend_id);
        if (g_session) guarded("GameLobbyJoinRequested", [&] {
            g_session->on_game_lobby_join_requested(e.lobby_id, e.friend_id);
        });
    }
    void on_session_request(const ps::SessionRequestEvent& e) override {
        log_line("steam: SessionRequest from %llu", (unsigned long long)e.steam_id);
        if (g_session && e.is_steam_id) guarded("SessionRequest", [&] { g_session->on_session_request(e.steam_id); });
    }
    void on_session_failed(const ps::SessionFailedEvent& e) override {
        log_line("steam: SessionFailed %llu state=%d reason=%d", (unsigned long long)e.steam_id, e.state, e.end_reason);
        if (g_session) guarded("SessionFailed", [&] { g_session->on_session_failed(); });
    }
    void on_relay_network_status(const ps::RelayNetworkStatusEvent& e) override {
        if (g_session) guarded("RelayNetworkStatus", [&] {
            g_session->on_relay_network_status(static_cast<std::uint32_t>(e.avail));
        });
    }
};
Sink g_sink;

std::uint64_t detour_create_lobby(void* self, int lobby_type, int max_members) {
    int forwarded = lobby_type;
    if (g_session) guarded("CreateLobby hook", [&] { forwarded = g_session->on_game_create_lobby(lobby_type); });
    if (forwarded != lobby_type)
        log_line("session: game CreateLobby(type %d, max %d) -> Seamless lobby (type %d)", lobby_type, max_members, forwarded);
    return g_original_create_lobby(self, forwarded, max_members);
}

void detour_leave_lobby(void* self, std::uint64_t lobby) {
    if (g_session) guarded("LeaveLobby hook", [&] { g_session->on_game_leave_lobby(lobby); });
    g_original_leave_lobby(self, lobby);
}

void install_lobby_hooks() {
    if (!g_lobby_hooks_enabled) {
        log_line("session: lobby hooks disabled (nrsc_disable_hooks.txt); no Seamless lobby is created");
        return;
    }
    void* mm = g_steam->matchmaking_object();
    const auto vtable = mm ? *static_cast<std::uintptr_t*>(mm) : 0;
    if (!vtable) {
        log_line("session: ISteamMatchmaking object unavailable; lobby hooks not installed");
        return;
    }
    try {
        static hooks::Installer installer(platform::process_code_memory());  // VtableSlot only: no inline back end
        hooks::HookSpec create;
        create.name = "lobby ISteamMatchmaking::CreateLobby (slot 13)";
        create.kind = hooks::Kind::VtableSlot;
        create.site = vtable + 8 * kSlotCreateLobby;
        create.detour = reinterpret_cast<void*>(&detour_create_lobby);
        create.original = reinterpret_cast<void**>(&g_original_create_lobby);
        hooks::HookSpec leave = create;
        leave.name = "lobby ISteamMatchmaking::LeaveLobby (slot 15)";
        leave.site = vtable + 8 * kSlotLeaveLobby;
        leave.detour = reinterpret_cast<void*>(&detour_leave_lobby);
        leave.original = reinterpret_cast<void**>(&g_original_leave_lobby);
        installer.install(create);
        installer.install(leave);
        log_line("session: lobby hooks installed (vtable 0x%llx)", (unsigned long long)vtable);
    } catch (const std::exception& e) {
        log_line("session: lobby hooks failed: %s", e.what());
    }
}

void start_session() {
    std::string err;
    g_steam = ps::SteamAdapter::create(g_sink, &err);
    if (!g_steam) {
        g_steam_failed = true;  // do not retry every frame; the game keeps running without co-op
        log_line("session: Steam adapter unavailable (%s); co-op disabled", err.c_str());
        return;
    }
    const auto own = g_steam->OwnSteamId();
    session::SteamInterfaces ifaces{g_steam->matchmaking(), g_steam->networking(), *g_steam, *g_steam};
    g_session = std::make_unique<session::Session>(ifaces, own);
    log_line("session: started, own SteamID %llu", (unsigned long long)own);
    install_lobby_hooks();
    g_session->set_game_players(&g_game_players, [](std::int32_t category, std::int32_t id) -> std::optional<std::u16string> {
        auto repo = hooks::params::current_messages();
        return repo ? repo->lookup(category, id) : std::nullopt;
    });
    // Text (devirt 2.9/2.11): the override's value for id 0x52409 is the member count; the second locale is the game's.
    hooks::params::params_state().text_special_value = [] { return g_session ? g_session->member_count() : 0; };
    const std::string lang = g_steam->GameLanguage();
    hooks::params::set_game_language(lang);
    log_line("session: game language \"%s\"", lang.c_str());
}

void detour_run_callbacks() {
    g_original_run_callbacks();  // delivers this frame's callbacks to the sink
    try {
        if (!g_session && !g_steam_failed) start_session();
        if (!g_session) return;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = 0.0f;
        if (g_last.QuadPart) dt = float(double(now.QuadPart - g_last.QuadPart) / double(g_freq.QuadPart));
        g_last = now;
        if (dt > 1.0f) dt = 1.0f;  // after a stall (loading screen) do not burst the limiters
        g_session->tick(dt);
    } catch (const std::exception& e) {
        log_line("session: tick failed: %s", e.what());
    }
}

}  // namespace

void set_lobby_hooks_enabled(bool enabled) { g_lobby_hooks_enabled = enabled; }

void set_game_slots(const signatures::SignatureTable& t) {
    g_slots = {t.at(0x60), t.at(0x68), t.at(0xb8), t.at(0x190), t.at(0x198), t.at(0x1b0), t.at(0x188)};
    log_line("session: game slots serialize=0x%llx deserialize=0x%llx message=0x%llx", (unsigned long long)g_slots.serialize,
             (unsigned long long)g_slots.deserialize, (unsigned long long)g_slots.show_message);
}

std::optional<hooks::HookSpec> session_driver_hook() {
    QueryPerformanceFrequency(&g_freq);
    HMODULE steam_api = GetModuleHandleW(L"steam_api64.dll");
    FARPROC run = steam_api ? GetProcAddress(steam_api, "SteamAPI_RunCallbacks") : nullptr;
    if (!run) {
        log_line("session: steam_api64.dll / SteamAPI_RunCallbacks not found; co-op disabled");
        return std::nullopt;
    }
    hooks::HookSpec spec;
    spec.name = "session driver SteamAPI_RunCallbacks";
    spec.kind = hooks::Kind::Inline;
    spec.site = reinterpret_cast<std::uintptr_t>(run);
    spec.detour = reinterpret_cast<void*>(&detour_run_callbacks);
    spec.original = reinterpret_cast<void**>(&g_original_run_callbacks);
    return spec;
}

}  // namespace nrsc::dll
