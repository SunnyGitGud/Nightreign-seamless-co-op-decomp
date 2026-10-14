// hooks::set_party_session_active keeps the one session word of the original (0 alone, 0x21 in a party) equal in all
// three hook groups (docs/wiring_world.md).
#include <gtest/gtest.h>

#include <cstring>

#include "nrsc/config/settings.hpp"
#include "nrsc/hooks/groups.hpp"
#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/hooks/session_word.hpp"
#include "nrsc/hooks/world/world_state.hpp"

namespace {

using namespace nrsc;

std::uint32_t params_word() {
    std::uint32_t w;
    std::memcpy(&w, hooks::params::params_state().settings_block + hooks::params::layout::kSettingsMode, sizeof w);
    return w;
}
std::uint32_t net_word() {
    std::uint32_t w;
    std::memcpy(&w, hooks::net::net_state().state_blob.data(), sizeof w);
    return w;
}

TEST(SessionWord, SoloIsZeroAndPartySetsBits0And5Everywhere) {
    const signatures::SignatureTable table;
    config::Settings settings;
    settings.save_file_extension = "co2";
    hooks::groups::world(table, settings);
    hooks::groups::params(table, settings);
    hooks::groups::net(table, settings);
    hooks::set_party_session_active(false);
    EXPECT_EQ(hooks::world::state().mode_word.load() & 0x21u, 0u);
    EXPECT_EQ(params_word() & 0x21u, 0u);
    EXPECT_EQ(net_word() & 0x21u, 0u);
    EXPECT_FALSE(hooks::params::params_state().alternate_set());

    hooks::set_party_session_active(true);
    EXPECT_EQ(hooks::world::state().mode_word.load() & 0x21u, 0x21u);
    EXPECT_EQ(params_word() & 0x21u, 0x21u);
    EXPECT_EQ(net_word() & 0x21u, 0x21u);
    EXPECT_TRUE(hooks::params::params_state().alternate_set());  // (0x21 & 5) == 1

    hooks::set_party_session_active(false);
    EXPECT_EQ(net_word() & 0x21u, 0u);
}

}  // namespace
