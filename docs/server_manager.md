# nrsc.dll server_manager

Source path: `nrsc\mod\server_manager\server_manager.cpp` (`.rdata 0x18014c71d`), one readable reference at `0x18006110c`
(inside `0x180061060`, line `0x9b`). Grades: **C** confirmed, **S** strongly inferred, **W** weakly inferred, **U** unknown.
Evidence: `evidence/server_manager/` (`decoded_strings.txt`, `call_graph.txt`, `curl_options.txt`). Pseudocode from REA/Ghidra 12.1.2.

## 0. Headline findings

1. **There is no HTTP server protocol in readable code (C).** No URL, host, header or JSON field name for a web API exists in
   `.rdata` or in any decoded stack string (`evidence/server_manager/decoded_strings.txt`). The "server" is **Steam itself**:
   a Steam lobby (ISteamMatchmaking 009), Steam P2P messages (ISteamNetworkingMessages 002) and one Steam leaderboard
   (ISteamUserStats 012). The name `server_manager` is the mod's, not a web client's.
2. **libcurl 7.76.1 (schannel) is embedded, but nothing readable calls it (C).** The only `curl_easy_setopt` caller in `.text`
   is libcurl's own DoH helper `0x1800df050`. No static edge from any mod function reaches `curl_easy_init/perform/setopt`.
   Whether protected code uses it is **U**. *Update 2026-10-15 (devirt, docs/devirt.md §2.7):* protected code **does** use it. The VM init
   routine `0x180040630` calls `curl_global_init(3)`, `curl_easy_init` (`0x18008e050`), sets URL, WRITEFUNCTION (`0x180033210`, copies into a
   zeroed `new[0x30e8]` buffer, no bound check), MAXFILESIZE `0x30e0` and WRITEDATA, then `curl_easy_perform` (`0x18008e110`) (S). The URL is
   built inside the VM. An "Update required" fatal MessageBoxA on the same path suggests a version/update check (W).
   *Real-image run (docs/devirt.md §2.8):* three fetches, same four options each (C). The URLs were decoded but never contacted:
   `https://raw.githubusercontent.com/LukeYui/NightreignSeamlessCoopRelease/refs/heads/main/VERSION`,
   `…/yuiamoroll/NightreignSeamlessCoopRelease/refs/heads/main/SUPERNIGHTLORD2` and `…/ENDLESSNIGHT`. The body is tokenised and
   compared (4-byte memcmp) with the local `"1.13"` (W for "version compare"); the token format and mismatch handling are **U**. nlohmann::json is used only by the locale loader and a few helpers (`0x180053xxx..0x18005fxxx`).
3. The heart of server_manager (lobby host/join, the ctor, handler registration, the per-frame tick) sits in protected code
   (`0x180061140` VM, 8025 B; `0x180064120` VM, 605 B; `0x180040630` VM, 28443 B is in the hooks area). About 90 readable functions around
   them are the Steam-facing pieces and are documented below.

## 1. libcurl: identification and exclusion (C)

| Item | Evidence |
|---|---|
| Version 7.76.1 | `"CLIENT libcurl 7.76.1"` at `0x18015ac10/40/70` |
| TLS backend schannel; no HTTPS proxy | `0x1801522b0 "schannel"`, `0x18015db90 "…built without the HTTPS-proxy support"` |
| Extras compiled in | alt-svc, cookies/HSTS text, DoH, SOCKS5 GSS, RTSP, FTP, NTLM/Negotiate, HTTP/2 framing text, no HTTP/3 |
| Library range | roughly `0x180085000..0x1800f3fff`, plus strings `0x18014e300..0x180156fff` and `0x18015d000..0x18015fd00` |
| curl option table | `evidence/server_manager/curl_options.txt`: 26 option numbers, all in `0x1800df050`, all stock DoH defaults |

Exclude from reconstruction: the whole range above, crashpad (`0x18002d310` client start, `0x1801547xx`, `0x18015d8xx..0x18015f4xx`),
nlohmann::json (`0x180053020..0x18005f525`) and the moodycamel queues. TLS, proxy, timeout and retry settings of an
application request could not be recovered because no application request exists in readable code (**U** for protected code).

