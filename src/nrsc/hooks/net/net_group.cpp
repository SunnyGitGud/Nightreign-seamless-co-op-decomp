#include "nrsc/hooks/net/net_group.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "nrsc/core/raw_memory.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::hooks::net {

namespace {

using raw::load;
using raw::store;
template <typename Fn>
Fn fn(void* p) {
    return reinterpret_cast<Fn>(p);
}
std::span<const std::byte> as_bytes_of(const void* p, std::size_t n) {
    return {static_cast<const std::byte*>(p), n};
}

int default_random() {
    // The original seeds the CRT generator with rdtsc, then calls rand() (C, nrsc 0x18001ec1b..0x18001ec28).
    std::srand(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return std::rand();
}

std::unique_ptr<NetState>& state_slot() {
    static std::unique_ptr<NetState> s;
    return s;
}

}  // namespace

NetState::NetState() : random(default_random) {}

NetState& net_state() {
    auto& s = state_slot();
    if (!s) s = std::make_unique<NetState>();
    return *s;
}
NetState& reset_net_state() {
    state_slot() = std::make_unique<NetState>();
    return *state_slot();
}

void NetState::enqueue_received(server::SteamId peer, std::uint8_t channel, std::span<const std::byte> payload) {
    received_[{peer, channel}].emplace_back(payload.begin(), payload.end());
}

std::size_t NetState::queued(server::SteamId peer, std::uint8_t channel) const {
    const auto it = received_.find({peer, channel});
    return it == received_.end() ? 0 : it->second.size();
}

// FUN_180040370 (C): first non-null peer with the active bit and a matching SteamID (peer+8); pops one message of the
// channel; copies min(size, capacity); returns the copied size.
std::size_t NetState::pop_received(server::SteamId peer, std::uint8_t channel, void* buffer, std::size_t capacity) {
    if (!peers || !peers->find_active(peer)) return 0;
    const auto it = received_.find({peer, channel});
    if (it == received_.end() || it->second.empty()) return 0;
    const std::vector<std::byte> msg = std::move(it->second.front());
    it->second.pop_front();
    const std::size_t n = std::min(msg.size(), capacity);
    if (n) std::memcpy(buffer, msg.data(), n);
    return n;
}

// ---- #8 / #9 (S, nrsc 0x1800292f0..0x1800293a2; one detour for both sites) ----
// Original first (C 0x180029323). Then, if pgd is the local PlayerGameData (gdm+8, C 0x180029335), send the 4-byte id to
// every active peer on channel 7 (C 0x1800293a2). Without a session the original is all that runs.
void detour_set_class(void* gdm, void* pgd, std::uint32_t id) {
    NetState& s = net_state();
    if (auto original = fn<void (*)(void*, void*, std::uint32_t)>(s.original_set_class)) original(gdm, pgd, id);
    if (!s.attached() || !gdm || !pgd) return;
    if (load<void*>(gdm, layout::kGdmPlayerGameData) != pgd) return;
    for (server::Peer& peer : s.peers->slots())
        if (peer.active()) s.bus->send(peer.steam_id, as_bytes_of(&id, sizeof id), kChannelClassId);
}

// ---- #12 (W, nrsc 0x180029860..0x180029915) ----
ByteBlob* detour_boss_list(ByteBlob* out, ByteBlob* src) {
    NetState& s = net_state();
    const bool can_replace = s.attached() && s.boss_list && src && src->alloc && src->alloc->vtbl && src->alloc->vtbl->allocate;
    if (!can_replace) {
        auto original = fn<ByteBlob* (*)(ByteBlob*, ByteBlob*)>(s.original_boss_list);
        return original ? original(out, src) : out;
    }
    const BossListSource& list = *s.boss_list;
    out->alloc = src->alloc;
    out->begin = out->end = out->cap = nullptr;
    if (!list.bytes.empty()) {  // allocator vt[0x50](size, 1) then a byte copy (C)
        out->begin = static_cast<std::uint8_t*>(src->alloc->vtbl->allocate(src->alloc, list.bytes.size(), 1));
        if (out->begin) {
            std::memcpy(out->begin, list.bytes.data(), list.bytes.size());
            out->end = out->cap = out->begin + list.bytes.size();
        }
    }
    if (src->u20 != list.forced_u20) src->u20 = list.forced_u20;  // the source is overwritten too (C)
    out->u20 = src->u20;
    out->u24 = src->u24;
    return out;
}

// ---- #13 (W, nrsc 0x180029920..0x180029984) ----
void detour_rate_snoop(void* a0, void* a1, void* entry, bool f, std::uint32_t g, std::uint8_t h, std::uint32_t i) {
    NetState& s = net_state();
    if (entry) {  // `xchg [S+0x58], e->+4` (C)
        std::atomic_ref<std::uint8_t>(s.rate.selector).exchange(load<std::uint8_t>(entry, layout::kEntryActive));
    }
    auto original = fn<void (*)(void*, void*, void*, bool, std::uint32_t, std::uint8_t, std::uint32_t)>(s.original_rate_snoop);
    if (original) original(a0, a1, entry, f, g, h, i);  // arguments unchanged, no return value (C)
}

// ---- #14 (C for the code, W for the purpose; nrsc 0x180029990..0x1800299b8) ----
// The hooks.json summary is inverted: true when the nrsc byte is NON-zero (C). The original is never called with a
// session attached; without one it is (the getter of byte +0xd8 of the 0x143c078d0 global).
bool detour_game_flag() {
    NetState& s = net_state();
    if (!s.attached()) {
        auto original = fn<bool (*)()>(s.original_game_flag);
        return original ? original() : false;
    }
    if (s.force_flag && *s.force_flag != 0) return true;
    void* const obj = s.game_data_slot ? *s.game_data_slot : nullptr;
    return obj ? load<std::uint8_t>(obj, layout::kGdmFlagD0) != 0 : false;
}

// ---- callbacks (C) ----
const CallbackVtbl kDecreaseCallbackVtbl{nullptr, nullptr, &decrease_done_callback};  // nrsc vtbl 0x180147170, slot 2 0x18001ebe0
const CallbackVtbl kUpdateCallbackVtbl{nullptr, nullptr, &rate_update_callback};      // nrsc vtbl 0x1801471a0, slot 2 0x18001ec00

// 0x18001ebe0 (C): `xchg byte [[cb+0x40]+0x15a], 1`.
void decrease_done_callback(LocalJobCallback* cb) {
    if (!cb || !cb->arg0) return;
    std::atomic_ref<std::uint8_t>(*(static_cast<std::uint8_t*>(cb->arg0) + layout::kGdmDecreaseDone)).store(1);
}

// 0x18001ec00 (C). Fabricates the "server result" of the rate update: when bit 0x40 of the game object's first byte is
// set, picks a delta from the rate range selected by `selector` bit 0 and raises the update-done byte (+0x159).
void rate_update_callback(LocalJobCallback* cb) {
    if (!cb || !cb->arg0) return;
    auto* st = static_cast<ChaosRateState*>(cb->arg0);
    auto* obj = static_cast<std::uint8_t*>(st->game_data);
    if (!obj || !(obj[layout::kGdmRateGateByte] & layout::kGdmRateGateBit)) return;
    NetState& s = net_state();
    const std::int32_t r = s.random ? s.random() : 0;
    if (st->selector & 1) {
        st->delta = r % 300 + 200;  // 200..499
    } else {
        const std::int32_t m = r % 150;  // 0..149
        // `if (cur <= m + 100) delta = -cur else delta = -100 - m` (unsigned compare, C nrsc 0x18001ec5e)
        st->delta = st->current_rate <= static_cast<std::uint32_t>(m + 100) ? -static_cast<std::int32_t>(st->current_rate) : -100 - m;
    }
    std::atomic_ref<std::uint8_t>(obj[layout::kGdmUpdateDone]).store(1);
}

namespace {

std::unique_ptr<LocalJobCallback> make_callback(const CallbackVtbl* vtbl, void* job, void* arg0, void* arg1) {
    auto cb = std::make_unique<LocalJobCallback>();
    std::memset(cb.get(), 0, sizeof *cb);  // +0x10..+0x38 zero (C)
    cb->vtbl = vtbl;
    cb->job = job;
    cb->self = cb.get();
    cb->arg0 = arg0;
    cb->arg1 = arg1;
    return cb;
}

// `attach(job, wrap(&scratch, callback))` (C nrsc 0x180029a6b..0x180029a7c)
void register_callback(ChaosJobHost& h, void* job, LocalJobCallback* cb) {
    void* scratch = nullptr;
    h.attach_callback(job, h.wrap_callback(&scratch, cb));
}

}  // namespace

// ---- #15 (S, nrsc 0x1800299c0..0x180029b10) ----
void** detour_decrease_job(void* mgr, void** out) {
    NetState& s = net_state();
    ChaosJobHost& h = s.jobs;
    if (!s.attached() || !h.decrease_ready()) {
        auto original = fn<void** (*)(void*, void**)>(s.original_decrease_job);
        return original ? original(mgr, out) : out;
    }
    void* job = h.alloc_object(layout::kDecreaseJobSize, layout::kJobAlign);
    if (!job) {
        *out = nullptr;
        return out;
    }
    std::memset(job, 0, layout::kDecreaseJobSize);  // refcount +8 = 0, +0x10 = 0, +0x60 = 0 (C); the rest is not read
    for (auto& slot : h.decrease_cb) {  // two callbacks, each replacing (destroying) the previous one (C)
        slot = make_callback(&kDecreaseCallbackVtbl, job, h.game_data, nullptr);
        register_callback(h, job, slot.get());
    }
    store<const void*>(job, layout::kJobVtbl, h.decrease_vtbl);
    std::atomic_ref<std::uint64_t>(*reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(job) + layout::kJobRefCount))
        .fetch_add(1);  // lock inc
    store<std::uint64_t>(job, layout::kDecreaseJobX70, 0);
    *out = job;
    return out;
}

