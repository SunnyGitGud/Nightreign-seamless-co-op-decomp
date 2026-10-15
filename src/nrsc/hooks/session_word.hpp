#pragma once
// The party session word (docs/wiring_world.md, docs/wiring_net.md): one u64 at the start of nrsc's master state that the
// original reaches as `*ctx[0xe0]`, `**ctx[0x58]` and the mode word `**(ctx[0x90]+0x10)`. It is 0 while alone and 0x21 (bits 0
// and 5) once a party exists on a guest, 0x05 (bits 0 and 2) on a host (C, live captures 2026-10-15 and session records
// 2026-10-16). The reconstruction keeps one copy per hook group, so this sets all three together (bits 0x25 only). The
// session layer calls set_session_word whenever the value changes.
#include <cstdint>

namespace nrsc::hooks {

void set_session_word(std::uint32_t word);
void set_party_session_active(bool active);  // = set_session_word(active ? 0x21 : 0)

}  // namespace nrsc::hooks
