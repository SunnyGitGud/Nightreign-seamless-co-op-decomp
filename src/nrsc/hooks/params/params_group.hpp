#pragma once
// Params group (docs/plan_gaps.md phase 2, WP9; docs/hooks.md section 9): hooks.json #2, #3, #4, #5, #7, #10, #11 and
// P3 (protected installer 0x1800221e0, row 3, detour 0x180026310).
//
// Grades: C confirmed from bytes, S strongly inferred, W weak, U unknown. The original reaches its state through the
// Hooks object `ctx` (filled by protected code); here the state is `ParamsState`, reached through params_state().
// Detours are free functions with the game's own argument lists (the original's invoker/_Do_call thunk that prepends
// `ctx` is replaced by the relay of nrsc::hooks::Installer).
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "nrsc/config/settings.hpp"
#include "nrsc/hooks/hook.hpp"
#include "nrsc/locale/message_repository.hpp"
#include "nrsc/params/regulation_manager.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::hooks::params {

// Signature-table slots of the hooked sites (docs/signatures.md, hooks.json tgt.table_off / sigtable_off).
inline constexpr unsigned kSlotEosBegin = 0x108;      // #2, raw-scan(2c340): entry of 0x140f271d0 (S)
inline constexpr unsigned kSlotLeaveOfflineCall = 0x110;  // #3, mode0 off 0xd: the E8 in STEP_NotifyLeaveOfflineModeWait (S)
inline constexpr unsigned kSlotTextLookup = 0x118;    // #4, mode1: call target 0x1427f4d20 (S)
inline constexpr unsigned kSlotSaveExtension = 0x120;  // #5, mode0 off 0x16: the E8 at 0x1425a1762 (S)
inline constexpr unsigned kSlotScalingEffect = 0x130;  // #7, mode1 off 0x10: entry of 0x1403db250 (S)
inline constexpr unsigned kSlotRegbndLoaded = 0x148;   // #10, mode0 off 0x12: the E8 at 0x1401abe89 (S)
inline constexpr unsigned kSlotParamRowGetter = 0x150;  // #11, mode1: entry of 0x140ea17e0 (C)
inline constexpr unsigned kSlotMapItemVisible = 0x298;  // P3, inline, entry of 0x140572c10 (C, protected row 3)

// Slots that feed the state wired by wire() (docs/wiring_params.md). Each value is an absolute address in the game image.
inline constexpr unsigned kSlotRemoveSpEffect = 0x70;   // fn 0x140504c40 (chr[0x190] ptr, id): live H+0x58 (C)
inline constexpr unsigned kSlotApplySpEffect = 0x78;    // fn 0x1403c80a0: live H+0x60 (C)
inline constexpr unsigned kSlotPostApply = 0x80;        // fn 0x1403e0ab0 (chr, float): live H+0x68 (C)
inline constexpr unsigned kSlotRefresh = 0x88;          // fn 0x140428bf0 (owner, flag): live H+0x70 (C)
inline constexpr unsigned kSlotSessionManager = 0x190;  // global 0x143c21ec8 -> CSSessionManager*; +0x78/+0x80 member vector (C)
inline constexpr unsigned kSlotParamRepository = 0x1c0; // global 0x143c28880 -> SoloParamRepository* (C)

// First generated SpEffect row id: live regulation manager +0x14 = 0x7fff0320 after 80 targets x 10 ids (S).
inline constexpr std::uint32_t kFirstGeneratedRowId = 0x7fff0000;
// Live value of the nrsc core object's first dword, the "mode word" W (constant in roundtable, matchmaking and
// expedition probes). (W & 5) == 1 holds. The meaning of its bits is U.
inline constexpr std::uint32_t kLiveModeWord = 0x21;

// Game structure layouts (documented offsets only).
namespace layout {
inline constexpr std::size_t kRowRefRow = 0x00;    // out{row*, id, tag}: hooks #11 (C, nrsc 0x1800297c0..0x180029858)
inline constexpr std::size_t kRowRefId = 0x08;
inline constexpr std::size_t kRowRefTag = 0x0c;
inline constexpr std::size_t kScalingArgId = 0x00;  // #7: `*param` is the 0x4f row id; `{u32 id, row*}` as FUN_180035630 (S)
inline constexpr std::size_t kChrSpEffectList = 0x190;  // the pointer stored at chr+0x190 is handed to the "remove SpEffect" helper (C, 0x1800292b3)
inline constexpr std::size_t kChrOwner = 0x1b8;         // **(chr+0x1b8) handed to the refresh helper (C)
inline constexpr std::size_t kChrScalingCount = 0x238;  // chr+0x238: player count the scaling was applied for (C)
inline constexpr std::size_t kItemFlagsHi = 0xaa;       // map item flags16: bit 7/8 are the 2-bit owner field (C)
inline constexpr std::size_t kItemFlagsLo = 0xab;
inline constexpr std::size_t kSettingsMode = 0x00;      // settings block: u32 mode word W, `(W & 5) == 1` (S)
inline constexpr std::size_t kSettingsSharedLoot = 0x194;  // settings block: u8 "everything visible" config byte (C)
inline constexpr std::size_t kSettingsBlockSize = 0x198;
}  // namespace layout

