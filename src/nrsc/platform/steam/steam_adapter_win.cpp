// Windows implementation of SteamAdapter over the Steamworks flat API (steam_api64.dll exports). See
// docs/steam_adapter.md. No Steamworks SDK headers: the minimal ABI pieces are declared here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "nrsc/platform/steam/steam_adapter.hpp"

namespace nrsc::platform::steam {
namespace {

using SteamAPICall_t = std::uint64_t;

// CCallbackBase (steam_api_internal.h, S). No virtual destructor (the SDK says so explicitly, the binary interface is
// exported across DLLs). MSVC vtable order = declaration order: Run(void*), Run(void*, bool, SteamAPICall_t),
// GetCallbackSizeBytes(). Fields after the vptr: uint8 m_nCallbackFlags @8, int m_iCallback @12.
// m_nCallbackFlags: 1 = k_ECallbackFlagsRegistered (set by steam_api64), 2 = k_ECallbackFlagsGameServer.
class CCallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool io_failure, SteamAPICall_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    std::uint8_t m_nCallbackFlags = 0;
    int m_iCallback = 0;
};
static_assert(sizeof(CCallbackBase) == 16);  // vptr + uint8 flags (+pad) + int id

// Struct sizes (S) for GetCallbackSizeBytes, checked by Steam against the posted callback.
int callback_size(int id) {
    switch (id) {
        case kCbLobbyCreated: return kLobbyCreatedSize;
        case kCbLobbyEnter: return kLobbyEnterSize;
        case kCbLobbyChatUpdate: return kLobbyChatUpdateSize;
        case kCbLobbyDataUpdate: return kLobbyDataUpdateSize;
        case kCbGameLobbyJoinRequested: return kGameLobbyJoinRequestedSize;
        case kCbSessionRequest: return kSessionRequestSize;
        case kCbSessionFailed: return kSessionFailedSize;
        case kCbRelayNetworkStatus: return 712;  // sizeof(SteamRelayNetworkStatus_t), S; only the first dword is read
        default: return 0;
    }
}

// One registered callback (broadcast) or one pending call result.
class CallbackObject final : public CCallbackBase {
public:
    CallbackObject(int id, ISteamCallbackSink& sink) : sink_(sink) { m_iCallback = id; }
    void Run(void* param) override {
        // Relay status: only the first dword is read (C), so never read past it (the struct's size is S and unchecked).
        const auto n = static_cast<std::size_t>(m_iCallback == kCbRelayNetworkStatus ? static_cast<int>(kRelayStatusMinSize)
                                                                                      : callback_size(m_iCallback));
        dispatch_steam_callback(m_iCallback, {static_cast<const std::byte*>(param), n}, sink_);
    }
    void Run(void* param, bool io_failure, SteamAPICall_t) override {
        done.store(true);
        if (io_failure) return;  // a failed call result carries no data; the broadcast callback path still reports
        Run(param);
    }
    int GetCallbackSizeBytes() override { return callback_size(m_iCallback); }
    std::atomic<bool> done{false};

private:
    ISteamCallbackSink& sink_;
};

// Opaque interface pointers (flat API takes them as `ISteamXxx*`).
struct Iface;

struct SteamNetConnInfoBuf { alignas(8) std::uint8_t b[1024]; };  // scratch, larger than SteamNetConnectionRealTimeStatus_t

struct Api {
    HMODULE mod = nullptr;
    // registration
    void(__cdecl* RegisterCallback)(CCallbackBase*, int) = nullptr;
    void(__cdecl* UnregisterCallback)(CCallbackBase*) = nullptr;
    void(__cdecl* RegisterCallResult)(CCallbackBase*, SteamAPICall_t) = nullptr;
    void(__cdecl* UnregisterCallResult)(CCallbackBase*, SteamAPICall_t) = nullptr;
    // accessors
    Iface*(__cdecl* SteamMatchmaking)() = nullptr;       // SteamAPI_SteamMatchmaking_v009
    Iface*(__cdecl* SteamNetMessages)() = nullptr;       // SteamAPI_SteamNetworkingMessages_SteamAPI_v002
    Iface*(__cdecl* SteamUser)() = nullptr;              // SteamAPI_SteamUser_v021 (the only export in this build)
    // ISteamMatchmaking
    int(__cdecl* GetNumLobbyMembers)(Iface*, std::uint64_t) = nullptr;
    std::uint64_t(__cdecl* GetLobbyMemberByIndex)(Iface*, std::uint64_t, int) = nullptr;
    const char*(__cdecl* GetLobbyData)(Iface*, std::uint64_t, const char*) = nullptr;
    bool(__cdecl* SetLobbyData)(Iface*, std::uint64_t, const char*, const char*) = nullptr;
    std::uint64_t(__cdecl* GetLobbyOwner)(Iface*, std::uint64_t) = nullptr;
    SteamAPICall_t(__cdecl* CreateLobby)(Iface*, int, int) = nullptr;
    SteamAPICall_t(__cdecl* JoinLobby)(Iface*, std::uint64_t) = nullptr;
    void(__cdecl* LeaveLobby)(Iface*, std::uint64_t) = nullptr;
    bool(__cdecl* RequestLobbyData)(Iface*, std::uint64_t) = nullptr;
    // ISteamNetworkingMessages (identity passed by pointer = C++ reference)
    int(__cdecl* SendMessageToUser)(Iface*, const SteamNetworkingIdentity*, const void*, std::uint32_t, int, int) = nullptr;
    int(__cdecl* ReceiveMessagesOnChannel)(Iface*, int, void**, int) = nullptr;
    bool(__cdecl* AcceptSessionWithUser)(Iface*, const SteamNetworkingIdentity*) = nullptr;
    bool(__cdecl* CloseSessionWithUser)(Iface*, const SteamNetworkingIdentity*) = nullptr;
    int(__cdecl* GetSessionConnectionInfo)(Iface*, const SteamNetworkingIdentity*, void*, void*) = nullptr;
    void(__cdecl* MessageRelease)(void*) = nullptr;      // SteamAPI_SteamNetworkingMessage_t_Release
    // ISteamUser
    std::uint64_t(__cdecl* GetSteamID)(Iface*) = nullptr;
    // ISteamApps
    Iface*(__cdecl* SteamApps)() = nullptr;              // SteamAPI_SteamApps_v008
    const char*(__cdecl* GetCurrentGameLanguage)(Iface*) = nullptr;
};

template <typename F>
bool load(HMODULE m, const char* name, F& out, std::string& err) {
    out = reinterpret_cast<F>(GetProcAddress(m, name));
    if (!out) {
        err += "missing export ";
        err += name;
        err += "; ";
        return false;
    }
    return true;
}

class Impl final : public SteamAdapter, public server::ISteamMatchmaking, public server::ISteamNetworkingMessages {
public:
    explicit Impl(ISteamCallbackSink& sink) : sink_(sink) {}
    ~Impl() override {
        for (auto& cb : broadcast_) api.UnregisterCallback(cb.get());
        for (auto& cr : results_)
            if (!cr->done.load()) api.UnregisterCallResult(cr.get(), cr_handle_of(cr.get()));
    }