// ---- #16 (S, nrsc 0x180029b20..0x180029ea2) ----
// Copies a game vector into `dst_off..` of the job: allocation of `bytes` via the data allocator, begin/end/cap (C). The
// original re-allocates once per element inside the copy loop (leaking all but the last); the final state is the same.
static void copy_vector(ChaosJobHost& h, void* job, std::size_t dst_off, const GameVec* v, std::size_t elem) {
    if (!v) return;
    const auto* b = static_cast<const std::uint8_t*>(v->begin);
    const auto* e = static_cast<const std::uint8_t*>(v->end);
    std::size_t bytes = static_cast<std::size_t>(e - b);
    if (bytes == 0) return;
    auto* dst = static_cast<std::uint8_t*>(h.alloc_data(bytes));
    if (!dst) return;
    std::memcpy(dst, b, bytes);
    const std::size_t used = bytes - bytes % elem;  // `and rbx,-8` for the u64 vector (C); whole elements for the others
    store<std::uint8_t*>(job, dst_off, dst);
    store<std::uint8_t*>(job, dst_off + 8, dst + used);
    store<std::uint8_t*>(job, dst_off + 16, dst + used);
}

void** detour_update_job(void* mgr, void** out, std::uint64_t a, std::uint64_t b, const GameVec* v64, const GameVec* v32,
                         std::uint32_t x, std::uint32_t y, const GameVec* v8, std::uint8_t flag) {
    NetState& s = net_state();
    ChaosJobHost& h = s.jobs;
    if (!s.attached() || !h.update_ready()) {
        auto original = fn<void** (*)(void*, void**, std::uint64_t, std::uint64_t, const GameVec*, const GameVec*,
                                      std::uint32_t, std::uint32_t, const GameVec*, std::uint8_t)>(s.original_update_job);
        return original ? original(mgr, out, a, b, v64, v32, x, y, v8, flag) : out;
    }
    void* job = h.alloc_object(layout::kUpdateJobSize, layout::kJobAlign);
    if (!job) {
        *out = nullptr;
        return out;
    }
    auto* bytes = static_cast<std::uint8_t*>(job);
    std::memset(job, 0, layout::kUpdateJobSize);
    store<const void*>(job, layout::kJobVtbl, h.update_vtbl);
    store<std::uint8_t*>(job, layout::kUpdateJobSelfPtr, bytes + 0x70);  // +0x68 = job+0x70 (C)
    store<std::uint64_t>(job, layout::kUpdateJobA, a);
    for (auto& slot : h.update_cb) {  // callbacks carry the 16 bytes {rate state, game data} (ctx98+8) (C)
        slot = make_callback(&kUpdateCallbackVtbl, job, &s.rate, h.game_data);
        register_callback(h, job, slot.get());
    }
    copy_vector(h, job, layout::kUpdateJobVec64, v64, 8);
    copy_vector(h, job, layout::kUpdateJobVec32, v32, 4);
    copy_vector(h, job, layout::kUpdateJobVec8, v8, 1);
    store<std::uint64_t>(job, layout::kUpdateJobB, b);
    store<std::uint32_t>(job, layout::kUpdateJobX, x);
    store<std::uint32_t>(job, layout::kUpdateJobY, y);
    store<std::uint8_t>(job, layout::kUpdateJobFlag, flag);
    std::atomic_ref<std::uint64_t>(*reinterpret_cast<std::uint64_t*>(bytes + layout::kJobRefCount)).fetch_add(1);
    *out = job;
    return out;
}

