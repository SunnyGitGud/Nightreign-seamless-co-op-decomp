#pragma once
// Net group (docs/plan_gaps.md phase 2, WP10; docs/hooks.md section 9): hooks.json #8, #9, #12-#16, #18, #19 and the
// protected-installer hooks P1, P2, P4, P9 (0x1800221e0, rows 1, 2, 4, 9).
//
// Grades: C confirmed from bytes, S strongly inferred, W weak, U unknown (evidence/hooks/detours/group1..4.md).
//
// SESSION LAYER AND FALLBACK. Networking is phase 3: the lobby/handler logic is still in protected code (VM functions
// 0x180061140, 0x180064120). The original reaches the server_manager core through `ctx[0x30]` and the Hooks object's
// sub-structs, which protected code fills. Here all of that is `NetState`, reached through net_state(). It holds an
// optional pointer to a server::MessageBus and server::PeerList ("session layer"). While none is attached
// (`attached() == false`, the default) EVERY detour here calls the game's original (or, where the original is a pass-through
// anyway, behaves like it), so the game runs as if nrsc's networking did not exist. Detours that never call the original
// in nrsc.dll (#12, #14, #15, #16, P2, P4, P9) only take their replacement path with a session attached AND the U
// members they need set; any missing U member falls back to the original as well.
//
// Detours are free functions with the game's own argument lists; the original's invoker/_Do_call thunk that prepends
// `ctx` is replaced by the relay of nrsc::hooks::Installer (same convention as hooks/params).
//
// Kind deviation: devirt/hooks.json list #12 and P2 as E9 patches at the function entry (no trampoline, orig None). A
// session-less fallback to the game's original needs a way to reach it, so these two are Kind::Inline (MinHook
// trampoline). Behaviour with a session attached is identical.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "nrsc/config/settings.hpp"
#include "nrsc/hooks/hook.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::hooks::net {

// Signature-table slots of the hooked sites (docs/signatures.md; hooks.json tgt.table_off / protected table sigtable_off).
inline constexpr unsigned kSlotSetClassA = 0x138;       // #8, inline mode0 off 0: the E8 at 0x1407fb19b (S)
inline constexpr unsigned kSlotSetClassB = 0x140;       // #9, mode0 off 6: the E8 at 0x14094afb4 (S)
inline constexpr unsigned kSlotBossListCopy = 0x158;    // #12, mode1: entry of 0x1409e5b10 (W)
inline constexpr unsigned kSlotRateSnoop = 0x160;       // #13, mode0 off 15: the E8 at 0x1401fd6bb (W)
inline constexpr unsigned kSlotGameFlagGetter = 0x168;  // #14, mode0 off 7: the E8 at 0x140698469 (C)
inline constexpr unsigned kSlotDecreaseJob = 0x170;     // #15, mode0 off 12: the E8 at 0x1409ee060 (S)
inline constexpr unsigned kSlotUpdateJob = 0x178;       // #16, mode0 off 14: the E8 at 0x140b5361e (S)
inline constexpr unsigned kSlotDebugSession = 0x238;    // #18, RTTI: vtable of PlayerDebugSession (slot +0x8) (C)
inline constexpr unsigned kSlotNetworkSession = 0x240;  // #19, RTTI: vtable of PlayerNetworkSession (slot +0x18) (C)
inline constexpr unsigned kSlotTeamCollect = 0x288;     // P1, mode0 off 12: the E8 at 0x140df8a7e (C)
inline constexpr unsigned kSlotTeamSend = 0x290;        // P2, mode1: entry of 0x140dfe4f0 (C)
inline constexpr unsigned kSlotMapItemRequest = 0x2a0;  // P4, mode1: entry of 0x140581b20 (S)
inline constexpr unsigned kSlotSessionBroadcast = 0x2c8;  // P9, mode0 off 12: the E8 at 0x14057fb35 (C)

