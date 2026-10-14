#include "nrsc/hooks/params/params_group.hpp"

#include <algorithm>
#include <cstring>
#include <exception>

#include "nrsc/hooks/groups.hpp"
#include "nrsc/params/param_file.hpp"

namespace nrsc::hooks::params {

namespace {


template <class T>
T load(const void* base, std::size_t offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + offset, sizeof v);
    return v;
}
template <class T>
void store(void* base, std::size_t offset, T v) {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &v, sizeof v);
}

std::unique_ptr<ParamsState>& state_slot() {
    static std::unique_ptr<ParamsState> s;
    return s;
}

}  // namespace

ParamsState::ParamsState(const config::Settings& settings)
    : regulation(settings.preferences, kFirstGeneratedRowId) {
    save_extension = u".";
    for (char c : settings.save_file_extension) save_extension.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    settings_block = zero_settings.data();
    alternate_set = [this] {  // (W & 5) == 1 over the same core object P3 reads (live: ctx[0x70]+0x18 == ctx[0x90]+0x10)
        std::uint32_t w;
        std::memcpy(&w, settings_block + layout::kSettingsMode, sizeof w);
        return (w & 5u) == 1u;
    };
}

ParamsState& params_state() {
    if (!state_slot()) state_slot() = std::make_unique<ParamsState>(config::Settings{});
    return *state_slot();
}
ParamsState& reset_params_state(const config::Settings& settings) {
    state_slot() = std::make_unique<ParamsState>(settings);
    return *state_slot();
}

// ---- #2 / #3 (S) ----
bool detour_eos_begin(void*, std::uint8_t) { return true; }
bool detour_leave_offline_flag(void*) { return true; }

// ---- #4 (S, nrsc 0x180028ac0..0x180028be8) ----
const char16_t* detour_text_lookup(void* repo, std::uint32_t version, std::uint32_t category, std::uint32_t id) {
    ParamsState& s = params_state();
    // (1) per-category handler: lower_bound(category), exact match only (C).
    if (auto h = s.text_handlers.find(category); h != s.text_handlers.end() && h->second) {
        return h->second(repo, version, category, id);
    }
    // (2) nrsc message repository (FUN_18000a1c0): non-null result wins (C). The pointer must outlive the call.
    if (s.messages) {
        if (auto text = s.messages->lookup(static_cast<std::int32_t>(category), static_cast<std::int32_t>(id))) {
            std::lock_guard lock(s.text_mutex);
            auto& slot = s.text_cache[{category, id}];
            if (!slot) {
                slot = std::make_unique<std::u16string>(std::move(*text));
            } else if (*slot != *text) {
                s.text_retired.push_back(std::move(slot));  // earlier pointers stay valid
                slot = std::make_unique<std::u16string>(std::move(*text));
            }
            return slot->c_str();
        }
    }
    // (3) the game's own text.
    auto original = reinterpret_cast<const char16_t* (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t)>(s.original_text_lookup);
    return original ? original(repo, version, category, id) : nullptr;
}

// ---- #5 (S, nrsc 0x180028bf0..0x180028c13) ----
void* detour_save_extension(void* dlstring, const char16_t*, std::uint64_t) {
    ParamsState& s = params_state();
    auto original = reinterpret_cast<void* (*)(void*, const char16_t*, std::uint64_t)>(s.original_save_assign);
    // Tail call with (this, contents of the extension wstring, its size) instead of (L".sl2", 4).
    return original ? original(dlstring, s.save_extension.c_str(), s.save_extension.size()) : nullptr;
}

