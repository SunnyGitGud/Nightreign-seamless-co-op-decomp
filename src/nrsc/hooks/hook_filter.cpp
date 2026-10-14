#include "nrsc/hooks/hook_filter.hpp"

#include <algorithm>

namespace nrsc::hooks {

namespace {

bool starts_with(std::string_view name, std::string_view prefix) { return name.rfind(prefix, 0) == 0; }

}  // namespace

std::optional<std::string> parse_disabled_hooks_line(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') return std::nullopt;
    return line;
}

std::optional<std::string_view> held_back_reason(std::string_view name) {
    // TODO(known-issue): #7 replaces the game's scaling function outright and #10 builds its rows. Their target-id list
    // (80 ids, filled by protected code) is not recovered; the stand-in could mis-scale enemies, so both stay out and the
    // game's own scaling runs (docs/wiring_params.md, docs/TODO.md).
    if (starts_with(name, "hook#7") || starts_with(name, "hook#10")) return "scaling target ids not recovered";
    // TODO(known-issue): P12 (6-slot bloodstain block) crashes a new game (2026-10-15 test, bisected): game code at
    // 0x14072ad50 reads the block with the vanilla 0x68-byte layout (effect handle at +0x58). The original compensates
    // elsewhere (not recovered), so the vanilla 3-slot block stays until that is understood (docs/TODO.md).
    if (starts_with(name, "P12")) return "crashes a new game with the 6-slot layout; see docs/plan_gaps.md";
    return std::nullopt;
}

void filter_hooks(std::vector<HookSpec>& specs, const std::vector<std::string>& disabled_prefixes,
                  const std::function<void(const std::string&)>& log) {
    std::erase_if(specs, [&](const HookSpec& h) {
        for (const std::string& prefix : disabled_prefixes) {
            if (!prefix.empty() && starts_with(h.name, prefix)) {
                log("disabled " + h.name + " (nrsc_disable_hooks.txt)");
                return true;
            }
        }
        return false;
    });
    std::erase_if(specs, [&](const HookSpec& h) {
        const auto reason = held_back_reason(h.name);
        if (reason) log("held back " + h.name + " (" + std::string(*reason) + ")");
        return reason.has_value();
    });
}

}  // namespace nrsc::hooks
