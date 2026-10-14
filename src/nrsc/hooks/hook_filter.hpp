#pragma once
// Which built HookSpecs the DLL actually installs. Two rules, applied in this order (the log shows it):
//   1. Diagnostic (not in the original): nrsc_disable_hooks.txt next to the DLL lists hook-name prefixes to skip, one per
//      line. The line "no-auto-host" also turns off the session layer's auto-hosted lobby (dll/session_driver).
//   2. Held back: hooks whose behaviour is known to be wrong or unsafe without state the reconstruction has not recovered
//      (docs/TODO.md). The game's own code runs for them.
// Hook names are matched by prefix ("hook#7 ..." matches "hook#7"), which is why HookSpec names are part of the interface.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nrsc/hooks/hook.hpp"

namespace nrsc::hooks {

// The diagnostic line that disables auto-hosting instead of naming a hook.
inline constexpr std::string_view kNoAutoHostLine = "no-auto-host";

// One raw line of nrsc_disable_hooks.txt (as fgets returns it) -> the entry, or nullopt for blank and '#' comment lines.
// Only trailing '\n', '\r' and ' ' are trimmed.
std::optional<std::string> parse_disabled_hooks_line(std::string line);

// Why `name` is held back, or nullopt when it may be installed.
std::optional<std::string_view> held_back_reason(std::string_view name);

// Removes the disabled hooks, then the held-back ones, from `specs`, reporting each removal through `log` with the exact
// texts the DLL log has always used.
void filter_hooks(std::vector<HookSpec>& specs, const std::vector<std::string>& disabled_prefixes,
                  const std::function<void(const std::string&)>& log);

}  // namespace nrsc::hooks
