# Rejoin research, track 1: what happens when a peer disconnects mid-expedition

Target: `nightreign.exe` v1.03.3 (sha256 9545e873...), image base 0x140000000. Grades: C confirmed (disassembly read, or live
recording), S strong, W weak, U unknown. Static work was done with capstone on the exe (the REA/Ghidra session timed out during
auto-analysis, 3 tries x 330 s); all addresses below were read from disassembly. Runtime rows cite
`evidence/runtime/20261016-025836_session-record/` (guest side, host dropped) and `20261016-030701_session-record/`.
Not covered: a host whose guest drops (no recording exists), and anything inside the platform network layer, which is
control-flow obfuscated (jumps into scattered chunks, e.g. `0x140df47f0`, `0x140debf60`).

## 0. Objects

| Name | Address | Grade |
|---|---|---|
| `[0x143c21ec8]` = CSSessionManagerImp (RTTI `CS::CSSessionManagerImp`, vtable 0x142ca1fd0, 3 slots) | ctor `0x140dee0f0`, dtor `0x140def2f0` | C |
| `Update(this, FD4Time*)` | `0x140df2a20` (0x5d0 bytes, event pump + state machine) | C |
| WorldChrManImp = `[0x143c0f0a8]`; local PlayerIns `+0x174e8`; PlayerIns ChrSet `+0x9a40` | | C (`0x140df5020`, `0x14051b2b0`) |
| Member owner `[0x143c21fc8]` vector at `+0x30..+0x38` is what nrsc calls the member vector. The same 0xF0 records are CSSessionManagerImp `+0x78..+0x80` | | C |

## 1. CSSessionManagerImp layout and state machine (item 3)

Two state dwords. **+0xc = phase**, **+0x10 = flow state** (what docs/devirt.md calls "+0x10 < 5 / >= 5").

| Offset | Meaning | Grade |
|---|---|---|
| +0x08 | platform session id (0 = none) | S |
| +0x0c | phase: 0 idle, 1 create/join pending, 2 created (host), 3 **session owner/host** (CORRECTED: not both roles; guest is 6, see section 6), 4/5 join-in-progress (`0x140df0f70`), 6 **guest** (set by `0x140df5930`; `0x140df6890` flips 6<->3 on owner change), 7 **leaving** (`0x140df1180`) | C for 0,3,7 (recording: 3->7->0); S rest |
| +0x10 | flow state, see below | C |
| +0x14 | float: **10.0 s** written at the end of every member removal (`0x140df5466`); -1.0 at ctor | C |
| +0x1a / +0x1b / +0x1d / +0x1f | pending-teardown flag / timer-flag / "leave requested while joining" / "party was multi" | S |
| +0x20 | byte countdown | W |
| +0x24 / +0x28 / +0x2c | pending async handles: create / join / leave | S (live: +0x2c = 6 while leaving) |
| +0x30 / +0x38 / +0x40 / +0x48 | sub-managers (+0x48 = inner FSM with its own +0x10 state and platform error codes 0x8102xxxx / 0x8110xxxx) | S |
| +0x60 | pointer to platform session object; its event poll is vslot +0x68, leave is +0x38, create +0x08, join +0x20, send +0x28 | S |
| +0x70 | vector (peer ids) | W |
| +0x78 / +0x80 | **member vector**, 0xF0 per record (divide-by-240 magic seen) | C |
| +0x160, +0x170 (=3 at ctor: max members, copied to create params `+8`), +0x178, +0x180 / +0x190 (timers), +0x1a0, +0x1a8, +0x1ac (float -1 = off), +0x1b8 (sub-object, vslot +0xc0 = update) | | S |
| +0x684 / +0x688 / +0x690 | list of timed pending requests; entries age by dt, **dropped at > 35.0 s** (const `0x142c11a7c`) | C |
| +0x6b8 | last error code (u64; high dword compared with 0x81040007, 0x81170104) | C |
| +0x74c | float timer, clamped at 86400.0 s (state 7) | C |

