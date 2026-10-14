#include "nrsc/dll/session_driver.hpp"

#include <windows.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/dll/init.hpp"
#include "nrsc/platform/steam/steam_adapter.hpp"
#include "nrsc/platform/steam/steam_callbacks.hpp"
#include "nrsc/session/session.hpp"

namespace nrsc::dll {
namespace {

using platform::steam::ISteamCallbackSink;
namespace ps = platform::steam;

// W (reconstruction policy): the original's lobby creation is in protected code that was not recovered
// (docs/phase3_vm_networking.md section 4). The host creates a lobby once the game has run for this long without
// joining one, so a friend can join through a Steam invite / "Join game".
constexpr float kAutoHostDelaySeconds = 20.0f;

std::unique_ptr<session::Session> g_session;
std::unique_ptr<ps::SteamAdapter> g_steam;
bool g_steam_failed = false;
bool g_auto_host_done = false;
bool g_auto_host_enabled = true;
float g_uptime = 0.0f;
LARGE_INTEGER g_last{}, g_freq{};

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
        g_auto_host_done = true;  // the player chose to join: never auto-host over it
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
        g_uptime += dt;

        if (g_auto_host_enabled && !g_auto_host_done && g_uptime >= kAutoHostDelaySeconds && !g_session->in_lobby()) {
            g_auto_host_done = true;
            log_line("session: auto-host (W): %s", g_session->host_start_session() ? "CreateLobby sent" : "skipped");
        }
        g_session->tick(dt);
    } catch (const std::exception& e) {
        log_line("session: tick failed: %s", e.what());
    }
}

}  // namespace

void set_auto_host_enabled(bool enabled) {
    g_auto_host_enabled = enabled;
    if (!enabled) log_line("auto-host disabled (nrsc_disable_hooks.txt)");
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