## 2. What server_manager is (S)

A `Server`-like object with these parts. All field offsets are from decompiled readable code (C) but class and field names are mine.

| Part | Evidence |
|---|---|
| **Core** (`ctx+0x18`): lock at `+0x78`, a "closed" flag at `+0xc4` (`0x7ffffffe` poison), `ISteamNetworkingMessages*` at `+0x50`, flags `+0x158` (lobby-create pending), `+0x159/+0x15a` (leaderboard add/refresh pending), lobby id `+0x120`, owner id `+0x128` | `0x180063110`, `0x180064a50`, `0x18003d760` |
| **Peer list** `ctx+0x28`: vector of `Peer*`; `peer+8` SteamID, `peer+0x128` flags (bit0 active, bit5 = lobby owner/host), per-peer receive queues `peer+0x398 + ch*0x268` (up to 999) and `peer+0x96770 + ch*0x268` (9 channels) | `0x18003f4a0`, `0x180040240`, `0x180040370`, `0x180064570` |
| **Message handler map** `ctx+0x78` and `ctx+0x88`: `std::map<u8 or int, std::function>` | `0x180063110`, `0x18003f4a0`, `0x180063570` |
| **Steam callbacks** (7 `CCallbackBase`-like objects, dtor unregisters via the IAT slot `0x1801612f8`, likely `SteamAPI_UnregisterCallback`, **W**) | `evidence/.../call_graph.txt` |
| **Leaderboard client** | `0x18003d420..0x18003d760` |
| Concurrent queues (moodycamel `ConcurrentQueue`, block 32 slots x `0x6028` B pool) | ctor `0x180064c60`, dtors `0x180065420`, `0x18003ff20` |

### 2.1 Lobby (C for the strings, S for the flow)

- Created lobby (callback `0x180064a50`, `LobbyCreated_t`: result at +0, lobby id at +8): if the create-pending flag `+0x158` was set, calls
  `SetLobbyData(lobby, "nrsc_lobby_type", "nrsc_ynx3_seamless_master_lobby")` and
  `SetLobbyData(lobby, "nrsc_lobby_version", "1.1.3")` (vtbl `+0xa0`). Both true: stores lobby id `+0x120`, owner `+0x128` and sets state flag bit 2.
- Entered lobby (callback `0x1800647b0`): `GetLobbyData(lobby,"nrsc_lobby_type")` is `memcmp`ed (0x20 bytes) with the master-lobby literal;
  mismatch means "not a seamless lobby" and the callback does nothing. Match: enumerate members (`GetNumLobbyMembers`, `GetLobbyMemberByIndex`),
  call `0x18003d920` to create a `Peer` for each member not yet present, set peer bit5 for the owner, store lobby/owner, state bit 0.
  **Note:** it does *not* compare `nrsc_lobby_version` in readable code (**U** whether protected code does; matching strings in
  `MODMESSAGE_YUINEXUSWRONGSSR` suggest a version/SSR check exists).
- Chat update (`0x1800645b0`, `LobbyChatUpdate_t`: user at +8, state at +0x18): states Left/Disconnected/Kicked/Banned (mask `0x10114`)
  for a member of our lobby: forwards a leave event (`0x18003ee30` peer reset + `[cb]->vtbl+0x18`); Entered (1): ensures a peer slot.
- **Session secret.** The `"[ -- ]"` secret from settings (`0x180037c50`) does **not** reach any readable server_manager code (C): the lobby
  data use only the fixed strings above and the version `1.1.3`. The settings function and the P2P/leaderboard code share no readable data (**U**
  for protected glue).

### 2.2 P2P wire format (C for the readable receive/send paths)