// ---- #7 (S, nrsc 0x180029140..0x1800292e9; logic C in params/regulation_manager) ----
void detour_scaling_effect(void* chr, const void* scaling_row_ref) {
    ParamsState& s = params_state();
    const std::uint32_t row_id = scaling_row_ref ? load<std::uint32_t>(scaling_row_ref, layout::kScalingArgId) : 0xffffffffu;
    const std::uint32_t count = nrsc::params::player_count_from_span(s.member_vector_bytes());
    const nrsc::params::EffectDecision d = nrsc::params::choose_scaling_effect(s.regulation, s.alternate_set(), row_id, count);
    const GameEffectApi& api = s.effects;
    switch (d.action) {
        case nrsc::params::EffectAction::None:
            return;
        case nrsc::params::EffectAction::Clear:
            // `mov rcx,[rsi+0x190]` at 0x1800292b3: the pointer stored at chr+0x190 is passed, not its address (C).
            if (api.remove_speffect) api.remove_speffect(load<void*>(chr, layout::kChrSpEffectList), nrsc::params::kClearArgument);
            store<std::uint32_t>(chr, layout::kChrScalingCount, 0);
            return;
        case nrsc::params::EffectAction::Apply: {
            if (api.apply_speffect) api.apply_speffect(chr, d.spEffect_id, chr, 1);
            if (api.post_apply) api.post_apply(chr, 0.0f);
            if (api.refresh) {  // **(chr+0x1b8) (C: two loads)
                void* p = load<void*>(chr, layout::kChrOwner);
                api.refresh(p ? load<void*>(p, 0) : nullptr, 1);
            }
            if (d.store_player_count) store<std::uint32_t>(chr, layout::kChrScalingCount, *d.store_player_count);
            return;
        }
    }
}

// ---- #10 (S, nrsc 0x1800295e0) ----
std::uint64_t detour_regbnd_loaded(void* singleton, void* a, void* b) {
    ParamsState& s = params_state();
    auto original = reinterpret_cast<std::uint64_t (*)(void*, void*, void*)>(s.original_regbnd_loaded);
    const std::uint64_t r = original ? original(singleton, a, b) : 0;  // original first (C)
    if (!s.regulation.ready() && s.regulation.target_ids.empty() && s.target_source) {
        try {
            s.regulation.target_ids = s.target_source();
        } catch (...) {
        }
    }
    s.regulation.on_regulation_loaded(s.row_source);  // ready, then build(0)+build(1) exactly once
    return r;
}

// ---- #11 (C, nrsc 0x1800297c0..0x180029858) ----
void* detour_param_row(void* out, std::uint32_t id) {
    ParamsState& s = params_state();
    if (auto hit = s.regulation.find_override(0, nrsc::params::kSpEffectParam, id)) {
        store<const std::byte*>(out, layout::kRowRefRow, hit->data);
        store<std::uint32_t>(out, layout::kRowRefId, hit->id);
        store<std::uint8_t>(out, layout::kRowRefTag, static_cast<std::uint8_t>(hit->tag));
        return out;
    }
    auto original = reinterpret_cast<void* (*)(void*, std::uint32_t)>(s.original_param_row);
    return original ? original(out, id) : out;
}

// ---- P3 shared loot (C, nrsc 0x180026317..0x18002636e) ----
bool detour_item_visible(void* item) {
    ParamsState& s = params_state();
    if (load<std::uint8_t>(s.settings_block, layout::kSettingsSharedLoot)) return true;  // config byte set: all visible
    auto original = reinterpret_cast<bool (*)(void*)>(s.original_item_visible);
    if (original && original(item)) return true;
    if ((load<std::uint32_t>(s.settings_block, layout::kSettingsMode) & 5) != 1) return false;
    // Owner bits: sign bit of byte 0xaa (flags bit 7) or bit 0 of byte 0xab (flags bit 8): non-zero owner field.
    if (load<std::uint8_t>(item, layout::kItemFlagsHi) & 0x80) return true;
    return (load<std::uint8_t>(item, layout::kItemFlagsLo) & 1) != 0;
}

