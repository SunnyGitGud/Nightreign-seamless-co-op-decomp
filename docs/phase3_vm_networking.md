# Phase 3: VM-protected networking functions (0x180061140, 0x180064120)

Grades: **C** confirmed (observed in Unicorn with controlled inputs, or by asm), **S** strongly inferred, **W** weak, **U** unknown.
Method: trace-based black-box on the Themida VM, Unicorn only, nothing native, no game, no network. The VM was not lifted; each claim is
an input -> output observation on the boundary (reads/writes of crafted objects, exits to readable `.text`, vtable calls). The
branch signature used for novelty is the hash of the executed basic-block sequence inside `.themida`, which is a clean path signature
(identical for inputs that take the same branches). Harness: `phase3/tools/` (copies of devirt/tools with small additions, see below),
outputs in `phase3/out/`. `devirt/` was only read (v2, v3, v4 and docs/devirt.md 2.9).

Tools (phase3/tools): `boundary2.py` (copy; added exit handlers with stack args, API handlers, shared image cache), `wp3.py` (copy),
`w61140.py` (spec-driven world for 0x180061140: object graph, peer/queue/lock/`ISteamNetworkingMessages` mocks, autoplant on null faults),
`par.py` (parallel runs), `fuzz61140.py` (coverage-guided fuzz), `sweep*.py`, `t_*.py`, `probe*.py` (the experiments below),
`w64120.py`, `t64.py` (0x180064120 scenarios). Experiment logs: `phase3/out/p61140_experiments.txt`, `phase3/out/p64120_experiments.txt`,
`phase3/out/fuzz61140_corpus_b.json` (2500-run fuzz, 1384 distinct VM paths, mostly fault artefacts; it found the three exit targets
and the `0x1800650c0` queue pop, then the targeted runs below were used).

Run ids below are `script:section` of those logs, e.g. `sweep3` = the timer grid.

---------------------------------------------------------------------------------------------------------------------------------

## 1. 0x180064120: `on LobbyDataUpdate(lobby, member, success)` (class = the `LobbyDataUpdate_t` callback listener)

### 1.1 Identification

The vtable area `.rdata 0x180147710..0x1801478f0` is 14 four-slot vtables, one derived/base pair per Steam callback listener
(`CCallbackBase`-like object of 16 bytes: vptr, flag byte at +8 (bit0 = registered), callback id at +0xc). Slot layout (all C, asm):

| slot | contents |
|---|---|
| 0 | `0x18004aa00`: `mov rax,[rcx]; mov rax,[rax+8]; jmp rax` = `Run(void*,bool,SteamAPICall_t)` forwards to slot 1 |
| 1 | `Run(void* param)`, the handler |
| 2 | `GetCallbackSizeBytes` (`mov eax,size`) |
| 3 | deleting dtor (restores the base vtable, calls the IAT unregister slot `0x1801612f8` if bit0 of +8, optional `operator delete`) |

The group at `0x1801478b0` is `{aa00, 0x180064120, aa70 (size 0x18), 0x180064380}`: size 24 = `LobbyDataUpdate_t {u64 lobby; u64 member; u8 success}`.
devirt 2.9 confirms by registration: `SteamAPI_RegisterCallback(obj, 0x1f9)` = `k_iLobbyDataUpdate` (C). The sibling at `0x180147890`
(`0x1800643c0`, size 0x10) is `GameLobbyJoinRequested_t` (id 0x14d). The "vtable 0x180147898" of the task statement is slot 1 of the
`0x180147890` group's neighbour; `0x1801478b8` is the slot of this function. So the class is the **LobbyDataUpdate callback wrapper**; it has
no data of its own beyond the 16-byte `CCallbackBase`; its back pointer is the core object.

### 1.2 State it works on

