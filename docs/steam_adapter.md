# Steam adapter (src/nrsc/platform/steam)

Windows-only implementation of `server::ISteamMatchmaking`, `server::ISteamNetworkingMessages`,
`session::ISteamLobbyControl` and `session::ISteamSessionInfo` over the Steamworks **flat API** exported by the game's
`steam_api64.dll` (only flat exports are used). No SDK, no import library:
`GetModuleHandleW(L"steam_api64.dll")` + `GetProcAddress`.

## Files
- `steam_decode.{hpp,cpp}`: portable POD layouts and decoders (Linux tests in `tests/platform/steam_decode_test.cpp`).
- `steam_callbacks.{hpp,cpp}`: `ISteamCallbackSink` (one method per callback) and `dispatch_steam_callback(id, bytes, sink)`.
- `steam_adapter.hpp`: `SteamAdapter` (abstract, implements the two session interfaces; `matchmaking()`, `networking()`,
  `OwnSteamId()`); `steam_adapter_win.cpp`: the implementation, compiled only for WIN32.
- `cmake/steam_adapter.cmake`, listed in `NRSC_MODULES` (top-level CMakeLists): decode/dispatch for both builds,
  adapter + tests where applicable.

## Exports used (all confirmed present in the game's steam_api64.dll via llvm-objdump -p)
Accessors: `SteamAPI_SteamMatchmaking_v009`, `SteamAPI_SteamNetworkingMessages_SteamAPI_v002`, `SteamAPI_SteamUser_v021`
(optional; NOTE this build exports only v021, not v023), `SteamAPI_ISteamUser_GetSteamID` (optional).
Matchmaking: `SteamAPI_ISteamMatchmaking_{GetNumLobbyMembers, GetLobbyMemberByIndex, GetLobbyData, SetLobbyData,
GetLobbyOwner, CreateLobby, JoinLobby, LeaveLobby, RequestLobbyData}`.
Messages: `SteamAPI_ISteamNetworkingMessages_{SendMessageToUser, ReceiveMessagesOnChannel, AcceptSessionWithUser,
GetSessionConnectionInfo}`, `SteamAPI_SteamNetworkingMessage_t_Release`.
Callbacks: `SteamAPI_RegisterCallback`, `SteamAPI_UnregisterCallback`, `SteamAPI_RegisterCallResult`,
`SteamAPI_UnregisterCallResult`.
`SteamAPI_SteamNetworkingIdentity_*` exports exist but are not needed: the SteamID identity is built directly
(`{type 0x10, size 8, id}`, verified against the 0x800000010 qword at 0x18003f0fd). ISteamUserStats (leaderboards) is
NOT implemented (call-result based, not requested; `SteamAPI_SteamUserStats_v012` is exported).

## Layouts (64-bit, Steam pack 8; S = public Steamworks headers, C = confirmed by nrsc.dll call sites)
| struct | layout |
|---|---|
| SteamNetworkingIdentity (136) | i32 type @0 (0x10 SteamID), i32 size @4, union[128] @8 (SteamID u64 @8) (S,C) |
| SteamNetworkingMessage_t | data* @0, i32 size @8, identity @16 (type @0x10, id @0x18), Release fn @184 (0xb8), i32 channel @192 (S,C) |
| CCallbackBase (16) | vptr; Run(void*), Run(void*,bool,SteamAPICall_t), GetCallbackSizeBytes (no dtor); u8 flags @8, i32 id @12 (S) |
| LobbyCreated_t 0x201 (16) | i32 result @0, u64 lobby @8 (C) |
| LobbyEnter_t 0x1f8 (24) | u64 lobby @0, u32 perms @8, u8 locked @12, u32 response @16 (S) |
| LobbyChatUpdate_t 0x1fa (32) | lobby @0, user changed @8, making change @16, u32 state @24 (C for @0/@8/@0x18) |
| LobbyDataUpdate_t 0x1f9 (24) | lobby @0, member @8, u8 success @16 (S) |
| GameLobbyJoinRequested_t 0x14d (16) | lobby @0, friend @8 (C) |
| SessionRequest_t 0x4e3 (136) | identity @0 (C) |
| SessionFailed_t 0x4e4 (696) | SteamNetConnectionInfo_t: identity @0, i32 state @176, i32 end reason @180 (S) |
| RelayNetworkStatus_t 0x501 | i32 avail @0; only 4 bytes are ever read (C); reported size 712 is S, unverified |
| RealTimeStatus (SteamNetConnectionRealTimeStatus_t) | i32 state @0, i32 ping @4 (S); scratch buffer of 1 KiB is passed |

## Callback delivery
All 8 callbacks are registered in the original order with `SteamAPI_RegisterCallback`; the sink is called on whichever
thread pumps `SteamAPI_RunCallbacks` (the game's). `CreateLobby`/`JoinLobby` return a `SteamAPICall_t`; the adapter also
registers a call result (`SteamAPI_RegisterCallResult`) for LobbyCreated_t / LobbyEnter_t so the result is delivered even
if Steam does not rebroadcast it as a plain callback. If both fire the sink sees the event twice: sink handlers must be
idempotent (`server::Lobby` is: create-pending is cleared atomically, peer slots are only claimed when free). An
`ioFailure` call result is dropped (no data). Unverified at runtime (static-only scope).

## Wiring in the DLL init
```cpp
#include "nrsc/platform/steam/steam_adapter.hpp"
// Steam must already be initialised (steam_api64.dll loaded, SteamAPI_Init done by the game) or accessors return null.
struct MySink : nrsc::platform::steam::ISteamCallbackSink { /* forward to server::Lobby / on_session_request ... */ };
std::string err;
auto steam = nrsc::platform::steam::SteamAdapter::create(sink, &err);   // null + err on failure; registers the 8 callbacks
// server::ISteamMatchmaking&        mm  = steam->matchmaking();
// server::ISteamNetworkingMessages& net = steam->networking();
// session::ISteamLobbyControl&      lc  = *steam;   session::ISteamSessionInfo& si = *steam;
```
Destroying the adapter unregisters everything. Keep it alive for the DLL's lifetime and destroy before steam_api64 unloads.