All traffic is `ISteamNetworkingMessages` (vtbl+0 `SendMessageToUser`, vtbl+8 `ReceiveMessagesOnChannel`), identity type `0x10`
(SteamID), channel `ctx+4`. Packet header 7 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | u8 | message id (key of the handler map) |
| 1 | u8 | sub-channel / peer channel (`<9` for the per-peer queue) |
| 2 | u8 | flag; if 0 the u32 at +3 must be `<= ctx+0xc` (max length) |
| 3 | u32 | payload length, must equal received size - 7 (`0x180040130/240`) |
| 7 | | payload |

Handlers receive `(ctx, data, sender peer, remaining = size - 7)`. Known message ids:
- `0x03` data (sent by `0x18003f030`: channel in byte 1; flags 9 = Reliable|NoNagle if the target is the same as the current host id, else 5)
  and a 1-byte control message `0x103` (`0x18003f2c0`, payload = bit 3 of the sender state, flags 9). Receivers `0x180040130`
  (ch-less broadcast queue at `ctx+0x130`), `0x180040240` (peer channel `<9`) and `0x18003f4a0` (an 8-byte sub-header: u32 channel, u32 len at +7/+0xb, `<999`).
- Other ids are registered in protected code (`0x180063570` has no readable caller): **U**.
Handler stubs live at `0x180064100` (std::function call forwarder). Message ids beyond 3 and their meaning are **U**.

### 2.3 Leaderboard (C)

- `0x18003d420`: `FindOrCreateLeaderboard("yui_nexus_3_seamless_deep_night_leaderboard_000", sort=2 Descending, display=1 Numeric)`
  (ISteamUserStats vtbl+0xb0). The result is bound to a CCallResult at `+0x80`; state=2.
- `0x18003d5e0`: `DownloadLeaderboardEntriesForUsers(handle, [ownSteamID], 1)` (vtbl+0xe8), state=4.
- `0x18003d6a0`: loops `GetDownloadedLeaderboardEntry` (vtbl+0xf0, 2 details); if the entry is ours, stores details[0] at `+0x4c`.
- `0x18003d760`: `UploadLeaderboardScore(handle, ForceUpdate=2, details{tier, value})` (vtbl+0xf8). Tier = index of the first threshold in the sorted
  vector (`+0x30..+0x38`) greater than the stored value (`+0x50`); `+0x54` is an accumulator added when `core+0x159` is set; `core+0x15a` forces a refresh.
  The game side is `YuiNexusNetRankingDB` (the fatal string at line 155 names it); what the tiers mean is **U**.

### 2.4 Dispatch objects feeding game state (S)

Four task/listener classes (vtables `0x180147b00/b40/b80/bc0`; RTTI is stripped) share `this+0x30 = owner`:
- `0x180063110` (run slot): the P2P pump described in 2.2.
- `0x180061060` (run slot): looks up key `[owner->[0]+0x70]+0x68` (int) in a `std::map<int, std::function>` at `[...+0x70]` and calls it with
  `(&key,&arg)`; absent key is fatal: `FUN_180040470("YuiNexusNetRankingDB", server_manager.cpp, 0x9b)`. This is the only source-path xref, so it is the
  "get registered game component" check that fires when a YuiNexusNetRankingDB handler is missing (S).
- `0x180061000` (run slot): iterates a list at `[owner+0x70]` and copies `{u32 value, u8 flag}` from each node into the pointed-to targets
  (a settings/params snapshot push, **W**).
- `0x180061140` (run slot): **VM, 8025 B** (`devirt/out/0x180061140*.json` is a partial trace; it reads `owner->[0]->[+0x80]`, writes a 4-byte field at
  `+0x148`, reads a float 1.0f from `0x180145d70` and a field `+0x14c`). Calls `0x18003f030` and `0x180040370` (send/receive helpers), so it is
  part of the game-state to message bridge. Semantics **U**.
- `CSQuickMatchLobbyContext`, `PlayerNetworkSession`, `PartyMemberInfo`, `CSChaosMatchRate*Job` appear only as RTTI strings in the
  signature function `0x1800655a0` (class-name scans, `signatures.cpp`); no readable server_manager code touches them. How the lobby
  state reaches CSQuickMatchLobbyContext is **U** (protected, or via hooks owned by WP1).

