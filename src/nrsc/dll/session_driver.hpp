#pragma once
// Drives the session layer (src/nrsc/session, docs/session.md) on the game thread.
//
// The original runs its per-frame server_manager task (0x180061140) through the game's task system and gets its Steam
// callbacks from SteamAPI_RunCallbacks, which the game pumps every frame. The reconstruction hooks SteamAPI_RunCallbacks
// in steam_api64.dll instead: after the original returns (all callbacks for this frame delivered), it ticks the session.
// Callbacks, the session tick and the game-thread hooks therefore share one thread, so NetState needs no lock (W: the
// original's task scheduler is U, docs/devirt.md 2.9). The Steam adapter is created on the first call, when Steam is
// known to be initialised.
#include <optional>

#include "nrsc/hooks/hook.hpp"

namespace nrsc::dll {

// The HookSpec for SteamAPI_RunCallbacks (Kind::Inline); nullopt (logged) if steam_api64.dll or the export is missing,
// in which case the game runs without co-op.
std::optional<hooks::HookSpec> session_driver_hook();

// Diagnostic: when false, no lobby is auto-hosted (set from the "no-auto-host" line of nrsc_disable_hooks.txt).
void set_auto_host_enabled(bool enabled);

}  // namespace nrsc::dll