// ---- #18 (S, nrsc 0x18002aaa0..0x18002abaf) ----
// Types 1 and 0x18 go to every active peer on channels 5/6 and report success; the original is not called (C). Other
// types, and every type while no session is attached, go to the original.
bool detour_debug_send(void* self, void* data, std::uint32_t len, std::uint32_t type) {
    NetState& s = net_state();
    if (s.attached() && (type == kSessionTypeA || type == kSessionTypeB)) {
        const std::uint8_t channel = type == kSessionTypeB ? kChannelSessionType18 : kChannelSessionType1;
        for (server::Peer& peer : s.peers->slots())
            if (peer.active()) s.bus->send(peer.steam_id, as_bytes_of(data, len), channel);
        return true;
    }
    auto original = fn<bool (*)(void*, void*, std::uint32_t, std::uint32_t)>(s.original_debug_send);
    return original ? original(self, data, len, type) : false;
}

// ---- #19 (S, nrsc 0x18002abb0..0x18002ad17) ----
// Deviation from the original (W: it has no bound check): the copy is limited to in->size, so a queued message can never
// overrun the game's buffer. The game only passes in->buf / in->size that are non-zero (S, game 0x140e0bce0).
void* detour_network_recv(void* self, void* out, void* in, std::uint32_t type) {
    NetState& s = net_state();
    if (s.attached() && (type == kSessionTypeA || type == kSessionTypeB) && in && out) {
        auto* buf = load<std::uint8_t*>(in, layout::kPacketBuf);
        const std::uint64_t size = load<std::uint64_t>(in, layout::kPacketSize);
        if (buf && size) {
            std::uint8_t local[kReceiveBufferSize] = {};
            const std::uint8_t channel = type == kSessionTypeB ? kChannelSessionType18 : kChannelSessionType1;
            const std::size_t cap = static_cast<std::size_t>(std::min<std::uint64_t>(size, kReceiveBufferSize));
            const std::size_t n = s.pop_received(load<server::SteamId>(self, layout::kSessionPeerId), channel, local, cap);
            if (n) {
                std::memcpy(buf, local, n);
                store<std::uint8_t*>(out, layout::kPacketBuf, buf);
                store<std::uint64_t>(out, layout::kPacketSize, size);
                store<std::uint32_t>(out, layout::kPacketOutLen, static_cast<std::uint32_t>(n));
                return out;  // the original leaves rax unset (C); the game's own function returns `out`
            }
        }
    }
    auto original = fn<void* (*)(void*, void*, void*, std::uint32_t)>(s.original_network_recv);
    return original ? original(self, out, in, type) : out;
}