std::vector<HookSpec> build_specs(const std::function<std::uint64_t(unsigned)>& slot, ParamsState& s) {
    auto fn = [](auto* p) { return reinterpret_cast<void*>(p); };
    const std::vector<std::uint8_t> call_op{0xE8};
    std::vector<HookSpec> specs;
    // #2: E9 at the entry of 0x140f271d0 (jmp-patch, no trampoline). Prologue from group1.md section 1a (C).
    specs.push_back({"hook#2 skip EOS session begin", Kind::JmpEntry, slot(kSlotEosBegin), fn(&detour_eos_begin), nullptr, {},
                     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0xB6, 0xFA, 0x8B, 0xCF}});
    specs.push_back({"hook#3 skip leave-offline notice", Kind::CallSite, slot(kSlotLeaveOfflineCall), fn(&detour_leave_offline_flag),
                     nullptr, {}, call_op});
    specs.push_back({"hook#4 FMG text lookup", Kind::Inline, slot(kSlotTextLookup), fn(&detour_text_lookup), &s.original_text_lookup, {}, {}});
    specs.push_back({"hook#5 save extension", Kind::CallSite, slot(kSlotSaveExtension), fn(&detour_save_extension),
                     &s.original_save_assign, {}, call_op});
    specs.push_back({"hook#7 multiplayer scaling effect", Kind::JmpEntry, slot(kSlotScalingEffect), fn(&detour_scaling_effect), nullptr, {}, {}});
    specs.push_back({"hook#10 build scaling rows", Kind::CallSite, slot(kSlotRegbndLoaded), fn(&detour_regbnd_loaded),
                     &s.original_regbnd_loaded, {}, call_op});
    specs.push_back({"hook#11 param 0xf row getter", Kind::Inline, slot(kSlotParamRowGetter), fn(&detour_param_row), &s.original_param_row, {}, {}});
    // P3: prologue is the signature at slot 0x298 (`48 89 5C 24 08 57 48 83 EC 20 F6 81 AA`, C).
    specs.push_back({"hookP3 shared loot visibility", Kind::Inline, slot(kSlotMapItemVisible), fn(&detour_item_visible),
                     &s.original_item_visible, {}, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0xAA}});
    return specs;
}


// ---- wiring of the state the original's protected code fills (docs/wiring_params.md) ----
namespace {

// The game's ApplySpEffect-style helper (0x1403c80a0) as the scaling detour calls it (asm 0x1800291fd..0x18002928a): rcx
// chr, edx id, r8 source, r9b flag, then seven stack arguments: two pointers to local option blocks, five bytes (0)
// and 0x10. The blocks are 4 floats of 1.0 (.rdata 0x180145da0) and a 0x40+ byte block whose first 32 bytes are 1.0.
using GameApplyFn = std::uint64_t (*)(void*, std::int32_t, void*, std::uint8_t, void*, void*, std::uint8_t, std::uint8_t,
                                      std::uint8_t, std::uint8_t, std::uint8_t, std::uint64_t);
GameApplyFn g_game_apply = nullptr;

void apply_speffect_thunk(void* chr, std::int32_t id, void* source, std::int32_t flag) {
    alignas(16) float a[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    alignas(16) float b[20] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    if (g_game_apply) g_game_apply(chr, id, source, static_cast<std::uint8_t>(flag), a, b, 0, 0, 0, 0, 0, 0x10);
}

template <class T>
T read_at(std::uint64_t address) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof v);
    return v;
}

constexpr std::size_t kMaxScalingTargets = 4096;

}  // namespace

std::vector<std::int32_t> list_row_ids(const std::byte* repository, std::int32_t param_index, std::size_t limit) {
    std::vector<std::int32_t> ids;
    if (!repository) return ids;
    // Same path as nrsc::params::find_row_in_repository (FUN_180035630): entry 0x48 bytes from +0x80, file at
    // *(*(entry+0x88)+0x80)+0x80 (the +0x80 of the final step is part of "file").
    const std::byte* entry = repository + static_cast<std::uint32_t>(param_index * 0x48);
    if (load<const std::byte*>(entry, 0x80) == nullptr) return ids;
    const auto* holder = load<const std::byte*>(entry, 0x88);
    if (!holder) return ids;
    const auto* inner = load<const std::byte*>(holder, 0x80);
    if (!inner) return ids;
    const std::byte* file = load<const std::byte*>(inner, 0x80);
    if (!file) return ids;
    const std::int32_t count = load<std::int32_t>(file - 0x0c, 0);
    if (count < 1) return ids;
    const std::uint32_t index_offset = (load<std::uint32_t>(file - 0x10, 0) + 0xfu) & 0xfffffff0u;
    const std::byte* index = file + index_offset;
    for (std::int32_t i = 0; i < count && ids.size() < limit; ++i) {
        const auto id = load<std::uint32_t>(index, static_cast<std::size_t>(i) * 8);
        const auto slot = load<std::uint32_t>(index, static_cast<std::size_t>(i) * 8 + 4);
        if (slot != 0xffffffffu && static_cast<std::int32_t>(id) >= 0) ids.push_back(static_cast<std::int32_t>(id));
    }
    return ids;
}