// Slots read by wire() (docs/wiring_net.md). The Hooks-object field that held each value in the original is in brackets.
inline constexpr unsigned kSlotGameDataGlobal = 0x1b0;     // address of the 0x143c078d0 global (C) [ctx[0x78]+0x00]
inline constexpr unsigned kSlotAllocatorGetterA = 0x28;    // 0x14080b1d0: `mov rax,[0x143c2da78]; ret` (C) [ctx[0x98]+0x18]
inline constexpr unsigned kSlotAllocatorGetterB = 0x30;    // 0x141fc3480: `mov rax,[global]; ret` (C) [ctx[0x98]+0x20]
inline constexpr unsigned kSlotWrapCallback = 0xf0;        // 0x14053e2b0 (S) [ctx[0x98]+0x48]
inline constexpr unsigned kSlotAttachCallback = 0xf8;      // 0x14080e3f0 (S) [ctx[0x98]+0x50]
inline constexpr unsigned kSlotDecreaseJobVtbl = 0x218;    // RTTI CS::CSChaosMatchRateDecreaseJob (C) [ctx[0x98]+0x28]
inline constexpr unsigned kSlotUpdateJobVtbl = 0x220;      // RTTI CS::CSChaosMatchRateUpdateJob (C) [ctx[0x98]+0x30]
inline constexpr unsigned kSlotTeamSendFn = 0x98;          // 0x140dfe250, call target (C) [ctx[0x60]+0x30]
inline constexpr unsigned kSlotPlayerSendFn = 0xa0;        // 0x140df2270, call target (C) [ctx[0x68]+0x20]

inline constexpr std::uint32_t kVtableSlotDebugSend = 0x8;      // PlayerDebugSession slot 1 -> 0x140e0ba70 (C)
inline constexpr std::uint32_t kVtableSlotNetworkRecv = 0x18;   // PlayerNetworkSession slot 3 -> 0x140e0bce0 (C)

// P2P channels used by the detours (server::MessageBus::send "channel" argument = the byte in the 3-byte header).
inline constexpr std::uint8_t kChannelClassId = 7;        // #8/#9 (C, nrsc 0x1800293a2: r9d = 7)
inline constexpr std::uint8_t kChannelSessionType1 = 5;   // #18/#19, message type 1 (C)
inline constexpr std::uint8_t kChannelSessionType18 = 6;  // #18/#19, message type 0x18 (C)
inline constexpr std::uint8_t kChannelMapItem = 8;        // P4 (C, nrsc 0x180026415: DWORD [rsp+0x20] = 8)
inline constexpr std::uint32_t kSessionTypeA = 0x01;      // meaning U
inline constexpr std::uint32_t kSessionTypeB = 0x18;      // meaning U
inline constexpr std::size_t kReceiveBufferSize = 0x400;  // #19 (C, nrsc 0x18002ac0c)
inline constexpr std::uint32_t kTeamPayloadType = 0x94;   // P2 per-peer send type (C, group1.md section 7)
inline constexpr std::size_t kMapItemMessageSize = 0x28;  // P4 (C)
// P4 sends only to peers whose flags have both of these bits (`(~flags & 0x21) == 0`, C nrsc 0x180026408). Bit 0 is
// server::kPeerActive; bit 5 is server::kPeerHost in the server module's naming, so P4 in effect sends the request to the
// lobby owner. The reading "ready peers" of docs/hooks.md is W; the mask itself is C.
inline constexpr std::uint32_t kPeerReadyMask = 0x21;