// Game helpers the scaling detour (#7) calls: hooks+0x58..0x70 in the original, the game's own sub-functions
// 0x140504c40, 0x1403c80a0, 0x1403e0ab0, 0x140428bf0 (C for the addresses, the extra register arguments are U).
// The pointers are filled by protected code (U): the default is null, and the detour then does nothing but bookkeeping.
struct GameEffectApi {
    void (*remove_speffect)(void* chr_speffect_list, std::int32_t id) = nullptr;          // hooks+0x58 = 0x140504c40
    void (*apply_speffect)(void* chr, std::int32_t id, void* source, std::int32_t flag) = nullptr;  // +0x60 = 0x1403c80a0
    void (*post_apply)(void* chr, float value) = nullptr;                                  // +0x68 = 0x1403e0ab0
    void (*refresh)(void* owner, std::int32_t flag) = nullptr;                             // +0x70 = 0x140428bf0
};

using TextHandler = std::function<const char16_t*(void* repo, std::uint32_t version, std::uint32_t category,
                                                  std::uint32_t id)>;

struct ParamsState {
    explicit ParamsState(const config::Settings& settings);

    // ---- originals (MinHook trampolines or relocated call targets), written by the Installer ----
    void* original_text_lookup = nullptr;  // #4: 0x1427f4d20 (repo, version, category, id) -> char16_t*   (ctx[0x50]+0x18)
    void* original_save_assign = nullptr;  // #5: 0x1425a2f40 (this, ptr, len)                              (ctx[0xc8]+0x20)
    void* original_regbnd_loaded = nullptr;  // #10: 0x140f4d3b0 (singleton, a, b)                          (ctx[0x88]+0x8)
    void* original_param_row = nullptr;    // #11: 0x140ea17e0 (out, id)                                    (ctx[0x88]+0x10)
    void* original_item_visible = nullptr;  // P3: 0x140572c10 (item) -> bool                               (ctx[0x90]+0x48)

    // ---- #4 text lookup override (S, nrsc 0x180028ac0) ----
    // Per-category handler map (std::map<u32, std::function> at *(ctx[0x50]+0x20)). Filled at startup by protected code
    // with exactly one entry, category 200 -> the message override FUN_18001e1c0 (C, docs/devirt.md 2.11); wire() does
    // the same.
    std::map<std::uint32_t, TextHandler> text_handlers;
    // The nrsc message repository (FUN_18000a1c0 target, ctx[0x50][1]) with the recovered key table and fixed texts
    // (kMessageKeys / kFixedTexts). Null when the locale files are missing: lookups then fall through to the original.
    // Replaced once by set_game_language(); read it through current_messages().
    std::shared_ptr<const locale::MessageRepository> messages;
    // The integer the override formats twice for id 0x52409: *(int*)(*ctx + 400) = the frame task's member count
    // (X+0x190, phase3 2.2 (5)). The session layer installs the source; 0 until then.
    std::function<std::int32_t()> text_special_value = [] { return std::int32_t{0}; };
    std::filesystem::path locale_dir;

    // ---- #5 save extension (S, nrsc 0x180028bf0) ----
    // UTF-16 text that replaces the game literal L".sl2" (len 4); ctx[0xc8] is a std::wstring. The original's contents are
    // built by protected code (U whether the leading '.' is included). The literal it replaces has the dot, so the
    // default is "." + settings.save_file_extension (S, by necessity).
    std::u16string save_extension;

    // ---- #7, #10, #11 scaling rows (C logic, params/regulation_manager) ----
    nrsc::params::RegulationManager regulation;
    // The game-backed row reader (FUN_180035630 over SoloParamRepository). Needs the repository pointer, which protected
    // code supplies (U): the default sees no rows, so the build creates nothing.
    nrsc::params::RowSource row_source = [](std::int32_t, std::int32_t) -> const std::byte* { return nullptr; };
    // The session member vector ctx[0x10]+0x78/+0x80, in bytes (records of 0xF0). U: filled by protected code, default 0
    // players (the detour then falls through to "fewer than 2 players: nothing").
    std::function<std::size_t()> member_vector_bytes = [] { return std::size_t{0}; };
    // `(*ctx[0x18] & 5) == 1` (the mode word, S). U: default false (override set 0).
    std::function<bool()> alternate_set = [] { return false; };
    // #10 (W): called once, just before the first build, when target_ids is still empty. wire() installs an enumerator
    // of the row ids of param 0x4f. The original's list (80 ids, filled by protected code) is not recoverable statically.
    std::function<std::vector<std::int32_t>()> target_source;
    // True when wire() resolved everything #7 needs (effect helpers, session manager, repository).
    bool scaling_wired = false;
    GameEffectApi effects;