**Member record (0xF0)**: `+0x10` SteamID (u64); `+0xd0` PlayerGameData slot index; `+0xd4` i32 other index (-1 none);
`+0xd8` leave reason (u64, written by OnMemberLeft); `+0xe0`, `+0xe8` bytes passed to the removal notifiers;
`+0xe1` is local player; `+0xe2` connected/ready (set `0x140df5eb4`, cleared `0x140df1d03`, used to address sends in `0x140df2910`);
`+0xe3` "session-ended-for-me" mark (set for all members by `0x140df5930`, cleared by type-0 events); `+0xe4` (CORRECTED, see section 6.2): not a pure pending-removal flag; on party vector B it means 'first type-0x93 data received'; on session vector A it is read by the Update sweep and written by `0x140df6680`. (C for access)

**Flow state (+0x10)**: written by Update (`0x140df3c08`=2, `0x140df3d15`=3, `0x140df3d09`=4, `0x140df3d90`=6, `0x140df3df5`=5), `0x140df5500` (=4), `0x140df5930` (=1),
`0x140df2910` (=8, "end session", `+0x1a8`=arg), `0x140df6350` (=0). Jump table `0x140df3fd0`: state 1 `0x140df3789` (members ready? **30 s** timer +0x180, then `0x140df5840(1)`),
2 `0x140df3c6d` (waits sub-object `+0x1b8`, **30 s** timer +0x190, then 4 or 3), 3 `0x140df3d2d` (wait, -> 6), 4 `0x140df3d9c` (wait, -> 5), 5/6/8 nothing, 7 `0x140df3e01` (86400 s clamp timer), default resets timers. (C)
Live: 0->1->4->5 at Roundtable party, 6 at expedition start, 7 later, 0 at leave (C, recording).
**Byte table `0x142ca1f00[flow]` = {0,0,0,0,0,0,**1**,0,**1**,0,0,0,6}**: states **6 and 8 are "session active"** (C). It gates removal and teardown (section 2, 3).
Meaning (CORRECTED, section 6.1): 1-4 joining/setup, 5 Roundtable party, 6 = short window (5.7-8.4 s live) at expedition start, **7 = steady expedition state**, 8 never seen.
In-expedition = flow 7 (CORRECTED; table[7]=0); disconnected = phase 0 or 7, flow 0.

## 2. Host side: peer loss to character removal (item 1)