// Game structure layouts (documented offsets only).
namespace layout {
// GameDataMan-like global 0x143c078d0 (W name): +8 PlayerGameData*, +0xd0/+0xd8 flag bytes, +0x159/+0x15a job-done bytes.
inline constexpr std::size_t kGdmPlayerGameData = 0x08;  // #8 (C, nrsc 0x180029335; game 0x1407fb19b rdx=[rcx+8])
inline constexpr std::size_t kGdmFlagD0 = 0xd0;          // #14 (C, nrsc 0x180029990..0x1800299b8)
inline constexpr std::size_t kGdmUpdateDone = 0x159;     // #16 callback (C, nrsc 0x18001eca6)
inline constexpr std::size_t kGdmDecreaseDone = 0x15a;   // #15 callback (C, nrsc 0x18001ebe0)
inline constexpr std::size_t kGdmRateGateByte = 0x00;    // #16 callback tests `[[S+8]] & 0x40` (C, nrsc 0x18001ec12)
inline constexpr std::uint8_t kGdmRateGateBit = 0x40;
// Rate-job entry (hook 13 argument 3): +4 "active" byte (C, nrsc 0x180029923).
inline constexpr std::size_t kEntryActive = 0x04;
// Player/debug session objects (#18/#19): +8 = remote peer SteamID (S, nrsc 0x18002abc3 `mov r12,[r14+8]`).
inline constexpr std::size_t kSessionPeerId = 0x08;
// #19 packet descriptors: In {u8* buf +0, u64 size +8}; Out {u8* buf +0, u64 size +8, u32 len +0x10} (C, nrsc 0x18002aca7).
inline constexpr std::size_t kPacketBuf = 0x00, kPacketSize = 0x08, kPacketOutLen = 0x10;
// Team session (P1/P2): peer record vector +0x30/+0x38, records of 0xF0 bytes: +0xe1 blocked, +0xe3 already sent (C, group1.md s7).
inline constexpr std::size_t kTeamPeersBegin = 0x30, kTeamPeersEnd = 0x38, kTeamRecordSize = 0xf0;
inline constexpr std::size_t kTeamRecBlocked = 0xe1, kTeamRecSent = 0xe3;
// CSSessionManager (P9): member vector +0x78/+0x80 of 0xF0 records, member id (i32) at +0xd4 (C, nrsc 0x180026726/0x180026770).
inline constexpr std::size_t kSessMembersBegin = 0x78, kSessMembersEnd = 0x80, kSessRecordSize = 0xf0, kSessRecId = 0xd4;
// Byte-vector blob of #12 {alloc +0, begin +8, end +0x10, cap +0x18, u32 +0x20, u32 +0x24} = 0x28 bytes (C, group3.md).
inline constexpr std::size_t kBlobSize = 0x28;
// std::vector-like triple {begin, end, cap} of the game (#16 arguments, P2 payload).
inline constexpr std::size_t kVecBegin = 0x00, kVecEnd = 0x08, kVecCap = 0x10;
// #15 job (0x78 bytes) and #16 job (0xe0 bytes), align 8 (C, nrsc 0x1800299e2 and 0x180029b4d).
inline constexpr std::size_t kDecreaseJobSize = 0x78, kUpdateJobSize = 0xe0, kJobAlign = 8;
inline constexpr std::size_t kJobVtbl = 0x00, kJobRefCount = 0x08;
inline constexpr std::size_t kDecreaseJobX10 = 0x10, kDecreaseJobX60 = 0x60, kDecreaseJobX70 = 0x70;
inline constexpr std::size_t kUpdateJobX10 = 0x10, kUpdateJobSelfPtr = 0x68, kUpdateJobA = 0x78, kUpdateJobB = 0x80;
inline constexpr std::size_t kUpdateJobVec64 = 0x88, kUpdateJobVec32 = 0xa0, kUpdateJobVec8 = 0xc0;
inline constexpr std::size_t kUpdateJobX = 0xb8, kUpdateJobY = 0xbc, kUpdateJobFlag = 0xd8;
}  // namespace layout

