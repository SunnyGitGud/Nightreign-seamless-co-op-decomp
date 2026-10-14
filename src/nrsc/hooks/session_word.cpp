#include "nrsc/hooks/session_word.hpp"

#include <cstdint>
#include <cstring>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/hooks/world/wiring.hpp"

namespace nrsc::hooks {

void set_party_session_active(bool active) {
    world::set_party_session_active(active);

    const auto apply = [active](std::uint32_t word) {
        return active ? (word | world::kPartySessionWord) : (word & ~world::kPartySessionWord);
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
