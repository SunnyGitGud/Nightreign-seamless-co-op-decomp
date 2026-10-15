// The init thread of the reconstructed nrsc.dll (original: thread proc 0x180001360 -> wrapper 0x1800405c0,
// docs/devirt.md "startup"). Functional reconstruction. The step order below is behaviour (docs/ARCHITECTURE.md):
// settings -> game module -> live ModuleImage -> SignatureTable -> memory block -> hook groups -> session driver ->
// enable inline hooks -> ready flag.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "nrsc/config/settings.hpp"
#include "nrsc/core/fatal_error.hpp"
#include "nrsc/dantelion/block_table_locator.hpp"
#include "nrsc/dantelion/memory.hpp"
#include "nrsc/dll/crash_log.hpp"
#include "nrsc/dll/fatal_box.hpp"
#include "nrsc/dll/init.hpp"
#include "nrsc/dll/live_image.hpp"
#include "nrsc/dll/log.hpp"
#include "nrsc/dll/session_driver.hpp"
#include "nrsc/hooks/groups.hpp"
#include "nrsc/hooks/hook.hpp"
#include "nrsc/hooks/hook_filter.hpp"
#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/hooks/world/wiring.hpp"
#include "nrsc/hooks/world/world_state.hpp"
#include "nrsc/platform/minhook_backend.hpp"
#include "nrsc/signatures/signature_table.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nrsc::dll {
namespace {

constexpr const char* kInitSourceLabel = "nrsc\\dll\\init.cpp";  // source label for fatal errors raised here
constexpr const wchar_t* kDisabledHooksFile = L"nrsc_disable_hooks.txt";

std::atomic<bool> g_ready{false};

// One detour group: its spec builder and the wire() step that fills the state the builder just reset
// (docs/wiring_world.md, wiring_params.md, wiring_net.md). Installed in table order: world, params, net.
struct HookGroup {
    hooks::groups::BuildFn build;
    void (*wire)(const signatures::SignatureTable&, const config::Settings&, const std::filesystem::path& dll_dir);
};

constexpr HookGroup kHookGroups[] = {
    {&hooks::groups::world,
     [](const signatures::SignatureTable& t, const config::Settings& s, const std::filesystem::path&) {
         hooks::world::wire(t, s);
     }},
    {&hooks::groups::params,
     [](const signatures::SignatureTable& t, const config::Settings& s, const std::filesystem::path& dll_dir) {
         hooks::params::wire(t, s, dll_dir);
     }},
    {&hooks::groups::net,
     [](const signatures::SignatureTable& t, const config::Settings& s, const std::filesystem::path&) {
         hooks::net::wire(t, s);
     }},
};

// NETWORK memory block resize (docs/dantelion_memory.md). `table` is null when the signature slot does not resolve, and
// the resize is then a logged no-op.
void resize_network_block(const dantelion::MemoryBlockTable* table) {
    if (!table) {
        log_line("memory block table source unknown (U): NETWORK block resize skipped");
        return;
    }
    dantelion::set_memory_block_size(
        *table, dantelion::kNetworkBlock, dantelion::kNetworkBlockSize,
        [](void* address, std::size_t length, std::uint32_t protection) -> std::uint32_t {
            DWORD old = 0;
            if (!VirtualProtect(address, length, protection, &old)) return 0;
            return old;
        });
}

// Diagnostic file next to the DLL (hooks/hook_filter.hpp). Read with fgets into a 256-byte buffer, as before: a longer
// line arrives as several entries.
std::vector<std::string> read_disabled_hooks(const std::filesystem::path& dll_dir) {
    std::vector<std::string> disabled;
    if (std::FILE* f = _wfopen((dll_dir / kDisabledHooksFile).c_str(), L"r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (auto entry = hooks::parse_disabled_hooks_line(line)) disabled.push_back(std::move(*entry));
        }
        std::fclose(f);
    }
    return disabled;
}

void install_hook_groups(hooks::Installer& installer, const signatures::SignatureTable& table,
                         const config::Settings& settings, const std::filesystem::path& dll_dir,
                         const std::vector<std::string>& disabled) {
    for (const HookGroup& group : kHookGroups) {
        std::vector<hooks::HookSpec> specs = group.build(table, settings);
        group.wire(table, settings, dll_dir);  // before any of the group's hooks is live
        hooks::filter_hooks(specs, disabled, [](const std::string& message) { log_line("%s", message.c_str()); });
        // A FatalError raised inside a detour must not unwind through game frames: report it from where it happens.
        // groups::world() resets the world state, so this is set after it is built and before any of its hooks is live.
        hooks::world::state().on_fatal = [](const std::string& message, const char* file, unsigned line) {
            fatal(message, file, line);
        };
        for (const hooks::HookSpec& spec : specs) {
            installer.install(spec);
            log_line("installed %s", spec.name.c_str());
        }
    }
}

void run_init() {
    wchar_t module_path[MAX_PATH * 2] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), module_path, ARRAYSIZE(module_path));
    const std::filesystem::path dll_dir = std::filesystem::path(module_path).parent_path();
    set_log_directory(dll_dir);
    log_line("init thread started");
    install_crash_log();

    const std::string ini = config::settings_path_for_module(std::filesystem::path(module_path).string());
    const config::Settings settings = config::load_settings(ini);
    log_line("settings loaded from %s", ini.c_str());

    const signatures::ModuleImage image = live_image(GetModuleHandleW(nullptr));
    const signatures::SignatureTable table = signatures::SignatureTable::build(image);
    log_line("signature table built: %u lookups", table.lookups_succeeded());

    resize_network_block(dantelion::find_block_table(image, table));  // signature slot 0x1f0 (docs/wiring_net.md)

    platform::MinHookBackend backend;
    hooks::Installer installer(platform::process_code_memory(), &backend);
    const std::vector<std::string> disabled = read_disabled_hooks(dll_dir);
    install_hook_groups(installer, table, settings, dll_dir, disabled);

    set_lobby_hooks_enabled(std::find(disabled.begin(), disabled.end(), hooks::kNoAutoHostLine) == disabled.end());
    if (auto driver = session_driver_hook()) {  // session layer on the game thread (docs/session.md)
        installer.install(*driver);
        log_line("installed %s", driver->name.c_str());
    }
    installer.enable_inline();
    log_line("%zu hooks installed, inline hooks enabled", installer.installed());
}

unsigned __stdcall thread_proc(void*) {
    try {
        run_init();
    } catch (const FatalError& e) {
        fatal(e.what(), e.source_file(), e.line());
    } catch (const std::exception& e) {
        fatal(e.what(), kInitSourceLabel, 0);
    }
    g_ready.store(true);
    g_ready.notify_all();
    return 0;
}

}  // namespace

void start_and_wait() {
    HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, thread_proc, nullptr, 0, nullptr));
    if (!thread) fatal("failed to start the init thread", kInitSourceLabel, 0);
    CloseHandle(thread);
    g_ready.wait(false);
}

}  // namespace nrsc::dll