// ---- P1 (C, nrsc 0x180026230) ----
bool detour_team_collect(void* team, void* vec) {
    auto original = fn<bool (*)(void*, void*)>(net_state().original_team_collect);
    return original ? original(team, vec) : false;
}

// ---- P2 (C/S, nrsc 0x180026250..0x1800262fb) ----
// With a session and a send function: every peer record that is neither blocked (+0xe1) nor already sent to (+0xe3) gets
// the payload (type 0x94), whatever its index: the game's [first, last] range is dropped. Otherwise the original.
void detour_team_send(void* team, std::uint8_t first, std::uint8_t last, const GameVec* payload) {
    NetState& s = net_state();
    if (!s.attached() || !s.team_send || !team || !payload) {
        if (auto original = fn<void (*)(void*, std::uint8_t, std::uint8_t, const GameVec*)>(s.original_team_send))
            original(team, first, last, payload);
        return;
    }
    const auto len = static_cast<std::uint32_t>(static_cast<const std::uint8_t*>(payload->end) -
                                                static_cast<const std::uint8_t*>(payload->begin));
    auto* rec = load<std::uint8_t*>(team, layout::kTeamPeersBegin);
    auto* end = load<std::uint8_t*>(team, layout::kTeamPeersEnd);
    for (; rec != end; rec += layout::kTeamRecordSize) {
        if (rec[layout::kTeamRecBlocked] || rec[layout::kTeamRecSent]) continue;
        s.team_send(team, rec, kTeamPayloadType, payload->begin, len);
        rec[layout::kTeamRecSent] = 1;
    }
}