// Game allocator object: vtable slot 0x50 allocates (size, align) (C, nrsc 0x1800299f0 `call [r9+0x50]`).
struct GameAllocator;
struct GameAllocatorVtbl {
    void* slots_before[10];
    void* (*allocate)(GameAllocator* self, std::size_t size, std::size_t align);  // +0x50
};
struct GameAllocator {
    const GameAllocatorVtbl* vtbl;
};
static_assert(offsetof(GameAllocatorVtbl, allocate) == 0x50);

// Byte-vector blob copied by #12 (the game's 0x1409e5b10 copy constructor, DLVector<u8>-like, W).
struct ByteBlob {
    GameAllocator* alloc;
    std::uint8_t* begin;
    std::uint8_t* end;
    std::uint8_t* cap;
    std::uint32_t u20;
    std::uint32_t u24;
};
static_assert(sizeof(ByteBlob) == layout::kBlobSize);

// std::vector-like triple as the game passes it.
struct GameVec {
    void* begin;
    void* end;
    void* cap;
};

// Callable shape handed to the game's wrap/attach helpers (#15/#16). nrsc's closures are MSVC std::function impls
// (`_Func_impl_no_alloc`, vtable slot 2 = _Do_call); the real layout is the game's/MSVC's and is W. 0x50 bytes in the
// original (#15 allocates 0x48: only the first 0x48 bytes are ever read, C).
struct LocalJobCallback;
struct CallbackVtbl {
    void* slot0;
    void* slot1;
    void (*do_call)(LocalJobCallback* self);  // slot 2 (C, closure vtables 0x180147170 / 0x1801471a0)
};
struct LocalJobCallback {
    const CallbackVtbl* vtbl;  // +0x00 (nrsc 0x180147170 for #15, 0x1801471a0 for #16)
    void* job;                 // +0x08 the job object
    std::uint8_t zero[0x28];   // +0x10..+0x38 zero (C)
    LocalJobCallback* self;    // +0x38 (C)
    void* arg0;                // +0x40 first state pointer: #15 game_data (ctx98+0x10), #16 rate state (ctx98+0x08)
    void* arg1;                // +0x48 #16 only: second 8 bytes copied from ctx98+0x10
};
static_assert(sizeof(LocalJobCallback) == 0x50 && offsetof(LocalJobCallback, arg0) == 0x40);

// The nrsc "shared state" S of #13/#16 (ctx[0x30]->[0x70]; S): +8 game_data, +0x50 current rate, +0x54 generated delta,
// +0x58 selector byte. Written by #13, read and written by the #16 callback (C, nrsc 0x18001ec00).
struct ChaosRateState {
    std::uint8_t pad0[8];
    void* game_data;             // +0x08 GameDataMan-like object: byte0 & 0x40 gates the callback, +0x159 = update done
    std::uint8_t pad10[0x40];
    std::uint32_t current_rate;  // +0x50 (U: who writes it; default 0)
    std::int32_t delta;          // +0x54 generated rate delta
    std::uint8_t selector;       // +0x58 snooped by #13 ("entry active" byte), bit 0 picks the rate range
};
static_assert(offsetof(ChaosRateState, game_data) == 0x08 && offsetof(ChaosRateState, current_rate) == 0x50 &&
              offsetof(ChaosRateState, delta) == 0x54 && offsetof(ChaosRateState, selector) == 0x58);