## 3. Triggering, threading, errors

| Question | Answer |
|---|---|
| Triggered from | Not visible. No readable code references the four vtables, the ctor `0x180064c60`, the map emplace `0x180063570` or the leaderboard starters. Instantiation and the per-frame call come from protected code (hooks/init), **U**. |
| Threads | Steam callbacks and `SteamNetworking` run on the callback thread; the mod uses lock-free queues and a core lock (`0x18008f88c`/`0x18008f894`, SRW/mutex wrappers) around every Steam call. Message receive is a poll (`0x180063110`). No `CreateThread` exists for server_manager in readable code. **S** |
| Timeouts / retries | None visible. Steam handles reliability (flag 9 Reliable). **U** for protected logic. |
| Fatal paths | `0x180040470` prints `"A fatal error has occurred and the process has been aborted:\n<what> (<file>:<line>)"` plus mod version `1.1.3` and `MessageBoxA` (title `Seamless Coop %s - Fatal Error`). Core lock failure: `FUN_18008ee9c(5)`, poisoned core (`+0xc4 == 0x7fffffff`): `FUN_18008ee9c(6)` (std::system_error style). Out-of-range queue index: `0x18003f010` (invalid array subscript). |
| Locale messages | `MODMESSAGE_YUINEXUS{NOSEAMLESS, WRONGSSR, BEGIN*PLAYER, PLAYERCOUNTREMAINING, PAUSED, RESUMED}` exist but have no readable xref, so they are shown from protected code. |

## 4. Proposed C++ module (not written)

```
nrsc/server/
  steam_iface.h      // abstract ISteamMatchmaking/NetworkingMessages/UserStats subset (injected, tests mock it)
  wire.h/.cpp        // 7-byte header encode/validate; (id, channel, flag, len); max-length rule
  lobby.h/.cpp       // kLobbyType, kMasterLobby, kVersion consts; OnLobbyCreated, OnLobbyEnter(memcmp 0x20), OnChatUpdate
  peer.h/.cpp        // Peer{steamid, flags(active,host), channel queues}; peer list ops
  message_bus.h/.cpp // handler map u8->function, Poll(): receive, filter by known peer, dispatch, release; Send(peer, ch, data, reliable)
  leaderboard.h/.cpp // Find/Download/Upload state machine (states 2,4), tier from thresholds
```
Tests: wire round-trips and invalid headers (`len != size-7`, `flag==0 && len>max`), lobby-data check against the literals, tier computation,
poll dropping messages from unknown or inactive peers, fatal-on-missing-handler.

## 4a. Corrections from the C++ integration (2026-10-15)

Checked against objdump during the `src/nrsc/server/` integration; these replace the statements above where they differ.

| # | Corrected statement | Grade | Evidence |
|---|---|---|---|
| 1 | Send flags in `0x18003f030` are `(channel not in int32 vector ctx+0x48..0x50) * 4 + 5`: **9** when the channel is absent, **5** when present. Nothing to do with the host. Who fills the vector is U | C | `0x18003f0af` (`std::find` `0x1800901a0`), `sete r13b`, `lea eax,[r12*4+5]` at `0x18003f13e` |
| 2 | The "0x103" control message is id **3**, channel **1** (the word `0x0103`): packet `03 01 00 01 00 00 00 b`, `b = (state>>3)&1`, flags 9 | C | `0x18003f2c0` |
| 3 | The poll (`0x180063110`) only checks `size >= 8` and, if flag==0, `len <= max`. `len == size-7` is checked by the receivers `0x180040130/0x180040240` | C (poll) / S (receivers) | asm |
| 4 | Leaderboard download (`0x18003d6a0`) stores 8 bytes at `+0x4c`: details[0] (tier) and details[1] (value, `+0x50`). Zero entries zero both | C | asm |
| 5 | Upload (`0x18003d760`): add flag only uploads {stored tier, value+acc} without recomputing. Refresh recomputes the tier, replaces the value with a representative one (0 / thr[0]/2 / (thr[t-1]+thr[t-2])/2, u32 wrap) and uploads only on change. Both flags: add first, then refresh. Thresholds are 8-byte elements (u32 at +4), unsigned compare, so equal goes to the next tier | C | asm |
| 6 | LobbyCreated: result at +0 is never read; the pending flag is cleared by atomic exchange first; the owner stored is core+8 (our own id, S) | C | `0x180064a50` |
| 7 | LobbyEnter: skips our own id and members with an active peer, claims the first inactive slot of a preallocated pool (size U), sets the host bit only on new peers from `GetLobbyOwner` (vtbl+0x118). Chat-update Entered does not set the host bit and ignores ourselves or a foreign lobby | C | `0x1800647b0`, `0x1800645b0` |
| 8 | The peer-marking branch of `0x180061140` requires the flag byte to be exactly `1` (a host peer `0x21` is not re-marked) | C | devirt trace + asm |