std::shared_ptr<const locale::MessageRepository> load_message_repository(const std::filesystem::path& locale_dir,
                                                                          std::vector<locale::MessageKey> keys,
                                                                          std::vector<locale::FixedText> fixed) {
    try {
        // Language selection and the English-first order are in protected code (U, docs/localisation.md): english only.
        auto locale = locale::load_locale(locale_dir, "english", nullptr, {});
        return std::make_shared<const locale::MessageRepository>(std::move(locale), std::move(keys), std::move(fixed));
    } catch (...) {
        return nullptr;  // the original would raise a Critical Error; keep the game's own text instead
    }
}

void wire_from_slots(const std::function<std::uint64_t(unsigned)>& slot, const config::Settings&,
                     const std::filesystem::path& dll_dir) {
    ParamsState& s = params_state();

    // P3 + mode word: core object = settings block, byte +0x194 = 0 (default settings). The first dword is the party session
    // word: 0 while alone (solo captures) and 0x21 during a party (C, docs/wiring_world.md). It starts at 0 here and is set
    // by hooks::set_party_session_active() (session layer), not at wiring time.
    const std::uint32_t solo = 0;
    std::memcpy(const_cast<std::uint8_t*>(s.settings_block) + layout::kSettingsMode, &solo, sizeof solo);

    // #7 game helpers: the sig slots hold the helper entry points; live H+0x58..0x70 equal them (C).
    const std::uint64_t rem = slot(kSlotRemoveSpEffect), app = slot(kSlotApplySpEffect), post = slot(kSlotPostApply),
                        ref = slot(kSlotRefresh);
    const bool helpers = rem && app && post && ref;
    if (helpers) {
        g_game_apply = reinterpret_cast<GameApplyFn>(static_cast<std::uintptr_t>(app));
        s.effects.remove_speffect = reinterpret_cast<void (*)(void*, std::int32_t)>(static_cast<std::uintptr_t>(rem));
        s.effects.apply_speffect = &apply_speffect_thunk;
        s.effects.post_apply = reinterpret_cast<void (*)(void*, float)>(static_cast<std::uintptr_t>(post));
        s.effects.refresh = reinterpret_cast<void (*)(void*, std::int32_t)>(static_cast<std::uintptr_t>(ref));
    }

    // #7 player count: *(*(CSSessionManager global) + 0x78/+0x80) (asm 0x180029165..0x18002918e).
    const std::uint64_t session_global = slot(kSlotSessionManager);
    if (session_global) {
        s.member_vector_bytes = [session_global]() -> std::size_t {
            const auto mgr = read_at<std::uint64_t>(session_global);
            if (!mgr) return 0;
            const auto begin = read_at<std::uint64_t>(mgr + 0x78), end = read_at<std::uint64_t>(mgr + 0x80);
            return end >= begin ? static_cast<std::size_t>(end - begin) : 0;
        };
    }

    // #10/#11 rows: SoloParamRepository* behind a game global (manager +0x170 holds the global's address, C).
    const std::uint64_t repo_global = slot(kSlotParamRepository);
    if (repo_global) {
        auto repo = [repo_global]() -> const std::byte* {
            return reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(read_at<std::uint64_t>(repo_global)));
        };
        s.row_source = [repo](std::int32_t param, std::int32_t id) -> const std::byte* {
            const std::byte* r = repo();
            return r ? nrsc::params::find_row_in_repository(r, param, id).row : nullptr;
        };
        s.target_source = [repo]() { return list_row_ids(repo(), nrsc::params::kScalingParam, kMaxScalingTargets); };
    }
    s.scaling_wired = helpers && session_global && repo_global;

    // #4: nrsc messages from <dll dir>/locale (key table U: nothing is overridden yet).
    s.messages = load_message_repository(dll_dir / "locale");
}

void wire(const nrsc::signatures::SignatureTable& table, const nrsc::config::Settings& settings,
          const std::filesystem::path& dll_dir) {
    try {
        wire_from_slots([&](unsigned off) { return table.at(off); }, settings, dll_dir);
    } catch (...) {
        // leave whatever was wired so far; the rest stays vanilla
    }
}

}  // namespace nrsc::hooks::params

namespace nrsc::hooks::groups {

std::vector<HookSpec> params(const signatures::SignatureTable& table, const config::Settings& settings) {
    hooks::params::ParamsState& state = hooks::params::reset_params_state(settings);
    return hooks::params::build_specs([&](unsigned off) { return table.at(off); }, state);
}

}  // namespace nrsc::hooks::groups