The listeners sit in the core object (S, computed twice: lock at `L0-0x50 = core+0x78` and poison at `L0-4 = core+0xc4` match the readable code; devirt 2.9
says core+0xc8..+0x138). `[this - 0xd8]` (this = core+0x138) is `core+0x60`, a pointer to **the master state object** `M` (the object
the hooks call `*ctx[0xe0]`, NetState's `state_blob`; `M[0]` is the u32 "mode word" 0x21 live). Fields of `M` seen by the four lobby/join handlers
(C asm unless noted):

| offset | size | meaning | by |
|---|---|---|---|
| 0x00 | u64 | state flags. bit0 lobby entered, bit2 lobby created (readable handlers), bit3 = the bit sent in the control message (0x18003f2c0 reads bit3 of a state byte), **bit4 (0x10) = "join target pending lobby data"**, bit5 set in live mode word 0x21 (U) | 643c0, 64120 |
| 0x120 / 0x128 | u64 | lobby id / lobby owner id | readable lobby code |
| 0x130 | u64 | **join target lobby id** (`GameLobbyJoinRequested_t.lobby`) | 643c0 writes, 64120 reads/clears |
| 0x138 | u64 | **join target friend id** | 643c0 |
| 0x140 | u64 | **join failure code** written by 64120 | 64120 |
| 0x158..0x15a | u8 | lobby-create pending, leaderboard add / refresh pending | readable |
| 0x418 | ptr | `ISteamMatchmaking*` (vtbl+0x98 = `GetLobbyData`) | 64120 |

### 1.3 Semantics (all C from `phase3/out/p64120_experiments.txt`; harness `w64120.py`, `t64.py`)

```c
// vtable 0x1801478b0 slot 1; this = &core->listener[7] (core+0x138); [this-0xd8] == core->state
void OnLobbyDataUpdate(Listener *this, LobbyDataUpdate_t *p) {
    State *M = *(State **)((char *)this - 0xd8);
    if (p->lobby != M->join_lobby)            // +0x130; p->member is NOT read
        return;                                // runs: key mismatch, no write
    if (!(M->flags & 0x10))                    // not waiting for data
        return;                                // (flag 0x10 clear: nothing happens, tested with 0, 0x1, ..., 0xff)
    M->flags &= ~0x10;                         // 8-byte read-modify-write, other bits kept
    if ((uint8_t)p->success == 0) {            // byte at p+0x10 (a count of 256 reads as 0)
        M->join_lobby = 0;  M->join_friend = 0;
        return;                                // +0x140 untouched
    }
    const char *t = mm->GetLobbyData(p->lobby, "nrsc_lobby_type");         // vtbl+0x98
    if (memcmp(t, "nrsc_ynx3_seamless_master_lobby", 0x20) != 0) {         // 31 chars + NUL
        M->join_lobby = 0;  M->join_friend = 0;
        M->join_error = 0xFFFFFFFFFFFFE0C1;    // "not a seamless lobby"
        return;
    }
    const char *v = mm->GetLobbyData(p->lobby, "nrsc_lobby_version");
    if (memcmp(v, "1.1.3", 6) != 0) {                                      // 6 bytes incl. NUL
        M->join_lobby = 0;  M->join_friend = 0;
        M->join_error = 0xFFFFFFFFFFFFE0C2;    // "version mismatch"
        return;
    }
    // match: nothing else. join_lobby/join_friend stay, flag 0x10 is now clear.
}
```

Evidence (run ids, `p64120_experiments.txt`): "flag 0x10 clear", "lobby mismatch", "success=0", "memcmp 0 0 / 1 0 / 0 1" (the three outcomes, strings
captured at the exits: `nrsc_ynx3_seamless_master_lobby\0` 32 B, `nrsc_y`/`1.1.3\0` 6 B; the exit is `0x180103010` = memcmp), error word invariant
under lobby id, member id, old `+0x140` value, and flag word. Writes per outcome: match = `M[0]` only; bad data = `M[0],+0x130,+0x138`; type/version mismatch adds `+0x140`.

* **C**: the control flow, the two GetLobbyData keys and compared lengths, the cleared fields, the constants 0xFFFFFFFFFFFFE0C1/C2 (written as 8 bytes).
* **S**: this is the second half of the "join from friend invite" flow: `0x1800643c0` (GameLobbyJoinRequested) stores {lobby, friend} to +0x130/+0x138 and sets bit 0x10; someone (U, probably the per-frame/join code) requests lobby data; this handler validates it. The readable `LobbyEnter` handler does not check `nrsc_lobby_version`, **this one does** (answers server_manager.md 2.1 "U whether protected code compares the version").
* **W**: the two error words are the message selectors for `MODMESSAGE_YUINEXUSNOSEAMLESS` / `WRONGSSR` (category 15000, ids 0 and 1, devirt 2.9); the on-wire/UI mapping of 0xE0C1/0xE0C2 is not seen. They look like `-0x1f40 + code` (code 1, 2).
* **U**: what consumes `M->flags` bit 4 clear with a retained `join_lobby` (the actual `JoinLobby`); the code that calls `RequestLobbyData`.

---------------------------------------------------------------------------------------------------------------------------------

## 2. 0x180061140: the per-frame server_manager task

### 2.1 Object graph (C for the dereference order, S for names)

`run(this, args)`: `this+0x30` = `A` (null -> return immediately, C `this:0x30=0`); `C = A[0]` ("manager"); `ctx = C[0]` (same `ctx` the readable
`0x180063110` uses: channel `ctx+4`, `ctx+0x18` = core, `ctx+0x20` -> state byte, `ctx+0x28` -> peer vector object). `dt = *(float*)(args+8)` (C).

| Object | offset | meaning |
|---|---|---|
| C | +0x00 | ctx (server) |
| C | +0x08 / +0x20 / +0x28 | pointers to game-side objects (only null/non-null and, for +0x28, the chain below, are read) |
| C | +0x70 | `std::map<int,std::function>` (used by task 0x180061060, not by this task) |
| C | **+0x80** | `X`: timers/counters block (below) |
| C | **+0x88** | `Y`: a `std::vector<Peer*>` object (begin at +0, end at +8). Elements are `Peer*` (flags byte at +0x128, id at +8, ping dword at +0x18) |
| C | +0x90 | `Z`: queue object (below) |
| ctx | +0x28 | `Q`: the peer vector (begin +0, end +8), same `Peer*` element type (flags +0x128, id +8, field +0x3c) |
| X | +0x140 | u64, read in the member pass, no effect found (U) |
| X | +0x148 / +0x14c | timer 1: accumulator (s) / rate (Hz) |
| X | +0x150 / +0x154 | timer 2: accumulator / rate |
| X | +0x170 / +0x174 | per-frame counters (below) |
| X | +0x190 | party member count |
| game | `[[C+0x28]+0x28]` | object `V` with `vector<0xF0-byte>` begin at +0x30, end at +0x38 (matches hooks.md 9 "member vector 2 x 0xF0") |

`Y` and `Q` are different pointers in the harness; in the real object they may be the same vector (W: both hold `Peer*` with the same field offsets; the
startup run only sees a 5-element pool).

### 2.2 Semantics, in execution order (C unless marked)

```c
void server_manager_tick(Task *this, TickArgs *a) {
    if (!this->owner) return;
    Mgr *C = this->owner[0];  Ctx *ctx = C->ctx;  Timers *X = C->timers;  float dt = a->dt;

    // (1) timer 1, 'handshake resend' (X+0x148/0x14c). sweep3: reset iff acc >= 1.0f / rate; rate < 0 always fires, rate == 0 never.
    X->acc1 += dt;                                   // the 1.0f is the constant at 0x180145d70
    if (X->acc1 >= 1.0f / X->rate1) {                // this is the old "2/3": acc 1.5 s at rate 0.6667 Hz
        X->acc1 = 0;
        for (Peer *y : *C->Y)                        // t_Y3 'T1': 32 of 64 odd values fire
            if ((y->flags & 0x11) == 0x01)           // active, not yet handshaken
                send_control(ctx, y);                // exit 0x18003f2c0(ctx, y): msg id 3 ch 1, 1 byte = bit3 of *ctx->state
    }

    // (2) control receive for every peer of ctx (always, no timer)
    for (Peer *e : ctx->peers)                       // begin/end read once from ctx+0x28
        if (e->flags & 1) {                          // sweepE: bit0 only
            uint8_t b;
            while (recv(ctx->peers.begin, ctx->peers.end, e->id, /*channel*/1, &b, /*max*/1) != 0) {   // exit 0x180040370
                if ((e->flags & 0x11) == 0x01) {     // first message while active and not handshaken
                    e->flags |= 0x10;                //   'handshaken'
                    if (b != 0) e->flags |= 0x40;    //   remote reports bit3 set
                    else        e->field3c = 1;      //   (+0x3c written only for payload 0; payload 1/2/ff never write it)
                    send_control(ctx, e);            //   reply, exactly once per peer
                }                                    // later messages are only drained (state 0x11/0x51: no writes)
            }
        }

    // (3) timer 2, 'ping poll' (X+0x150/0x154), same rule as (1)
    X->acc2 += dt;
    if (X->acc2 >= 1.0f / X->rate2) {
        X->acc2 = 0;
        for (Peer *y : *C->Y)
            if ((y->flags & 0x11) == 0x11) {         // active and handshaken (t_Y3 'T2')
                lock(core);                          // exits 0x18008f88c / 0x18008f894 around it
                // ISteamNetworkingMessages vtbl+0x28 = GetSessionConnectionInfo(&identity{type 0x10, id y->id}, NULL, &qs)
                y->ping = qs.m_nPing;                // Peer+0x18, int32 (t_T2: independent of return value, state, quality)
                unlock(core);
            }
    }

    // (4) counters over Y (every frame)
    X->n_a = X->n_b = 0;
    for (Peer *y : *C->Y) if (y->flags & 1) { if (y->flags & 0x40) X->n_b++; else X->n_a++; }   // sweepY2: counts, 8 peers -> 8
                                                     // n_a = X+0x170 (active, remote bit3 clear), n_b = X+0x174 (remote bit3 set)

    // (5) party size
    X->members = (V->end - V->begin) / 0xF0;         // X+0x190; V = [[C+0x28]+0x28]; empty or null chain -> 0 (sweepT1; non multiples give magic-division junk)

    // (6) cross-thread job queue
    if (C->Z) {                                      // Z+0x20 (tail idx) != Z+0x28 (head idx)  (probeZ)
        Item *it; if (try_dequeue(Z, &it)) ...;      // exit 0x1800650c0 = moodycamel ConcurrentQueue::try_dequeue(Z, out) (C asm)
    }                                                // one pop per frame in the runs (second item never popped). What is done with the item: U
                                                     // (a tree at [Z+8] with +0x18 nil byte / +0x20 key is walked: looks like a std::map lookup, W)
    // (7) game state mirror
    //   [[C+0x20]+0x10] = u32 s:  s==0 -> g_0x180183018 = 0;  s==1||s==2 -> g_0x180183018 = 6;  other -> g unchanged   (probeT34)
    //   [[C+8]+0x20] is only null-tested (twice); C+0x90 is tested before (6).
}
```

Grades: (1)-(5) **C** (each rule has a sweep with a clean result; run ids `sweep3`, `sweep2`, `t_Y3`, `sweepE`, `t_E2`, `t_T2`, `sweepY`, `sweepY2`, `sweepT1`);
roles "handshake", "ping poll", "remote bit3" are **S/W** (names mine). (6) exit and trigger **C**, purpose **W**. (7) **C** for the mapping, **U** for meaning
(the global `0x180183018` is read only by VM code; 6 could be a mode enumerator).

Peer flag bits as used here (C, additive to server_manager.md): `0x01` active, `0x10` handshake complete (first control message received), `0x20` lobby host
(unchanged here), `0x40` remote's state bit3 was set at handshake. The old doc statement "flag == 1 -> 0x11, field3c = 1, send" was an artefact of the
earlier harness returning a non-zero object from `0x180040370`: the true condition is "a 1-byte message arrived" and `field3c` is set only for payload 0.

### 2.3 Where the send helper is (and is not) called

* `0x18003f2c0` (control): from (1) for each `Y` peer with `(flags&0x11)==1` when timer 1 fires, and from (2) once per peer after its first message. **C**.
* `0x180040370` (pop): from (2), arguments `(ctx.peers.begin, ctx.peers.end, e->id, 1, stackbuf, 1)`; returns the byte count; loop until 0. **C**.
* `0x18003f030` (data send): **never reached** in any run: fuzz (2500 runs, all fields in the read set, 1384 paths), nor in the targeted worlds above. By elimination
  it is not in the timer/peer/count/member/queue branches explored; the remaining candidates are the item handler of (6) and the branch using `X+0x140` / `V` elements
  (unexplored). **U** (S that it is not on the control path: control uses `0x18003f2c0`).

### 2.4 Remaining unknowns for this function (U)

* What the dequeued item of (6) is and how it is consumed (needs a model of the `[Z+8]` tree and the item type).
* `X+0x140`, the second read of `X` after the member count; the contents of `V` elements (never read in these runs).
* Initial values of the rates `X+0x14c`, `X+0x154`; set by the constructor (VM) which no run executed with real data.
* Meaning of the global `0x180183018` and its consumers; `C+8`, `C+0x20`, `C+0x28` owners.
* Whether `Y` equals the ctx peer vector.

---------------------------------------------------------------------------------------------------------------------------------

## 3. Proposal: `src/nrsc/session/` (new; not written here)

Target: a session layer that uses `src/nrsc/server/` (PeerList, MessageBus, Leaderboard, Lobby), attaches to `NetState` via `attach(bus, peers)`
and calls `nrsc::hooks::set_party_session_active(bool)`. All names are proposals.

```
src/nrsc/session/
  session_state.hpp   struct SessionState   // = NetState::state_blob view: u64 flags (+0), join_lobby (+0x130), join_friend (+0x138), join_error (+0x140)
                      enum StateFlag : u64 { kInLobby=0x1, kLobbyCreated=0x4, kBit3=0x8, kJoinPending=0x10, kParty=0x20 };
                      constexpr u64 kJoinErrorNoSeamless = 0xFFFFFFFFFFFFE0C1, kJoinErrorWrongVersion = 0xFFFFFFFFFFFFE0C2;
  join_flow.hpp/.cpp  class JoinFlow {
                        JoinFlow(SessionState&, server::ISteamMatchmaking&);
                        void on_game_lobby_join_requested(u64 lobby, u64 friend_id);      // 0x1800643c0: join_lobby=lobby, join_friend=friend, flags|=kJoinPending
                        enum class DataResult { Ignored, Accepted, BadData, NoSeamless, WrongVersion };
                        DataResult on_lobby_data_update(u64 lobby, u64 member, bool success); // 0x180064120 exactly as 1.3 (member unused)
                      };                                  // needs ISteamMatchmaking::GetLobbyData in steam_iface.hpp; compares kLobbyType (32 B with NUL) and kVersion (6 B)
  frame_task.hpp/.cpp class FrameTask {                    // 0x180061140
                        struct Timers { float acc1, rate1, acc2, rate2; };            // defaults U (pick rate1 = rate2 = 1.0 until known)
                        FrameTask(SessionState&, server::PeerList& ctx_peers, std::vector<server::Peer*>& y /*may alias ctx_peers*/,
                                  server::MessageBus&, hooks::net::NetState&, ISteamNetMessagesInfo&);
                        void tick(float dt);                                          // phases (1)-(5) of 2.2
                        int active_without_bit3() const; int active_with_bit3() const; int member_count() const;   // X+0x170/0x174/0x190
                        std::function<void()> on_job;                                 // phase (6), U
                      };
                      // phase (2) uses NetState::pop_received(peer, 1, &b, 1); handshake -> peer.flags |= 0x10 (|0x40 or field3c = 1), bus.send_control(peer)
                      // phase (1) uses MessageBus::send_control(peer); phase (3) needs ISteamNetworkingMessages::GetSessionConnectionInfo (vtbl+0x28):
                      //   add `bool GetSessionConnectionInfo(SteamId, int &ping)` to the injected interface, store ping into a new Peer::ping_ms (Peer+0x18, int32)
  party.hpp/.cpp      class PartyTracker { void update(); }  // calls hooks::set_party_session_active(state.flags & kInLobby && any peer handshaken) (W)
```

Behaviour tests to write: (a) timer rule `acc>=1/rate` incl. rate 0 / negative; (b) peer state machine over the table in 2.2 (flags 0x01 + payload 0/1 -> 0x11 with field3c=1 /
0x51, reply sent once, later messages drained); (c) counters; (d) `on_lobby_data_update` table in 1.3 (five outcomes, member ignored, flag 0x10 cleared first); (e) member count
as `(end-begin)/0xF0`. The `kParty` condition for `set_party_session_active` is **W**: live mode word 0x21 = bits 0 and 5 of `M[0]`; bit0 is the lobby-entered flag (C), bit5's setter is not in
the VM code studied here (U).

`Peer` additions needed in `server/peer.hpp`: `ping_ms` (+0x18), handshake bit `kPeerHandshaken = 0x10` (already `kPeerMarked`, rename), `kPeerRemoteBit3 = 0x40`.
`task_timer.cpp` should be corrected: the reset rule is `acc >= 1.0f / rate`, not `acc >= 1 && rate >= 2/3` (the old pair was one point of the real curve, `sweep3`), and the marking
condition is "a message arrived and `(flags&0x11)==1`", not "flags == 1".

---------------------------------------------------------------------------------------------------------------------------------

## 4. Summary

Recovered: the class and full semantics of `0x180064120` (LobbyDataUpdate listener, including the previously unknown `nrsc_lobby_version` check and the failure words);
the identity of the sibling `0x1800643c0`; the layout of the eight listeners and their back pointer; for `0x180061140` the two rate-limiters (`acc >= 1/rate`), the handshake
state machine over peer flags (0x01/0x10/0x40, `field3c`), the periodic control resend, the per-peer 1-byte receive loop, the ping poll through
`GetSessionConnectionInfo`, the counters `X+0x170/0x174`, the member count `X+0x190 = bytes/0xF0`, the queue pop, the `0x180183018` mapping.
Not recovered: any caller of `0x18003f030`, the job queue item handling, `X+0x140`, default rates, and who requests the lobby data and does the actual join.