## 4b. Startup wiring (real-image emulation, 2026-10-15; docs/devirt.md 2.9)

Reconstructed in `src/nrsc/server/callbacks.{hpp,cpp}` (tests `tests/server/callbacks_test.cpp`).

| Item | Value | Grade |
|---|---|---|
| Message handlers (map emplace `0x180063570`) | 0 → `0x18003f4a0` (sub-header receiver), 1 → `0x180040130` (broadcast receiver), 2 → `0x180004c80` (`mov al,1; ret`, ignored), 3 → `0x180040240` (peer-channel receiver) | C (ids, handlers) / S (roles) |
| Steam callbacks, in order | `0x201` LobbyCreated → `0x180064a50`; `0x1f8` LobbyEnter → `0x1800647b0`; `0x1fa` LobbyChatUpdate → `0x1800645b0`; `0x4e3` SessionRequest → `0x1800644c0`; `0x4e4` SessionFailed → `0x180001080` (no-op); `0x501` RelayNetworkStatus → `0x180064430`; `0x14d` GameLobbyJoinRequested → `0x1800643c0`; `0x1f9` LobbyDataUpdate → `0x180064120` | C (ids, slots) / S (names) |
| SessionRequest | under the core lock: closed core (`+0xc4 == 0x7fffffff`) → `FUN_18008ee9c(6)`; else `AcceptSessionWithUser` (ISteamNetworkingMessages vtbl+0x10) | C |
| RelayNetworkStatus | first dword stored at core+0x70 | C |
| GameLobbyJoinRequested | {lobby, friend} → core+0x130/+0x138, core state `|= 0x10` | C |
| Peer pool | 5 slots (queue ctor `0x180064c60` runs 5 × 1009) | C |
| Task objects | vtables `0x180147b00/b40/b80/bc0`, each `+0x28 = game+0xfebc80`; owner at `this+0x30`, core at `owner+8` | C (observed) / U (scheduler) |

This resolves open item 1 for handler registration and callback ids, and open item 4 (callback ids). Senders for ids 0–2, the task
scheduler and the LobbyDataUpdate body (`0x180064120`) are still open.

## 5. Open unknowns

1. Everything behind `0x180061140` (VM, 8 KB), `0x180064120` (VM, 605 B) and the protected callers: lobby host/join sequencing, handler registration
   and message ids other than 3, the quick-match/CSQuickMatchLobbyContext bridge, locale-message triggers, the `nrsc_lobby_version` check.
2. Whether protected code uses libcurl at all (update check, telemetry). No URL string decodes in readable code (C), so a URL would have to be built or decoded in the VM.
3. How the settings secret `"[ -- ]"` (docs/baseline.md §2) is consumed; it does not appear in server_manager (C).
4. Steam callback ids (`SteamAPI_RegisterCallback` calls are in protected code); my labels LobbyCreated/LobbyEnter/LobbyChatUpdate come from field use (**S**).
5. IAT names for slots `0x1801612c0` (MessageBoxA, S), `0x1801612f8` (SteamAPI_UnregisterCallback, W) are not recoverable from the zeroed hint table.
