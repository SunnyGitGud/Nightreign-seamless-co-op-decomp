# Session layer (`src/nrsc/session/`)

Platform-independent glue between the Steam callbacks / frame loop and `server/*` + `hooks::net::NetState`. Semantics from
`docs/phase3_vm_networking.md` sections 1-3; grades C confirmed, S strong, W weak, U unknown. Build: `cmake/session.cmake`
(needs the `server` and `hooks_net` modules; `session` is last in `NRSC_MODULES`). Tests: `tests/session/session_test.cpp` (31).

## Components

| File | Class | Original | Grade |
|---|---|---|---|
| `session_state.hpp` | `SessionState`, `StateFlag`, `kJoinError*`, `kPeerHandshaken/RemoteBit3/StateMask` | master state `M` | C (offsets), W (bit5 = party) |
| `join_flow.*` | `JoinFlow` | `0x1800643c0`, `0x180064120` | C |
| `frame_task.*` | `FrameTask`, `timer_fires` | `0x180061140` phases 1-5 | C (rules), S/W (names) |
| `party.*` | `PartyTracker` | none (calls `hooks::set_party_session_active`) | W |
| `lobby_lifecycle.*` | `LobbyLifecycle` | none: RequestLobbyData / JoinLobby / CreateLobby / LeaveLobby | **W, all of it** |
| `steam_session_iface.hpp` | `ISteamLobbyControl`, `ISteamSessionInfo` | vtable calls the server iface lacks | S (public Steamworks signatures) |
| `session.*` | `Session` | facade | mine |

`SessionState` wraps `server::CoreState` (flags = `state_flags`, join target = `join_request_lobby/friend`), so `Lobby`,
`MessageBus`, `on_session_request` and `JoinFlow` all see the same memory as the original's `M`. `join_error` (M+0x140) is the one
extra field. The party mode word (`hooks` blob+0, bits 0|5 = 0x21) is a different copy than `SessionState::flags`: `PartyTracker`
mirrors it into `kParty` (0x20) of SessionState only; bit 0 of the blob is written by the hook setter.

## Behaviour

* `JoinFlow::on_lobby_data_update` is the exact table of phase3 1.3 (lobby != join target or `kJoinPending` clear -> no write; then
  pending cleared; `!success` -> target cleared, +0x140 kept; type != master -> error 0xE0C1; version != "1.1.3" -> 0xE0C2; match ->
  target kept). `member` is unused.
* `FrameTask::tick` runs (1) handshake resend (timer 1, peers with `(flags&0x11)==1`), (2) per-peer receive on channel 1 through
  `NetState::pop_received` (first message on an active, un-handshaken peer: `|=0x10`, `|=0x40` if payload != 0 else `field3c = 1`,
  one reply; later messages drained), (3) ping poll (timer 2, `(flags&0x11)==0x11`), (4) counters (`X+0x170/0x174`), (5) member
  count = bytes / 0xF0. Limiter: `acc += dt; fire iff acc >= 1.0f / rate` (rate < 0 always, 0 never). Phase (7) (mirror global
  0x180183018) is not modelled (U, no readable consumer). Phase (6): `post_job` / `run_one_job`, one job per tick, W/U (the original's
  queue item type is unknown; a plain `std::function` queue stands in).
* Ping: `server::Peer` has no `ping_ms` (server/* is owned elsewhere), so pings live in `FrameTask` (`ping_ms(id)`, -1 = none).
  When `Peer::ping_ms` appears, store it there as well.
* `Session::tick(dt)`: while attached, `bus.poll()` (W: the original's caller of `0x180063110` is not recovered), `FrameTask::tick`,
  `PartyTracker::update`; then one job outside the lock. Inert while detached.
* Attach/detach: `on_lobby_enter` success and adopted by `server::Lobby::OnLobbyEnter` -> `net.attach(bus, peers)`.
  `leave_session()` (or destruction) -> `LeaveLobby`, detach, all peers reset, lobby/created bits cleared, party word cleared.
  Peer-left chat updates reset the peer, drop its ping and re-evaluate the party.
* Message id 3 handler (the peer-channel receiver, body is protected code, W): packet `[3][ch][0][u32 len][payload]` -> payload is
  queued with `NetState::enqueue_received(sender, ch, payload)`. Ids 0 and 1 are forwarded to optional `set_sub_header_handler`
  / `set_broadcast_handler` (U bodies), id 2 is the C no-op.
* Party condition (W): `kInLobby` and at least one peer with `(flags & 0x11) == 0x11`. The hook setter is called only on change.

## W / U summary

W (standard Steam flow, all in `LobbyLifecycle` plus the two calls in `Session`): GameLobbyJoinRequested -> `RequestLobbyData`
(dropped target when it cannot be sent); data accepted -> `JoinLobby`; LobbyEnter failure or success consumes the join target;
host: `host_start_session` -> `CreateLobby(friends-only, 6)` with `lobby_create_pending`, `LobbyCreated` tags the data keys
(`server::Lobby::OnLobbyCreated`, C), LobbyEnter then adopts; party condition; `bus.poll()` in `tick`; default config values.
U: default timer rates (1.0 Hz chosen, configurable via `SessionConfig`/`set_rates`), `core.channel` (0) and `max_len` (0x400),
who sets M bit 3 (`set_bit3`), the source of the member vector, job queue payload, `X+0x140`, phase (7).

## Thread safety

One mutex inside `Session` guards all state and every dispatcher/`tick`; it is safe to drive it from one non-game thread while
others call the observers or `post_job`. Not covered: `hooks::net::NetState` has no lock (not editable here), so while attached the
game-thread hooks and `Session::tick` (`pop_received` / `enqueue_received`) race on its receive-queue map. Either tick from the game
thread, or add a lock to `NetState` (open point). Handlers installed with `set_*_handler` run under the mutex.

## Adapter wiring

Construct after `hooks::groups::net()` (which recreates `NetState`; the Session holds a reference). Register the 8 callbacks of
`server::kRegisteredCallbacks` and forward to the `Session::on_*` functions; call `tick(dt)` each frame; user action "start a
session" -> `host_start_session()`, "leave" -> `leave_session()`.
