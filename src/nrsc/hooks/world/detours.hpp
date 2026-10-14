#pragma once
// Detours of the world group (docs/hooks.md section 9). Free functions with the game's argument lists (x64: this and
// arguments as pointers or integers; the original's leading `ctx` is gone, the state is world::state()).
// Grades: behaviour C/S/W/U, evidence in detours.cpp and evidence/hooks/detours/group*.md.
#include <cstdint>

namespace nrsc::hooks::world {

// --- protected-installed (P*) -----------------------------------------------------------------------------------------
void* detour_p0_game_data_ctor(void* mem);                                  // 0x180026200
std::uint32_t detour_p5_battle_action(void* a0, void* a1, std::int32_t a2, std::int32_t a3, std::uint8_t a4,
                                      std::uint8_t a5, std::uint8_t a6, std::uint64_t a7);  // 0x1800264e0
void* detour_p6_handle_to_chr(void* out, std::uint32_t* handle);            // 0x1800265f0
bool detour_p7_is_local_or_owned(void* chr);                                // 0x180026690
bool detour_p8_session_contains(void* mgr, void* key);                      // 0x1800266c0
void* detour_p10_party_member_info_ctor(void* game_buffer);                 // 0x180026c50
void detour_p11_party_member_info_reset(void* pmi);                         // 0x180026e50
void* detour_p12_bloodstain_man_ctor(void* block);                          // 0x180027090
bool detour_p13_player_flag(std::uint32_t player_id);                       // 0x180027310
void detour_p14_chr_action(void* obj);                                      // 0x180027870
bool detour_p15_effect_handle_valid(void* handle);                          // 0x180027dc0
// P16 (0x180028030) has no readable reference; the live patch at slot 0x5d0 is a BytePatch (see world.cpp).
// Its helper is kept for completeness: min(pref ? 6 : 3, cap) with 1 for a missing owner.
std::uint32_t p16_slot_group_size(const void* a1);

// --- hooks.json #1, #6, #17, #20-#27 ---------------------------------------------------------------------------------
std::uint64_t detour_h1_lua_register(void* self, void* a2, const char* name, std::uint64_t a4);   // 0x180028990
bool detour_h6_sign_in(void* a, void* b);                                                          // 0x180028c20
std::uint64_t detour_h17_auto_control_register(void* self, void* entry, const char* name, std::uint64_t a4);  // 0x180029ec0
void detour_h20_lobby_update_state(void* self);                             // 0x18002ad20
void detour_h21_lobby_member_count(void* self, std::uint32_t value);        // 0x18002ad40
void detour_h22_lobby_flag(void* self, std::uint8_t flag);                  // 0x18002ae00
void detour_h23_ec_player_no(void* cond, void* a2, void* a3);               // 0x18002ae90
std::uint64_t detour_h24_ec_chr_dead(void* cond, void* a2, void* a3);       // 0x18002afa0
std::uint64_t detour_h25_ec_team_type(void* cond, void* a2, void* a3);      // 0x18002afc0
std::uint64_t detour_h26_ec_event_flag_state(void* cond, void* a2, void* a3);  // 0x18002b1f0
void detour_h27_ec_sortie_member_num(void* cond, void* a2, void* a3);       // 0x18002b230

// Helpers shared with the tests.
std::uint32_t lobby_member_count();  // size of the member vector / 0xf0 (0 when absent)

}  // namespace nrsc::hooks::world
