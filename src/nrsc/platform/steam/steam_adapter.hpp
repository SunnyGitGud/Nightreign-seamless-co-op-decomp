#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "nrsc/platform/steam/steam_callbacks.hpp"
#include "nrsc/server/steam_iface.hpp"
#include "nrsc/session/steam_session_iface.hpp"

// Windows-only adapter that implements server::ISteamMatchmaking / ISteamNetworkingMessages on top of the Steamworks
// FLAT API exported by the game's steam_api64.dll (resolved with GetModuleHandleW + GetProcAddress; no SDK, no import
// library). See docs/steam_adapter.md. The header itself is portable; the implementation (steam_adapter_win.cpp) is only
// compiled for WIN32.
namespace nrsc::platform::steam {

class SteamAdapter : public session::ISteamLobbyControl, public session::ISteamSessionInfo {
public:
    virtual ~SteamAdapter() = default;

    // Resolves steam_api64.dll (must already be loaded in the process), the interface accessors and every flat
    // function; registers the 8 callbacks with SteamAPI_RegisterCallback. Returns null (and fills *error) if the module
    // or any required export is missing. `sink` must outlive the adapter. Windows only: null elsewhere.
    static std::unique_ptr<SteamAdapter> create(ISteamCallbackSink& sink, std::string* error = nullptr);

    virtual server::ISteamMatchmaking& matchmaking() = 0;
    virtual server::ISteamNetworkingMessages& networking() = 0;
    // The game's ISteamMatchmaking object (SteamAPI_SteamMatchmaking_v009()); its vtable is the one the original
    // patches (slots 13 CreateLobby and 15 LeaveLobby, docs/devirt.md 2.11).
    virtual void* matchmaking_object() = 0;

    // Session-layer interfaces (src/nrsc/session/steam_session_iface.hpp) are implemented by the same object:
    //   ISteamLobbyControl::CreateLobby / JoinLobby / LeaveLobby / RequestLobbyData (results arrive through the sink),
    //   ISteamSessionInfo::GetSessionConnectionInfo(remote, ping_ms).
    // ISteamUser::GetSteamID (own id), 0 if unavailable.
    virtual SteamId OwnSteamId() = 0;
    // ISteamApps::GetCurrentGameLanguage (the name for the second locale load, devirt 2.9); empty if unavailable.
    virtual std::string GameLanguage() = 0;
};

}  // namespace nrsc::platform::steam