// ---- P4 (S/W, nrsc 0x180026370..0x1800264d7) ----
std::uint32_t detour_map_item_request(void* a0, std::uint64_t a1, std::uint64_t a2, const void* a3, void* a4,
                                      std::uint8_t a5, std::uint64_t a6) {
    NetState& s = net_state();
    auto original = fn<std::uint32_t (*)(void*, std::uint64_t, std::uint64_t, const void*, void*, std::uint8_t, std::uint64_t)>(
        s.original_map_item_request);
    if (!s.attached()) return original ? original(a0, a1, a2, a3, a4, a5, a6) : 0;
    if (s.mode_word && (*s.mode_word & 5) == 1 && a3) {
        // 0x28-byte message {a1, a6, *a3 (24 bytes)} to every peer with both mask bits set; original skipped; returns 0.
        std::uint8_t msg[kMapItemMessageSize];
        std::memcpy(msg, &a1, 8);
        std::memcpy(msg + 8, &a6, 8);
        std::memcpy(msg + 16, a3, 24);
        for (server::Peer& peer : s.peers->slots())
            if ((~peer.flags & kPeerReadyMask) == 0) s.bus->send(peer.steam_id, as_bytes_of(msg, sizeof msg), kChannelMapItem);
        return 0;
    }
    // Otherwise the original runs under the lock, with the request id published for P9 (C).
    std::lock_guard<std::mutex> guard(s.lock);
    s.current_request.store(static_cast<std::int64_t>(a1));
    const std::uint32_t r = original ? original(a0, a1, a2, a3, a4, a5, a6) : 0;
    s.unicast_id_a.store(-1);
    s.current_request.store(-1);
    return r;
}

// ---- P9 (S, nrsc 0x180026700..0x18002682b) ----
bool detour_session_broadcast(void* mgr, std::uint32_t type, void* data, std::uint32_t len) {
    NetState& s = net_state();
    auto original = fn<bool (*)(void*, std::uint32_t, void*, std::uint32_t)>(s.original_session_broadcast);
    const std::int32_t id_a = s.unicast_id_a.load();
    const std::int32_t id_b = s.unicast_id_b.load();
    if (!s.attached() || !s.player_send || !mgr || (id_a == -1 && id_b == -1)) return original ? original(mgr, type, data, len) : false;
    // id_a wins over id_b. A missing member returns false and sends nothing (C).
    const std::int32_t id = id_a != -1 ? id_a : id_b;
    auto* rec = load<std::uint8_t*>(mgr, layout::kSessMembersBegin);
    auto* end = load<std::uint8_t*>(mgr, layout::kSessMembersEnd);
    for (; rec != end; rec += layout::kSessRecordSize)
        if (load<std::int32_t>(rec, layout::kSessRecId) == id) return s.player_send(mgr, rec, type, data, len);
    return false;
}