// ctx[0x98] sub-struct of #15/#16 (S): game helpers used to build the local jobs. Filled by protected code (U): every
// pointer defaults to null, and the detours then call the original instead.
struct ChaosJobHost {
    void* (*alloc_object)(std::size_t size, std::size_t align) = nullptr;  // +0x18 -> allocator, vt[0x50] (job objects)
    void* (*alloc_data)(std::size_t size) = nullptr;                       // +0x20 -> allocator, vt[1] (vector data) (#16)
    const void* decrease_vtbl = nullptr;                                   // +0x28 vtable stored in the #15 job (RTTI CS::CSChaosMatchRateDecreaseJob 0x142c717d8)
    const void* update_vtbl = nullptr;                                     // +0x30 vtable stored in the #16 job (CS::CSChaosMatchRateUpdateJob 0x142c71858)
    void* game_data = nullptr;                                             // +0x10 the object the #15 callback flags (+0x15a)
    // +0x48 `wrap(&scratch, callback)` and +0x50 `attach(job, wrapped)`: game helpers, signatures S (nrsc 0x180029a73/a7c).
    void* (*wrap_callback)(void** scratch, void* callback) = nullptr;
    void (*attach_callback)(void* job, void* wrapped) = nullptr;
    // +0x58/+0x60 (#15) and +0x68/+0x70 (#16): the live callbacks. A new job replaces (destroys) the previous ones (C).
    std::unique_ptr<LocalJobCallback> decrease_cb[2];
    std::unique_ptr<LocalJobCallback> update_cb[2];

    bool decrease_ready() const { return alloc_object && wrap_callback && attach_callback && decrease_vtbl; }
    bool update_ready() const { return alloc_object && alloc_data && wrap_callback && attach_callback && update_vtbl; }
};

// Boss selection list of #12 (ctx[0xe8]: byte vector at +0x18..+0x20, u32 at +0x48). Filled by protected code (U).
struct BossListSource {
    std::vector<std::uint8_t> bytes;
    std::uint32_t forced_u20 = 0;
};

struct NetState {
    // ---- session layer (phase 3) ----
    server::MessageBus* bus = nullptr;
    server::PeerList* peers = nullptr;
    bool attached() const noexcept { return bus != nullptr && peers != nullptr; }
    void attach(server::MessageBus& b, server::PeerList& p) noexcept { bus = &b; peers = &p; }
    void detach() noexcept { bus = nullptr; peers = nullptr; }

    // Per-peer receive queues, `peer + 0x96770 + channel * 0x268` in the original, popped by FUN_180040370 (C). The
    // fill side is a message handler in protected code (U); phase 3 calls enqueue_received() from its handlers.
    void enqueue_received(server::SteamId peer, std::uint8_t channel, std::span<const std::byte> payload);
    // FUN_180040370: finds the ACTIVE peer with this SteamID, pops one queued message of the channel, copies
    // min(size, capacity) bytes and returns that count; 0 when there is no such peer or no message (C).
    std::size_t pop_received(server::SteamId peer, std::uint8_t channel, void* buffer, std::size_t capacity);
    std::size_t queued(server::SteamId peer, std::uint8_t channel) const;

    // ---- originals (call-site targets, MinHook trampolines, old vtable slots), written by the Installer ----
    void* original_set_class = nullptr;     // #8 and #9: 0x140202aa0 (gdm, pgd, id)               (ctx[0x78]+0x18)
    void* original_boss_list = nullptr;     // #12: 0x1409e5b10 (out, src)                          (none in nrsc)
    void* original_rate_snoop = nullptr;    // #13: 0x1406c2900 (7 args)                            (ctx[0x78]+0x28)
    void* original_game_flag = nullptr;     // #14: 0x1401fe1c0 ()                                  (ctx[0x78]+0x30)
    void* original_decrease_job = nullptr;  // #15: 0x140af9cd0 (mgr, out)                          (none in nrsc)
    void* original_update_job = nullptr;    // #16: 0x140af9e10 (10 args)                           (none in nrsc)
    void* original_debug_send = nullptr;    // #18: 0x140e0ba70 (this, data, len, type)             (ctx[0x68]+0x28)
    void* original_network_recv = nullptr;  // #19: 0x140e0bce0 (this, out, in, type)               (ctx[0x68]+0x30)
    void* original_team_collect = nullptr;  // P1: 0x140dfe070 (team, vec)                          (ctx[0x60]+0x48)
    void* original_team_send = nullptr;     // P2: 0x140dfe4f0 (team, first, last, payload)         (none in nrsc)
    void* original_map_item_request = nullptr;  // P4: 0x140581b20 (7 args)                         (ctx[0x90]+0x50)
    void* original_session_broadcast = nullptr;  // P9: 0x140df24d0 (mgr, type, data, len)          (ctx[0x90]+0x60)

