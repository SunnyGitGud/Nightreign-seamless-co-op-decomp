#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Abstract subset of the Steam interfaces used by nrsc.dll's server_manager (nrsc\mod\server_manager\server_manager.cpp).
// The original calls these through raw vtable slots of ISteamMatchmaking (009), ISteamNetworkingMessages (002) and
// ISteamUserStats (012); the offsets in the comments are the byte offsets used at the call sites
// (docs/server_manager.md 2.1-2.3). Nothing here links against the Steam SDK; tests supply mocks.
// The core lock (core+0x78, 0x18008f88c/0x18008f894) the original takes around each Steam call is not modelled.
namespace nrsc::server {

using SteamId = std::uint64_t;
using LeaderboardHandle = std::uint64_t;

// SteamNetworkingIdentity type 0x10 = SteamID (the 0x800000010 qword built at 0x18003f0fd is {type 0x10, size 8}).
inline constexpr std::uint32_t kIdentityTypeSteamId = 0x10;

// Send flags used by the mod: 9 = Reliable(8) | NoNagle(1); 5 = NoDelay(4) | NoNagle(1) (unreliable).
inline constexpr int kSendReliableNoNagle = 9;
inline constexpr int kSendUnreliableNoDelay = 5;

// k_EResultOK, tested after SendMessageToUser at 0x18003f17f.
inline constexpr int kResultOk = 1;

// ISteamMatchmaking. S (offsets from the call sites, interface identity from the offsets matching 009).
class ISteamMatchmaking {
public:
    virtual ~ISteamMatchmaking() = default;
    virtual std::int32_t GetNumLobbyMembers(SteamId lobby) = 0;                     // vtbl+0x88
    virtual SteamId GetLobbyMemberByIndex(SteamId lobby, std::int32_t index) = 0;   // vtbl+0x90
    // Returns "" for a missing key. The original gets a char* and memcmp()s 0x20 bytes.
    virtual std::string GetLobbyData(SteamId lobby, const std::string& key) = 0;    // vtbl+0x98
    virtual bool SetLobbyData(SteamId lobby, const std::string& key, const std::string& value) = 0;  // vtbl+0xa0
    virtual SteamId GetLobbyOwner(SteamId lobby) = 0;                               // vtbl+0x118
};

// A received ISteamNetworkingMessage, reduced to what the poll reads: identity type (msg+0x10), SteamID (msg+0x18),
// payload and size (msg+0, msg+8). Release() (msg+0xb8) is implicit: the poll releases every message it receives.
struct ReceivedMessage {
    std::uint32_t identity_type = kIdentityTypeSteamId;
    SteamId steam_id = 0;
    std::vector<std::byte> data;
};

// ISteamNetworkingMessages. S.
class ISteamNetworkingMessages {
public:
    virtual ~ISteamNetworkingMessages() = default;
    // vtbl+0: the identity is always a SteamID. Returns an EResult (kResultOk on success).
    virtual int SendMessageToUser(SteamId target, std::span<const std::byte> data, int send_flags,
                                  int remote_channel) = 0;
    // vtbl+8: the poll asks for at most 256 messages.
    virtual std::vector<ReceivedMessage> ReceiveMessagesOnChannel(int channel, int max_messages) = 0;
    // vtbl+0x10, called by the SessionRequest callback 0x1800644c0 (C). Not pure, so existing mocks need no change.
    virtual bool AcceptSessionWithUser(SteamId /*remote*/) { return false; }
};

// One LeaderboardEntry_t as read at 0x18003d6a0 (steam id first, then up to 2 detail ints).
struct LeaderboardEntry {
    SteamId steam_id = 0;
    std::int32_t details[2] = {0, 0};
};

// ISteamUserStats. S.
class ISteamUserStats {
public:
    virtual ~ISteamUserStats() = default;
    virtual void FindOrCreateLeaderboard(const std::string& name, int sort_method, int display_type) = 0;  // vtbl+0xb0
    virtual void DownloadLeaderboardEntriesForUsers(LeaderboardHandle handle, std::span<const SteamId> users) = 0;  // vtbl+0xe8
    // vtbl+0xf0, called with 2 detail slots. False when the index is not available.
    virtual bool GetDownloadedLeaderboardEntry(std::uint64_t entries_handle, std::int32_t index,
                                               LeaderboardEntry& out) = 0;
    virtual void UploadLeaderboardScore(LeaderboardHandle handle, int upload_method,
                                        std::span<const std::int32_t> details) = 0;  // vtbl+0xf8
};

}  // namespace nrsc::server