    // ---- P3 shared loot (C/S, nrsc 0x180026310) ----
    // The settings block *(ctx[0x90]+0x10): u32 mode word at +0, u8 config byte at +0x194. U: filled by protected code;
    // the default points at a zeroed block (config byte clear, mode 0), which gives "original OR nothing" = original.
    const std::uint8_t* settings_block = nullptr;  // set to `zero_settings` by the constructor

    // Text strings handed out by #4 stay alive for the process lifetime (the game keeps the pointer).
    std::mutex text_mutex;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::unique_ptr<std::u16string>> text_cache;
    std::vector<std::unique_ptr<std::u16string>> text_retired;

    std::array<std::uint8_t, layout::kSettingsBlockSize> zero_settings{};
};

// Singleton owned by this module. groups::params() (re)creates it from the settings.
ParamsState& params_state();
ParamsState& reset_params_state(const config::Settings& settings);

// ---- detours (game argument lists) ----
// #2 (S) 0x140f271d0 (this, mode): EOS anti-cheat session begin. Never started, reports success. (nrsc 0x180004c80)
bool detour_eos_begin(void* cheat, std::uint8_t mode);
// #3 (S) call 0x140bc7f90 in STEP_NotifyLeaveOfflineModeWait: forced true, step returns early. (nrsc 0x180004c80)
bool detour_leave_offline_flag(void* singleton);
// #4 (S) 0x1427f4d20: handler map, then nrsc messages, then the original. (nrsc 0x180028ac0)
const char16_t* detour_text_lookup(void* repo, std::uint32_t version, std::uint32_t category, std::uint32_t id);
// #5 (S) 0x1425a2f40(this, ptr, len) at the SLContentFormat ctor: ".sl2" -> save extension. (nrsc 0x180028bf0)
void* detour_save_extension(void* dlstring, const char16_t* text, std::uint64_t length);
// #7 (S) 0x1403db250 (chr, row): applies the scaling SpEffect for 2-6 players, never calls the original. (nrsc 0x180029140)
void detour_scaling_effect(void* chr, const void* scaling_row_ref);
// #10 (S) 0x140f4d3b0 (singleton, a, b): original, then ready + build once. (nrsc 0x1800295e0)
std::uint64_t detour_regbnd_loaded(void* singleton, void* a, void* b);
// #11 (C) 0x140ea17e0 (out, id): override row, else the original. (nrsc 0x1800297c0)
void* detour_param_row(void* out, std::uint32_t id);
// P3 (C/S) 0x140572c10 (item) -> visible to the local player. (nrsc 0x180026310)
bool detour_item_visible(void* item);

// Wires the state that the original's protected code fills (docs/wiring_params.md). Call after groups::params()
// (which resets the state). Anything that cannot be resolved stays at its vanilla default. Never throws.
void wire(const nrsc::signatures::SignatureTable& table, const nrsc::config::Settings& settings,
          const std::filesystem::path& dll_dir);
// Same with a slot reader (tests). Slot values are addresses inside the live process: wire reads through them.
void wire_from_slots(const std::function<std::uint64_t(unsigned)>& slot, const nrsc::config::Settings& settings,
                     const std::filesystem::path& dll_dir);
// Builds the message repository from <locale_dir>/english.json; nullptr when the file is missing or unparsable
// (the original would show a Critical Error; vanilla text is the safe fallback). `keys`/`fixed` are the (category, id)
// tables, which only protected code knows (U): wire() passes none.
std::shared_ptr<const locale::MessageRepository> load_message_repository(const std::filesystem::path& locale_dir,
                                                                          std::vector<locale::MessageKey> keys = {},
                                                                          std::vector<locale::FixedText> fixed = {});
// The (category, id) -> key table and the fixed texts the original builds at startup (C, read from the live maps in
// the real-image emulation, docs/devirt.md 2.9).
const std::vector<locale::MessageKey>& message_keys();
const std::vector<locale::FixedText>& fixed_texts();
// Locale order of the original (C, devirt 2.9): "english" is loaded first, then the game's language
// (ISteamApps::GetCurrentGameLanguage) with english as its fallback. wire() loads english only (Steam is not up yet);
// the session layer calls this once Steam answers. Empty or "english" keeps the english repository.
void set_game_language(const std::string& language);
std::shared_ptr<const locale::MessageRepository> current_messages();
// The 80 regulation target ids (C: the regulation manager's vector right after startup in the real-image emulation,
// 7700..7779, docs/devirt.md 2.11).
std::vector<std::int32_t> scaling_target_ids();
// Row ids of one param in a SoloParamRepository (the sorted index of its file), at most `limit`.
std::vector<std::int32_t> list_row_ids(const std::byte* repository, std::int32_t param_index, std::size_t limit);

// Computes the HookSpec list from a slot reader (table.at in the DLL). Separate from groups::params so that tests can
// provide addresses without a scanned image.
std::vector<HookSpec> build_specs(const std::function<std::uint64_t(unsigned)>& slot, ParamsState& state);

}  // namespace nrsc::hooks::params