    // ---- #12 boss list (W, nrsc 0x180029860) ----
    // ctx[0xe8]. U: nullopt, and the detour then calls the original (the game's own boss-id list).
    std::optional<BossListSource> boss_list;

    // ---- #13/#16 rate state (S) ----
    ChaosRateState rate{};
    // `rand()` replacement for the #16 callback (the original seeds with rdtsc then calls the CRT rand, C nrsc 0x18001ec1b).
    // Default: rdtsc-like seed (steady clock ticks) then std::rand().
    std::function<int()> random;

    // ---- #14 game flag (C, nrsc 0x180029990) ----
    // &ctx[0xe0]->+0x195, the nrsc "force true" byte. U: null = clear.
    const std::uint8_t* force_flag = nullptr;
    // Address of the pointer to the singleton behind ctx[0x78] (the GameDataMan-like global 0x143c078d0, S: chain shape
    // `***ctx[0x78]`). U: null = no singleton, the +0xd0 byte reads as 0.
    void* const* game_data_slot = nullptr;

    // ---- #15/#16 local jobs (S) ----
    ChaosJobHost jobs;

    // ---- P2 team send (C) ----
    // ctx[0x60]+0x30: `send(team, record, type, data, len)`, same shape as the game's 0x140dfe250 (S; possibly that very
    // function, W). U: null, and P2 then calls the original.
    void (*team_send)(void* team, void* record, std::uint32_t type, const void* data, std::uint32_t len) = nullptr;

    // ---- P4/P9 (C/S) ----
    // `*(u32*)*ctx[0x90]->+0x10` mode word (also read by params' P3 and #10): `(W & 5) == 1` selects the forwarding mode
    // in P4. U: null = mode word 0, the forwarding path is never taken.
    const std::uint32_t* mode_word = nullptr;
    // ctx[0x90]+0x28 / +0x2c: unicast target ids (i32). -1 = unset. P9 reads them; the writers are in protected code (U);
    // P4 clears +0x28 after the original (C nrsc 0x180026492).
    std::atomic<std::int32_t> unicast_id_a{-1};
    std::atomic<std::int32_t> unicast_id_b{-1};
    // ctx[0x90]+0x30: id of the request being processed while P4 runs the original (C nrsc 0x18002646a).
    std::atomic<std::int64_t> current_request{-1};
    // ctx[0x90]+0x20: lock object, held by P4 around the original (vtbl[0] acquire / vtbl[1] release, C).
    std::mutex lock;
    // ctx[0x68]+0x20: per-player send `(mgr, entry, type, data, len)` = the game's 0x140df2270 (S). U: null, and P9 then
    // calls the original.
    bool (*player_send)(void* mgr, void* entry, std::uint32_t type, void* data, std::uint32_t len) = nullptr;

    // ---- nrsc's own shared state object (the original's `*ctx[0xe0]`, a heap object of the mod) ----
    // Offsets used by the net group: +0x00 u32 mode word (live value 0x21), +0x159/+0x15a job-done bytes, +0x195 force
    // flag. mode_word, force_flag, jobs.game_data and rate.game_data point into this block after wire(). The block is
    // zero (vanilla). Phase 3 sets the mode word (0x21 was seen in every live phase). Never moved after construction.
    static constexpr std::size_t kStateBlobSize = 0x200;
    alignas(8) std::array<std::uint8_t, kStateBlobSize> state_blob{};
    static constexpr std::size_t kStateModeWord = 0x00, kStateForceFlag = 0x195;

    NetState();

private:
    std::map<std::pair<server::SteamId, std::uint8_t>, std::deque<std::vector<std::byte>>> received_;
};