// ---- wiring (docs/wiring_net.md) ----
namespace {

// The game's two allocator getters (ctx[0x98]+0x18 / +0x20): no-argument functions that return an allocator object.
// ChaosJobHost wants plain `(size, align)` callables, so the getter addresses live here. wire() sets them.
using AllocatorGetter = GameAllocator* (*)();
AllocatorGetter g_object_allocator_getter = nullptr;
AllocatorGetter g_data_allocator_getter = nullptr;

// nrsc 0x1800299dc..0x1800299f0: `a = getter(); a->vtbl[0x50](a, size, align)`.
void* alloc_object_via_getter(std::size_t size, std::size_t align) {
    GameAllocator* a = g_object_allocator_getter ? g_object_allocator_getter() : nullptr;
    return a && a->vtbl && a->vtbl->allocate ? a->vtbl->allocate(a, size, align) : nullptr;
}

// nrsc 0x180029cb8..0x180029cc5: `a = getter(); a->vtbl[1](a, size)`.
void* alloc_data_via_getter(std::size_t size) {
    GameAllocator* a = g_data_allocator_getter ? g_data_allocator_getter() : nullptr;
    if (!a || !a->vtbl) return nullptr;
    auto* vtbl = reinterpret_cast<void* (*const*)(GameAllocator*, std::size_t)>(a->vtbl);
    return vtbl[1] ? vtbl[1](a, size) : nullptr;
}

template <typename Fn>
Fn slot_fn(const std::function<std::uint64_t(unsigned)>& slot, unsigned off) {
    return reinterpret_cast<Fn>(static_cast<std::uintptr_t>(slot(off)));
}

}  // namespace

void wire_from_slots(const std::function<std::uint64_t(unsigned)>& slot, NetState& s) {
    // nrsc's own state object: mode word, force flag, and the object the job callbacks flag.
    s.mode_word = reinterpret_cast<const std::uint32_t*>(s.state_blob.data() + NetState::kStateModeWord);
    s.force_flag = s.state_blob.data() + NetState::kStateForceFlag;
    s.jobs.game_data = s.state_blob.data();
    s.rate.game_data = s.state_blob.data();

    // #14: ctx[0x78]->[0] is the address of the global; the detour reads **it.
    s.game_data_slot = reinterpret_cast<void* const*>(static_cast<std::uintptr_t>(slot(kSlotGameDataGlobal)));

    // #15/#16 helpers. Any missing slot leaves the matching *_ready() false, so the detour falls back to the original.
    g_object_allocator_getter = slot_fn<AllocatorGetter>(slot, kSlotAllocatorGetterA);
    g_data_allocator_getter = slot_fn<AllocatorGetter>(slot, kSlotAllocatorGetterB);
    s.jobs.alloc_object = g_object_allocator_getter ? &alloc_object_via_getter : nullptr;
    s.jobs.alloc_data = g_data_allocator_getter ? &alloc_data_via_getter : nullptr;
    s.jobs.wrap_callback = slot_fn<void* (*)(void**, void*)>(slot, kSlotWrapCallback);
    s.jobs.attach_callback = slot_fn<void (*)(void*, void*)>(slot, kSlotAttachCallback);
    s.jobs.decrease_vtbl = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(slot(kSlotDecreaseJobVtbl)));
    s.jobs.update_vtbl = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(slot(kSlotUpdateJobVtbl)));

    // P2 / P9 send helpers: the game's own functions (call targets of the signature sites).
    s.team_send = slot_fn<decltype(s.team_send)>(slot, kSlotTeamSendFn);
    s.player_send = slot_fn<decltype(s.player_send)>(slot, kSlotPlayerSendFn);

    // boss_list stays nullopt (vanilla): its contents come from the session layer (docs/wiring_net.md).
    // unicast_id_a/b stay -1 (live value in every phase).
}

void wire(const signatures::SignatureTable& table, const config::Settings& settings) {
    (void)settings;  // no setting reaches the net group
    wire_from_slots([&](unsigned off) { return table.at(off); }, net_state());
}