    bool init(std::string& err) {
        api.mod = GetModuleHandleW(L"steam_api64.dll");
        if (!api.mod) {
            err = "steam_api64.dll is not loaded";
            return false;
        }
        bool ok = true;
        ok &= load(api.mod, "SteamAPI_RegisterCallback", api.RegisterCallback, err);
        ok &= load(api.mod, "SteamAPI_UnregisterCallback", api.UnregisterCallback, err);
        ok &= load(api.mod, "SteamAPI_RegisterCallResult", api.RegisterCallResult, err);
        ok &= load(api.mod, "SteamAPI_UnregisterCallResult", api.UnregisterCallResult, err);
        ok &= load(api.mod, "SteamAPI_SteamMatchmaking_v009", api.SteamMatchmaking, err);
        ok &= load(api.mod, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002", api.SteamNetMessages, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers", api.GetNumLobbyMembers, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex", api.GetLobbyMemberByIndex, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_GetLobbyData", api.GetLobbyData, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_SetLobbyData", api.SetLobbyData, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_GetLobbyOwner", api.GetLobbyOwner, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_CreateLobby", api.CreateLobby, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_JoinLobby", api.JoinLobby, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_LeaveLobby", api.LeaveLobby, err);
        ok &= load(api.mod, "SteamAPI_ISteamMatchmaking_RequestLobbyData", api.RequestLobbyData, err);
        ok &= load(api.mod, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser", api.SendMessageToUser, err);
        ok &= load(api.mod, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel", api.ReceiveMessagesOnChannel, err);
        ok &= load(api.mod, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser", api.AcceptSessionWithUser, err);
        ok &= load(api.mod, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo", api.GetSessionConnectionInfo, err);
        ok &= load(api.mod, "SteamAPI_SteamNetworkingMessage_t_Release", api.MessageRelease, err);
        // Optional: own SteamID.
        std::string ignored;
        load(api.mod, "SteamAPI_SteamUser_v021", api.SteamUser, ignored);
        load(api.mod, "SteamAPI_ISteamUser_GetSteamID", api.GetSteamID, ignored);
        load(api.mod, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser", api.CloseSessionWithUser, ignored);
        load(api.mod, "SteamAPI_SteamApps_v008", api.SteamApps, ignored);
        load(api.mod, "SteamAPI_ISteamApps_GetCurrentGameLanguage", api.GetCurrentGameLanguage, ignored);
        if (!ok) return false;

        mm_ = api.SteamMatchmaking();
        net_ = api.SteamNetMessages();
        if (!mm_ || !net_) {
            err = "Steam interface accessor returned null (Steam not initialised yet?)";
            return false;
        }
        static constexpr int kIds[] = {kCbLobbyCreated,   kCbLobbyEnter,        kCbLobbyChatUpdate,
                                       kCbSessionRequest, kCbSessionFailed,     kCbRelayNetworkStatus,
                                       kCbGameLobbyJoinRequested, kCbLobbyDataUpdate};  // original registration order
        for (int id : kIds) {
            auto cb = std::make_unique<CallbackObject>(id, sink_);
            api.RegisterCallback(cb.get(), id);
            broadcast_.push_back(std::move(cb));
        }
        return true;
    }

    server::ISteamMatchmaking& matchmaking() override { return *this; }
    server::ISteamNetworkingMessages& networking() override { return *this; }
    void* matchmaking_object() override { return mm_; }

    // ---- server::ISteamMatchmaking ----
    std::int32_t GetNumLobbyMembers(SteamId lobby) override { return api.GetNumLobbyMembers(mm_, lobby); }
    SteamId GetLobbyMemberByIndex(SteamId lobby, std::int32_t i) override { return api.GetLobbyMemberByIndex(mm_, lobby, i); }
    std::string GetLobbyData(SteamId lobby, const std::string& key) override {
        const char* v = api.GetLobbyData(mm_, lobby, key.c_str());
        return v ? std::string(v) : std::string();
    }
    bool SetLobbyData(SteamId lobby, const std::string& key, const std::string& value) override {
        return api.SetLobbyData(mm_, lobby, key.c_str(), value.c_str());
    }
    SteamId GetLobbyOwner(SteamId lobby) override { return api.GetLobbyOwner(mm_, lobby); }

    // ---- server::ISteamNetworkingMessages ----
    int SendMessageToUser(SteamId target, std::span<const std::byte> data, int flags, int channel) override {
        const SteamNetworkingIdentity id = make_steamid_identity(target);
        return api.SendMessageToUser(net_, &id, data.data(), static_cast<std::uint32_t>(data.size()), flags, channel);
    }
    std::vector<server::ReceivedMessage> ReceiveMessagesOnChannel(int channel, int max_messages) override {
        std::vector<server::ReceivedMessage> out;
        void* msgs[256];
        const int cap = std::clamp(max_messages, 0, 256);
        if (cap == 0) return out;
        const int n = api.ReceiveMessagesOnChannel(net_, channel, msgs, cap);
        for (int i = 0; i < n; ++i) {
            const auto h = decode_message_header({static_cast<const std::byte*>(msgs[i]), kMsgOffChannel + 4});
            if (h && h->data && h->size >= 0) {
                server::ReceivedMessage m;
                m.identity_type = h->identity_type;
                m.steam_id = h->steam_id;
                const auto* p = static_cast<const std::byte*>(h->data);
                m.data.assign(p, p + h->size);
                out.push_back(std::move(m));
            }
            api.MessageRelease(msgs[i]);  // the poll releases every message it receives (msg+0xb8)
        }
        return out;
    }
    bool AcceptSessionWithUser(SteamId remote) override {
        const SteamNetworkingIdentity id = make_steamid_identity(remote);
        return api.AcceptSessionWithUser(net_, &id);
    }
    bool CloseSessionWithUser(SteamId remote) override {
        if (!api.CloseSessionWithUser) return false;
        const SteamNetworkingIdentity id = make_steamid_identity(remote);
        return api.CloseSessionWithUser(net_, &id);
    }

    // ---- extras ----
    void CreateLobby(int lobby_type, int max_members) override {
        track_call_result(api.CreateLobby(mm_, lobby_type, max_members), kCbLobbyCreated);
    }
    void JoinLobby(SteamId lobby) override { track_call_result(api.JoinLobby(mm_, lobby), kCbLobbyEnter); }
    void LeaveLobby(SteamId lobby) override { api.LeaveLobby(mm_, lobby); }
    bool RequestLobbyData(SteamId lobby) override { return api.RequestLobbyData(mm_, lobby); }
    // The original passes info = NULL and only reads the quality struct (C); we pass scratch buffers for both.
    bool GetSessionConnectionInfo(SteamId remote, std::int32_t& ping_ms) override {
        const SteamNetworkingIdentity id = make_steamid_identity(remote);
        SteamNetConnInfoBuf status{};
        const int state = api.GetSessionConnectionInfo(net_, &id, nullptr, status.b);
        if (state == 0) return false;  // k_ESteamNetworkingConnectionState_None: unknown session
        std::memcpy(&ping_ms, status.b + 4, 4);  // SteamNetConnectionRealTimeStatus_t::m_nPing
        return true;
    }
    std::string GameLanguage() override {
        if (!api.SteamApps || !api.GetCurrentGameLanguage) return {};
        Iface* apps = api.SteamApps();
        const char* lang = apps ? api.GetCurrentGameLanguage(apps) : nullptr;
        return lang ? std::string(lang) : std::string();
    }
    SteamId OwnSteamId() override {
        if (!api.SteamUser || !api.GetSteamID) return 0;
        Iface* u = api.SteamUser();
        return u ? api.GetSteamID(u) : 0;
    }

private:
    // The broadcast registrations already cover LobbyCreated/LobbyEnter on hosts where Steam posts them; the call
    // result registration guarantees delivery when it does not. Handlers must therefore be idempotent (they are:
    // server::Lobby clears create-pending atomically and only claims free peer slots).
    void track_call_result(SteamAPICall_t call, int id) {
        if (call == 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::erase_if(results_, [](const std::unique_ptr<CallbackObject>& c) { return c->done.load(); });
        auto cr = std::make_unique<CallbackObject>(id, sink_);
        api.RegisterCallResult(cr.get(), call);
        handles_.push_back({cr.get(), call});
        results_.push_back(std::move(cr));
    }
    SteamAPICall_t cr_handle_of(CallbackObject* o) const {
        for (auto& h : handles_)
            if (h.first == o) return h.second;
        return 0;
    }

    ISteamCallbackSink& sink_;
    Api api;
    Iface* mm_ = nullptr;
    Iface* net_ = nullptr;
    std::vector<std::unique_ptr<CallbackObject>> broadcast_;
    std::mutex mu_;
    std::vector<std::unique_ptr<CallbackObject>> results_;
    std::vector<std::pair<CallbackObject*, SteamAPICall_t>> handles_;
};

}  // namespace

std::unique_ptr<SteamAdapter> SteamAdapter::create(ISteamCallbackSink& sink, std::string* error) {
    auto impl = std::make_unique<Impl>(sink);
    std::string err;
    if (!impl->init(err)) {
        if (error) *error = err;
        return nullptr;
    }
    return impl;
}

}  // namespace nrsc::platform::steam
