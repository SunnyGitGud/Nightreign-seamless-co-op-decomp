#include "nrsc/hooks/session_word.hpp"

#include <cstdint>
#include <cstring>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/hooks/world/wiring.hpp"

namespace nrsc::hooks {

void set_party_session_active(bool active) { set_session_word(active ? world::kPartySessionWord : 0); }

void set_session_word(std::uint32_t value) {
    world::set_session_word(value);

    const auto apply = [value](std::uint32_t word) {
        return (word & ~world::kSessionWordMask) | (value & world::kSessionWordMask);
    };
    auto update = [&](std::uint8_t* word_bytes) {
        std::uint32_t w;
        std::memcpy(&w, word_bytes, sizeof w);
        w = apply(w);
        std::memcpy(word_bytes, &w, sizeof w);
    };
    params::ParamsState& p = params::params_state();
    update(const_cast<std::uint8_t*>(p.settings_block) + params::layout::kSettingsMode);
    update(net::net_state().state_blob.data());
}

}  // namespace nrsc::hooks