// ---- hook table ----
std::vector<HookSpec> build_specs(const std::function<std::uint64_t(unsigned)>& slot, NetState& s) {
    auto addr = [](auto* p) { return reinterpret_cast<void*>(p); };
    const std::vector<std::uint8_t> call_op{0xE8};
    // Vtable slots: the table holds the vtable VA (RTTI lookup); 0 stays 0 so the installer rejects it.
    auto vslot = [&](unsigned off, std::uint32_t slot_off) { const std::uint64_t v = slot(off); return v ? v + slot_off : 0; };
    std::vector<HookSpec> specs;
    // #8/#9: both sites call 0x140202aa0 (C), so they share one original slot.
    specs.push_back({"hook#8 class id broadcast (site 1)", Kind::CallSite, slot(kSlotSetClassA), addr(&detour_set_class),
                     &s.original_set_class, {}, call_op});
    specs.push_back({"hook#9 class id broadcast (site 2)", Kind::CallSite, slot(kSlotSetClassB), addr(&detour_set_class),
                     &s.original_set_class, {}, call_op});
    // #12 / P2: Kind::Inline, not the E9-at-entry of the original, to keep a way back to the game's function (see header).
    // TODO(known-issue): MinHook's relocation of these two prologues is untested in game (docs/TODO.md).
    specs.push_back({"hook#12 boss list copy", Kind::Inline, slot(kSlotBossListCopy), addr(&detour_boss_list), &s.original_boss_list, {}, {}});
    specs.push_back({"hook#13 rate selector snoop", Kind::CallSite, slot(kSlotRateSnoop), addr(&detour_rate_snoop),
                     &s.original_rate_snoop, {}, call_op});
    specs.push_back({"hook#14 game flag getter", Kind::CallSite, slot(kSlotGameFlagGetter), addr(&detour_game_flag),
                     &s.original_game_flag, {}, call_op});
    specs.push_back({"hook#15 local ChaosMatchRateDecrease job", Kind::CallSite, slot(kSlotDecreaseJob), addr(&detour_decrease_job),
                     &s.original_decrease_job, {}, call_op});
    specs.push_back({"hook#16 local ChaosMatchRateUpdate job", Kind::CallSite, slot(kSlotUpdateJob), addr(&detour_update_job),
                     &s.original_update_job, {}, call_op});
    specs.push_back({"hook#18 PlayerDebugSession send", Kind::VtableSlot, vslot(kSlotDebugSession, kVtableSlotDebugSend),
                     addr(&detour_debug_send), &s.original_debug_send, {}, {}});
    specs.push_back({"hook#19 PlayerNetworkSession receive", Kind::VtableSlot, vslot(kSlotNetworkSession, kVtableSlotNetworkRecv),
                     addr(&detour_network_recv), &s.original_network_recv, {}, {}});
    specs.push_back({"hookP1 team payload collect", Kind::CallSite, slot(kSlotTeamCollect), addr(&detour_team_collect),
                     &s.original_team_collect, {}, call_op});
    specs.push_back({"hookP2 team payload send", Kind::Inline, slot(kSlotTeamSend), addr(&detour_team_send), &s.original_team_send, {}, {}});
    specs.push_back({"hookP4 map item request", Kind::Inline, slot(kSlotMapItemRequest), addr(&detour_map_item_request),
                     &s.original_map_item_request, {}, {}});
    specs.push_back({"hookP9 session broadcast unicast", Kind::CallSite, slot(kSlotSessionBroadcast), addr(&detour_session_broadcast),
                     &s.original_session_broadcast, {}, call_op});
    return specs;
}

}  // namespace nrsc::hooks::net

namespace nrsc::hooks::groups {

std::vector<HookSpec> net(const signatures::SignatureTable& table, const config::Settings& settings) {
    (void)settings;  // no setting influences the net group (the preference bit 0xa7f418 belongs to the world group)
    hooks::net::NetState& state = hooks::net::reset_net_state();
    return hooks::net::build_specs([&](unsigned off) { return table.at(off); }, state);
}

}  // namespace nrsc::hooks::groups
