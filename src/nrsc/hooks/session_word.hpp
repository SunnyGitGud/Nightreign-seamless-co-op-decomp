#pragma once
// The party session word (docs/wiring_world.md, docs/wiring_net.md): one u64 at the start of nrsc's master state that the
// original reaches as `*ctx[0xe0]`, `**ctx[0x58]` and the mode word `**(ctx[0x90]+0x10)`. It is 0 while alone and 0x21 (bits 0
// and 5) once a party exists (C, live captures 2026-10-15). The reconstruction keeps one copy per hook group, so this sets
// all three together. The session layer (phase 3) calls it when a party forms or ends.
namespace nrsc::hooks {

void set_party_session_active(bool active);

}  // namespace nrsc::hooks