Chain (all C unless noted):
1. Platform layer detects loss. Callbacks registered: P2PSessionRequest 1202 / **P2PSessionConnectFail 1203** (singleton ctor `0x1427c2f10`, fan-out handler `0x1427c3060` to up to 4 listeners), and Steam lobby callbacks 505 (LobbyDataUpdate) / **506 (LobbyChatUpdate)** (`0x142588a8e..0x142588b92`), i.e. the game uses legacy ISteamNetworking P2P plus Steam lobbies (imports show only `SteamInternal_*` flat interface). The code that turns these into session events is in the obfuscated layer: **U**.
2. Session events are polled in `Update` (`0x140df2ec3`, platform vslot +0x68) by type: 1 = member data / state (`0x140df5ae0`, `0x140df71a0`), 2 = member joined (`0x140df6680`... see below), 3 = `0x140df6890`, 4 = session error (stores `+0x6b8`, calls `ResetSession` then dispatch `0x140df4580`). Exact type-to-name mapping S: handler at `0x140df2f5b` (calls `0x140df6680` then `0x140df71a0`) is the **member-left** path, since `0x140df6680` finds the record by SteamID, writes the reason to `+0xd8` and removes it.
3. **`CSSessionManagerImp::OnMemberLeft(this, steamid*, reason)` = `0x140df6680`**. Computes `immediate = !(*[0x143c17eb0]->[+0x10] bit0 flag via 0x140bc7f90) && table[flow]==0`.
   - (CORRECTED, section 6.1: only in the short flow-6 window, NOT steady flow 7) flow 6/8: NOT removed. Only `record+0xe4 = 1` (pending removal). The record, its PlayerGameData slot and the PlayerIns stay. (C)
   - otherwise erased at once (swap with last, `-0xf0` on `+0x80`) and `RemoveMemberFinalize` called.
   Then always: `0x140dd50a0([+0x30], steamid)` (sub-manager forget), `0x140df8380([+0x1a0])`, and `0x140f272a0(ctx, 2, steamid)` (EOS anti-cheat peer unregister; same object as the EOS session hooked by nrsc hook #2 at `0x140f271d0`).
4. The deferred ones are swept in `Update` at `0x140df3070..0x140df3165`: for each record with `+0xe4`, if `!0x140bc7f90() && !table[flow]` -> erase + `RemoveMemberFinalize`. (CORRECTED, section 6.1) A dropped player keeps its record only in the flow-6 window (or while `0x140bc7f90()` is true); in steady flow 7 it is erased immediately.
5. **`RemoveMemberFinalize(this, record*)` = `0x140df5020`** (C):
   - notify listener `0x14073fd30` (via `0x14061e1d0` of `[+0x48]` of a global at `0x143c1xxxx`),
   - `[global+0x68]` `0x140b67130(steamid, rec+0xe0)`,
   - PlayerGameData slot `0x1401fe530(rec+0xd0)` (GameDataMan `0x143c078d0`, 0x8A0 stride): optional effect `0x14073f680`,
   - **PlayerIns removal**: `0x140519750(WorldChrMan, &steamid)` scans ChrSet `WorldChrMan+0x9a40` for the player with that SteamID, then `0x140aedd00` / `0x14066f870` / vslot `+0x558` on it, and `0x14051b2b0(WorldChrMan, chr)` = **WorldChrManImp::RemoveChr** (erases from `+0x17580` vector, clears `+0x174e8`/`+0x174f0` if equal, searches `+0x9b48/+0x9a40/+0x9a98/+0x9af0` ChrSets and sets `chr+0x1f3 |= 0x20` = deferred delete),
   - `0x1401fdd80(rec+0xd0)` resets the PlayerGameData slot (S),
   - **if members <= 1** and `!0x1406ae7b0()` and `+0x1f` and `0x1406aea70()` and not busy (`0x14073fa40`, `0x140b66fe0`): `LeaveSession(this,0)` (`0x140df1180`),
   - `[global 0x143c...+0x64] = 1`, `0x140b9feb0(rec+0xd4)` (UI party slot), `0x140df2730` (refresh), **`this+0x14 = 10.0f`**.
6. `Update` also ticks `+0x14` down (`0x140df33ed..0x140df343c`) while phase == 3, member count <= 1 and not busy; at <= 0 it calls `LeaveSession(this,0)`. Observed live (guest) 4.9 s from member count 2->1 to phase 3->7, not 10 s (**U**, some other condition or an earlier smaller value).

**Timeouts (item 1)**. None found for "no data from peer"; the game-side values are:
| Value | Where | Grade |
|---|---|---|
| **30000 ms** x4 at create-params `+0xc,+0x10,+0x14,+0x18` (+ `+0x26c,+0x270`) | defaults written by ctor `0x140deebf0` (`0x140deec39..0x140deec4e`, `0x140deed0f/19`); the params struct is filled by `0x140defbd0` (CreateSession) -> `0x140df4830` and passed to the platform vslot +0x08. Same constant in join params: `0x140dee5c7`, `0x140de9959` (`0x140de96c0`), `0x14257e495` (`+0xa4`, library ctor `0x14257e430`) | C that the value is there; **U** which field is "peer timeout" (consumer in obfuscated layer) |
| **30.0 s** setup timers `+0x180` / `+0x190` | set to `0x41f00000` in `0x140df5930` (`0x140df59f5`, `0x140df5a2b`); state 2 re-arms `+0x190` to 30.0 (`0x1432f36ec`); expiry -> `0x140df5840(this,1)` -> LeaveSession | C |
| **10.0 s** post-removal timer `+0x14` | `0x140df5466` | C |
| 35.0 s pending-request age limit | `0x142c11a7c` read at `0x140df36df` | C |
| Steam's own P2P timeout (legacy API, not configurable here) | Steam client | S |
To extend: patch the immediate 0x7530 in `0x140deebf0` (4+2 sites) and the ctor-local ones; if the effective peer timeout is Steam's P2P, it cannot be changed from the game. (W)

## 3. Dropped-client side: what ends the run (item 2)

Live (guest, host dropped, `...025836`, C): 03:04:43.93 member vector 2->1 (host record erased immediately: flow was 7, not 6/8);
44.03 nrsc `M+0` 0x21->0, lobby ids zeroed; 44.5 members 1->0; **48.84 `SessionMan+0xc` 3->7, `+0x2c`=6, `PGD.local+0x98` 13->0** (LeaveSession ran);
**51.54 `+0x8` 2->0, `+0xc` 7->0, `+0x10` 7->0, `+0x2c` 6->0** (ResetSession); 03:05:01.15 (+9.6 s) **local PlayerGameData rewritten**
(name, `+0xc8`, `+0xe5`, runes `+0x110` 1664->0, `+0x187...`, many fields: this is the return-to-menu load, not ResetSession).
So the game-level teardown is **not** instant (7.6 s from member loss to idle, 17 s to PGD rewrite) and is driven by member loss, not by a socket timeout: the trigger is the Steam P2P/lobby event for the host leaving.

Code path (C):
1. Host leaves: event as in section 2 on the guest: host record erased (flow not 6/8) or flagged (flow 6/8) and `RemoveMemberFinalize` -> member count <= 1.
2. `LeaveSession(this, bool)` = **`0x140df1180`** (callers ~30 sites incl. menu jobs `0x140b1a790` JoinSessionWaitJob, `0x140dcd...` job functions, `0x140df5840`, `0x140df5020`, Update). No-op if global phase in {0,2,5} (mask 0x25) or leave already pending (`+0x2c`) ; if phase 1/4 only sets `+0x1d`; else **phase = 7**, clears timed list, posts session event `(flag+0xc8)<<32` via `0x140df71a0`, calls platform leave (vslot +0x20) storing the handle in `+0x2c`; if the platform returns 0 immediately -> `0x140df61e0`.
3. Platform leave completes (`+0x2c` poll in Update) -> **`ResetSession(this, bool)` = `0x140df61e0`**: `0x140f27270(ctx,2)` (EOS end), frees `+0x730/+0x738`, `phase=0`, platform destroy session (vslot +0x38), `+8=0`, listener reset (`+0x700` vslot +0x28), `+0x18=0`, **`0x140203200(first_pending, +0x6b8 error)`** stores the end reason into `[0x143c046a8]->+0x148` (+0x5c/+0x68/+0x70; only caller), `[global+0xb]=1`, tail `0x140df6350`.
4. **`0x140df6350` = session-end cleanup**: if `table[flow]` (6/8) or `0x140bc7f90()` or `+0x1c` -> only `+0x1a = 1` (deferred, re-run from Update `0x140df32ed` once flow leaves 6/8); else `flow = 0`, clears party (`0x140df6fa0`, `0x140df76b0`, `0x140df2730`, `0x140dd5050`, `0x1404d34f0([+0x168])`), `WorldChrMan+0x174e8` (local PlayerIns): `0x14066f2d0(chr,1)`, `0x14066f870` (reset multiplayer team/state), `0x140b67120`, `[..+0x64]=1`, `0x140dfa0a0(..,0)`.
   **Observed flow 7->0 at ResetSession** while table says 7 is not "active": consistent.
5. The expedition-failed / return-to-Roundtable UI+step flow is driven from the game steps, not from here: classes `CS::CSEventNetworkErrorReturnTitleStep` (vtbl `0x142c2cff0`, ctor `0x140615790`, one creator `0x1406144b0` <- `0x1405f5540`), `CS::CSEventNetworkErrorLeaveOnlineModeStep` (vtbl `0x142c2c8e8`, ctor `0x140614b70`), `CS::CSNetworkFlowStep` (vtbl `0x142ca1968`, `0x140de6090/6270/65a0`), `CS::InGameStep` (vtbl `0x142c865b8`). The ReturnTitle step body `0x140615e80` sets `[0x143c...+0x4a8]=1`, calls `0x140868a20`, `0x1406af5d0`, `0x14060aba0(1)`. Which step is entered for "host lost during expedition" and what decides it: **U** (not reached).
   The run result / PlayerGameData is not transmitted anywhere from this path; local state is wiped later by the title/menu load (recording above).

### What lives where (PlayerGameData vs elsewhere) (item 2)
- PlayerGameData (GameDataMan `0x143c078d0`, `[+8]` local, `[+0x10]` + idx*0x8A0 remote slots; offsets from recording `...030701`, W names): `+0x68` level, `+0x6c` runes held, `+0x70` runes earned, `+0x10/+0x14/+0x18` HP/max/base, `+0x1c/+0x20` FP, `+0x3c..+0x50` attributes, `+0x270` weapon slot, `+0x98/+0xe4` expedition/map state (13->0 at LeaveSession, i.e. **cleared by the leave path at 48.84**), `+0xc8` sessions joined count, `+0xe8` chosen character id, `+0x187` etc. (C for values).
- Remote slots: reset by `0x1401fdd80(idx)` (RemoveMemberFinalize), so a remote player's slot is wiped on removal.
- Local PlayerGameData rewrite: **only at the later menu/title load** (+9.6 s), not at LeaveSession/ResetSession (C, recording). Inventory/relics: not located (U); level/runes/HP/attrs are in PGD (above).
- Elsewhere: member/party state in CSSessionManagerImp, PlayerIns (WorldChrMan), team type, event flags, map/world state (host-owned).

## 4. Hook points

| # | Address | Use | Effect |
|---|---|---|---|
| H1 | `0x140df1180` LeaveSession (single choke point) | return early when the cause is host/peer loss | keeps phase 3 and the session; platform side may still be dead |
| H2 | `0x140df5020` RemoveMemberFinalize / `0x14051b2b0` RemoveChr | skip for a chosen SteamID (host side: keep PlayerIns + slot) | note `0x1401fdd80` wipes the slot; the deferred path (`+0xe4`) already exists for flow 6/8 |
| H3 | `0x140df6680` OnMemberLeft | force deferred (`+0xe4=1`) / drop the event | host keeps record; on rejoin clear `+0xe4` and set `+0xe2` |
| H4 | `0x140df61e0` ResetSession and `0x140df6350` | block / defer (`+0x1a`) | stops phase->0 and local PlayerIns reset |
| H5 | `0x140deebf0` ctor | patch 0x7530 values | longer connect/keepalive timeouts (effect U) |
| H6 | Update `0x140df33ed` block | remove 10 s auto-leave | |
| H7 | `0x1427c3060` P2PSessionConnectFail fan-out | swallow failures for a peer | |

## 5. Feasibility read (SUPERSEDED, see Reconciliation below)

- **Host keeps a dropped player's slot:** plausible. The game already defers member removal in flow 6/8 (`+0xe4`), so H3 (or no hook at all while flow stays 6) keeps the record, PGD slot and PlayerIns. Unverified on a host (no recording). Rejoin would need the guest to re-enter the lobby and a handshake that clears `+0xe4`; the existing join flow (`0x140df6890` member-joined, `0x140df5ae0`) probably sets a *new* record, so duplicate/stale record handling is needed: S/U.
- **Dropped client stays in-world:** hard. LeaveSession/ResetSession are easy to block, but the client then has no transport: its P2P session and lobby are dead, the host removed its record and PlayerIns on its side (unless H3), and the map/world state is host-owned and in motion. A holding state needs the client to reconnect to the same Steam lobby and re-run the join with a state resync; the game's join assumes a fresh Roundtable member (`0x140df5930` marks all members `+0xe3`, joining uses the lobby flow). PlayerGameData is not wiped until the later menu load, so the *data* survives for ~10 s, but nothing should be relied on.
- Timeouts: no game-level inactivity timeout found; the 30 s values are session setup/create parameters. Removal is event-driven by Steam (P2P fail 1203 / lobby 506). So "extending a timeout" is likely **not the lever**; suppressing the event (H3/H7) is.
- Overall: host-side retention **medium**, client-side seamless rejoin **low** without runtime tracing of the obfuscated platform layer and of a host with a dropping guest. Needed next: record a host whose guest drops, and trace flow state at drop (6 vs 7).


## 6. Reconciliation with live data (2026-10-16 recordings; supersedes the corrections marked above)

### 6.1 Byte table `0x142ca1f00` is indexed correctly; steady expedition (flow 7) is NOT covered (C)
Base and index re-read: `lea rcx,[rip+0x1eabb83]` at `0x140df6376` gives `0x142ca1f00`, index `movsxd rax,[this+0x10]`, `cmp byte [rax+rcx],0`
(`0x140df637d`); Update/OnMemberLeft use `[rax+r14+0x2ca1f00]` with r14 = image base (`0x140df3070 - 0xdf3070`). Raw bytes `00 00 00 00 00 00 01 00 01 00 00 00 06`:
index 6 = 1, **index 7 = 0**, index 8 = 1. No off-by-one, and I found no second field indexing it. Live flow is 5->6 at expedition start and 6->7 after
7.2 s / 8.4 s (guest) and 5.7 s (host), then 7 until leave; 8 never. So my earlier "expedition = flow 6, removal deferred" was wrong: the
deferral and the `0x140df6350` teardown deferral apply only inside the short flow-6 window. In flow 7 `OnMemberLeft` erases at once
**unless `0x140bc7f90()` is true** (object `[[0x143c17eb0]+0x10]`, byte `+0x178` bit 0; W: an "offline/network-mode" bit; hook #3 targets a call to it). That bit was not recorded: read it live to know
whether removal is deferred in flow 7 (probe `[[0x143c17eb0]+0x10]+0x178`).

### 6.2 Two member vectors; the recorded `Members` is vector B (C for addresses, W for relation)
Session manager vector **A** = `[0x143c21ec8]+0x78/+0x80` (read by Update, OnMemberLeft, RemoveMemberFinalize). The recorder watched
vector **B** = `[0x143c21fc8]+0x30/+0x38` (party/member owner; read by `0x140df5500`, Update `0x140df2cc6`, `0x140df9830`). Same 0xF0 stride, different containers
(B is rebuilt from A by `0x140df2730` / `0x140df76b0`, W). The live `+0xe2/+0xe3/+0xe4` are B's:
- **B `+0xe4`**: set at `0x140df9a20` after the 0x38-byte type-0x93 message is applied to the member's PlayerGameData slot (setters `0x14020d6a0..0x14020d730`):
  means "first sync data received". That is why it is 1 on the host from join time and stays 1 (matches live, `...030701` member 1 `+0xe4` 0->1 at 03:11:35.2). It is not pending removal.
- A `+0xe4` is still the one read by the Update sweep (`0x140df3079`) and written by `0x140df6680`; whether A and B share the semantic is U (A not recorded).
- B `+0xe3` 0->1->0 at host phase 1->3: consistent with the mark `0x140df5930` sets on all A records and Update clears (`0x140df38fc`); S.
- B `+0xe2` 1->0 on member 0 when the guest joined: `0x140df1d03` clears it, `0x140df5eb4` sets it; S. `+0xd8`, `+0xe1` unchanged: `+0xd8` is only written by OnMemberLeft (no leave in the host log), `+0xe1` (local) is constant. Consistent.

### 6.3 What drives `+0x0c` 6->3 sixteen seconds before the member loss (C code, W trigger)
Phase 6 = guest of a remote owner, phase 3 = session owner: host = 0->1->3, guest = 0->4->6 (live). The writer is
**`0x140df6890` = `OnOwnerChanged(this, steamid*)`**, called from Update's event type 3 (`0x140df2f51`; also from the ready scan `0x140df2b7e/0x140df2d8e`).
Logic (C): scan A; the record whose SteamID equals the event id gets `+0xe0 = 1`, others 0; copy it to the owner struct `+0x90` and set `this+0x160 = rec.+0xe2`.
If the cached owner (`this+0xa0`) is non-null and differs (owner changed): if **`this+0x1e == 0` -> `LeaveSession(this,0)`**; else if the new owner is the local record (`+0xe1` and `+0xe0`):
**phase 6 -> 3** (`0x140df6979..0x140df6980`), set bytes `+0x14/+0x15` of a global, `0x140dccfa0`/`0x1406aeaf0` checks; if the local record was the owner and no longer is: phase 3 -> 6 (`0x140df6bb7`);
otherwise notify sub-managers (`0x140dd4660`, `0x140dd1060`, `0x140dcd0d0`, `0x140b67160`).
`this+0x1e` is the **host-migration-allowed flag**: set in `0x140df4830` from bit 4 of a settings dword (`[settings+0x24] >> 4 & 1`) and forced to 0 when `+0x745` is set. In the recording it was non-zero (no leave at 03:04:27.7), so **the guest was promoted to owner 16 s before the member vector shrank**.
So the earliest disconnect signal is the platform session's owner-change event (type 3 -> `0x140df6890` -> phase 3), the 16 s lead means the session layer (obfuscated, U) detects host loss and migrates, and the member record is only erased 16 s later (event type 2 -> `0x140df6680`, probably the P2P-fail or lobby-leave, W). Not Steam lobby owner change per se (that would also have shrunk the lobby); exact detector U.
Best hook points for an early signal: **`0x140df6890`** (owner change, the 6->3 write at `0x140df6980`) and the type-3 dispatch at `0x140df2f36`.

### 6.4 Other corrections
- Section 3 "flow 7->0 consistent" stays: flow 7 is the in-expedition state, and ResetSession/teardown ran from flow 7 (table 0) so it was not deferred, which is why the guest tore down 7.6 s after the count drop.
- The recorded sequence on the dropped guest, now complete: 27.7 phase 6->3 (migration), 43.9 B 2->1, 44.0 nrsc M cleared, 44.5 B 1->0, 48.8 phase 3->7 (LeaveSession), 51.5 7->0 (ResetSession), 61.1 PGD rewritten (menu load).
  The host-side drop (second recording, `...030701` t=03:10:33) shows phase 6->7 then 7->0 within 1.2 s for a *deliberate* leave by the guest-side machine: a leave is fast, a lost peer is slow (16 s + 7.6 s).
- The 10 s `+0x14` timer vs the observed 4.9 s: unresolved (U).

### 6.5 Corrected feasibility: "host keeps the dropped guest's slot during the expedition"
- **Not free any more.** In steady flow 7 the host erases the record and PlayerIns at the member-left event (unless the `0x140bc7f90` bit is set, unmeasured). Nothing in the retail game defers it.
- **Feasible with a small hook (medium-high):** hook/patch `OnMemberLeft` (`0x140df6680`) to take the deferred branch for the chosen SteamID (set `+0xe4`, skip erase) or just skip the call; PlayerIns is only removed from `RemoveMemberFinalize` (`0x140df5020` -> `0x14051b2b0`), and the slot wipe `0x1401fdd80` is inside it, so skipping the erase keeps record, PGD slot and PlayerIns. Needed in addition:
  suppress the Update sweep for that record (`0x140df3079`), avoid `LeaveSession` via the member-count<=1 rules (`0x140df5020`, Update `0x140df33ed`) when it is a 2-player run, keep the PlayerIns from timing out in WorldChrMan (U: untested), and stop the Steam side from dropping the P2P/lobby relationship (U).
- The 16 s lead is useful: the host would see the same owner-event pattern only if the *host* drops. For a dropped **guest** the host has no recording; its earliest signal is expected to be the member-left event (type 2) itself, or a P2P fail (1203), not a 16 s earlier phase change. U.
- Rejoin of that guest then needs: the lobby re-entry (nrsc already does it), a join that reuses the kept record (clear `+0xe4`, set `+0xe2`, update SteamID/slot index) instead of creating a new one via `0x140df5ae0`, and the type-0x93 PGD resync. Estimated medium; needs a host-side recording of a guest drop to confirm event timing and the `0x140bc7f90` bit.
- Client-side seamless rejoin: unchanged, low.