// Singleton owned by this module. groups::net() (re)creates it.
NetState& net_state();
NetState& reset_net_state();

// ---- detours (game argument lists) ----
// #8/#9 (S) 0x140202aa0 (gdm, pgd, id): original first; if pgd is the local PlayerGameData, the 4-byte id goes to every
// active peer on channel 7. (nrsc 0x1800292f0)
void detour_set_class(void* gdm, void* pgd, std::uint32_t id);
// #12 (W) 0x1409e5b10 (out, src): returns nrsc's boss byte list instead of the game's. (nrsc 0x180029860)
ByteBlob* detour_boss_list(ByteBlob* out, ByteBlob* src);
// #13 (W) 0x1406c2900: stores entry+4 in the rate selector, then the original. (nrsc 0x180029920)
void detour_rate_snoop(void* a0, void* a1, void* entry, bool f, std::uint32_t g, std::uint8_t h, std::uint32_t i);
// #14 (C/W) 0x1401fe1c0 (): getter of byte +0xd8. (nrsc 0x180029990)
bool detour_game_flag();
// #15 (S) 0x140af9cd0 (mgr, out): local ChaosMatchRateDecrease job. (nrsc 0x1800299c0)
void** detour_decrease_job(void* mgr, void** out);
// #16 (S) 0x140af9e10: local ChaosMatchRateUpdate job. (nrsc 0x180029b20)
void** detour_update_job(void* mgr, void** out, std::uint64_t a, std::uint64_t b, const GameVec* v64, const GameVec* v32,
                         std::uint32_t x, std::uint32_t y, const GameVec* v8, std::uint8_t flag);
// #18 (S) PlayerDebugSession slot 0x8 (this, data, len, type). (nrsc 0x18002aaa0)
bool detour_debug_send(void* self, void* data, std::uint32_t len, std::uint32_t type);
// #19 (S) PlayerNetworkSession slot 0x18 (this, out, in, type). (nrsc 0x18002abb0)
void* detour_network_recv(void* self, void* out, void* in, std::uint32_t type);
// P1 (C) 0x140dfe070 (team, vec): pass-through. (nrsc 0x180026230)
bool detour_team_collect(void* team, void* vec);
// P2 (C/S) 0x140dfe4f0 (team, first, last, payload). (nrsc 0x180026250)
void detour_team_send(void* team, std::uint8_t first, std::uint8_t last, const GameVec* payload);
// P4 (S/W) 0x140581b20 (a0, a1, a2, a3, a4, a5, a6). (nrsc 0x180026370)
std::uint32_t detour_map_item_request(void* a0, std::uint64_t a1, std::uint64_t a2, const void* a3, void* a4,
                                      std::uint8_t a5, std::uint64_t a6);
// P9 (S) 0x140df24d0 (mgr, type, data, len) at the type 0x2c call. (nrsc 0x180026700)
bool detour_session_broadcast(void* mgr, std::uint32_t type, void* data, std::uint32_t len);

// The #16 callback body (nrsc 0x18001ec00) and the #15 one (0x18001ebe0), exposed for tests.
void rate_update_callback(LocalJobCallback* cb);
void decrease_done_callback(LocalJobCallback* cb);

// Fills NetState's non-session members from the signature table (docs/wiring_net.md). Call after groups::net() (which
// resets the state) and before the hooks go live. Every member whose source slot is 0 is left vanilla (null).
void wire(const signatures::SignatureTable& table, const config::Settings& settings);
// Same with an injectable slot reader (tests). `slot(off)` returns the table value for a table offset.
void wire_from_slots(const std::function<std::uint64_t(unsigned)>& slot, NetState& state);

// Computes the HookSpec list from a slot reader (table.at in the DLL). Separate from groups::net so that tests can
// provide addresses without a scanned image.
std::vector<HookSpec> build_specs(const std::function<std::uint64_t(unsigned)>& slot, NetState& state);

}  // namespace nrsc::hooks::net
