#pragma once
// Detour groups (docs/plan_gaps.md phase 2, docs/hooks.md section 9). Each group builds its HookSpecs from the resolved
// signature table and owns its detours and their state. The DLL init installs them in this order: world, params, net.
#include <vector>

#include "nrsc/config/settings.hpp"
#include "nrsc/hooks/hook.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::hooks::groups {

// Party size and world state: P0, P5-P8, P10-P16, hooks.json #1, #6, #17, #20-#27 (src/nrsc/hooks/world/).
std::vector<HookSpec> world(const signatures::SignatureTable& table, const config::Settings& settings);

// Params, text, save file and offline flow: hooks.json #2-#5, #7, #10, #11, P3 (src/nrsc/hooks/params/).
std::vector<HookSpec> params(const signatures::SignatureTable& table, const config::Settings& settings);

// Session, P2P bridge and matchmaking: hooks.json #8, #9, #12-#16, #18, #19, P1, P2, P4, P9 (src/nrsc/hooks/net/).
std::vector<HookSpec> net(const signatures::SignatureTable& table, const config::Settings& settings);

}  // namespace nrsc::hooks::groups
