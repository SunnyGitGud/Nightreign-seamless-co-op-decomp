// The init thread of the reconstructed nrsc.dll (original: thread proc 0x180001360 -> wrapper 0x1800405c0,
// docs/devirt.md "startup"). Functional reconstruction: settings -> game module -> live ModuleImage ->
// SignatureTable -> memory block -> hook groups -> enable inline hooks -> ready flag.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "nrsc/config/settings.hpp"
#include "nrsc/core/fatal_error.hpp"
#include "nrsc/dantelion/memory.hpp"
#include "nrsc/dll/init.hpp"
#include "nrsc/dll/crash_log.hpp"
#include "nrsc/dll/session_driver.hpp"
#include "nrsc/hooks/groups.hpp"
#include "nrsc/hooks/hook.hpp"
#include "nrsc/hooks/world/world_state.hpp"
#include "nrsc/hooks/world/wiring.hpp"
#include "nrsc/hooks/params/params_group.hpp"
#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/dantelion/block_table_locator.hpp"
#include "nrsc/platform/minhook_backend.hpp"
#include "nrsc/signatures/module_image.hpp"
#include "nrsc/signatures/signature_table.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nrsc::dll {
namespace {

std::atomic<bool> g_ready{false};
std::filesystem::path g_dll_dir;

}  // namespace

void log_line(const char* fmt, ...) {
    if (g_dll_dir.empty()) return;
    std::filesystem::path path = g_dll_dir / "nrsc_reconstruction.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fclose(f);
}

namespace {

// Original fatal box: text "A fatal error has occurred and the process has been aborted:\n\n%s\n%s\n%u",
// title "Seamless Coop 1.1.3 - Fatal Error"; then the process is terminated.
[[noreturn]] void fatal(const std::string& message, const std::string& file, unsigned line) {
    log_line("FATAL: %s (%s:%u)", message.c_str(), file.c_str(), line);
    char text[1024];
    std::snprintf(text, sizeof text, "A fatal error has occurred and the process has been aborted:\n\n%s\n%s\n%u",
                  message.c_str(), file.c_str(), line);
    MessageBoxA(nullptr, text, "Seamless Coop 1.1.3 - Fatal Error", MB_OK | MB_ICONERROR);
    TerminateProcess(GetCurrentProcess(), 1);
    for (;;) Sleep(1000);
}

signatures::ModuleImage live_image(HMODULE game) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(game);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const std::size_t size = nt->OptionalHeader.SizeOfImage;
    std::vector<signatures::ImageSection> sections;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        signatures::ImageSection s;
        s.name.assign(reinterpret_cast<const char*>(sec[i].Name), strnlen(reinterpret_cast<const char*>(sec[i].Name), 8));
        s.rva = sec[i].VirtualAddress;
        s.virtual_size = sec[i].Misc.VirtualSize;
        s.raw_size = sec[i].SizeOfRawData;
        s.characteristics = sec[i].Characteristics;
        sections.push_back(std::move(s));
    }
    // Copy page by page so any decommitted page reads as zero instead of faulting.
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t off = 0; off < size;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(base + off, &mbi, sizeof mbi)) break;
        const std::size_t region = std::min<std::size_t>(mbi.RegionSize - ((base + off) - static_cast<const std::uint8_t*>(mbi.BaseAddress)), size - off);
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Protect != 0)
            std::memcpy(bytes.data() + off, base + off, region);
        off += region;
    }
    return signatures::ModuleImage(reinterpret_cast<std::uint64_t>(base), std::move(bytes), std::move(sections));
}

// NETWORK memory block resize (docs/dantelion_memory.md). The engine component table is reached through
// *(installer_ctx + 0xb8) in the original, a context filled by protected code; nothing in the signature table
// points at it (U). Until its source is recovered this is a documented no-op unless `table` is supplied.
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

void run_init() {
    wchar_t module_path[MAX_PATH * 2] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), module_path, ARRAYSIZE(module_path));
    g_dll_dir = std::filesystem::path(module_path).parent_path();
    log_line("init thread started");
    install_crash_log();

    const std::string ini = config::settings_path_for_module(std::filesystem::path(module_path).string());
    const config::Settings settings = config::load_settings(ini);
    log_line("settings loaded from %s", ini.c_str());

    HMODULE game = GetModuleHandleW(nullptr);
    const signatures::ModuleImage image = live_image(game);
    const signatures::SignatureTable table = signatures::SignatureTable::build(image);
    log_line("signature table built: %u lookups", table.lookups_succeeded());

    resize_network_block(dantelion::find_block_table(image, table));  // signature slot 0x1f0 (docs/wiring_net.md)

    platform::MinHookBackend backend;
    hooks::Installer installer(platform::process_code_memory(), &backend);
    std::vector<std::string> disabled;
    if (std::FILE* f = _wfopen((g_dll_dir / "nrsc_disable_hooks.txt").c_str(), L"r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            std::string v(line);
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
            if (!v.empty() && v[0] != '#') disabled.push_back(v);
        }
        std::fclose(f);
    }
    // Each group's wire() fills the state its builder just reset (docs/wiring_*.md), before any of its hooks is live.
    const std::filesystem::path dll_dir = g_dll_dir;
    auto wire = [&](auto build) {
        if (build == &hooks::groups::world) hooks::world::wire(table, settings);
        else if (build == &hooks::groups::params) hooks::params::wire(table, settings, dll_dir);
        else hooks::net::wire(table, settings);
    };
    for (auto build : {&hooks::groups::world, &hooks::groups::params, &hooks::groups::net}) {
        std::vector<hooks::HookSpec> specs = build(table, settings);
        wire(build);
        // #7 replaces the game's scaling function outright and #10 builds its rows. Their target-id list (80 ids, filled by
        // protected code) is not recovered; the stand-in could mis-scale enemies, so both stay out and the game's own
        // scaling runs (docs/wiring_params.md).
        // Diagnostic (not in the original): nrsc_disable_hooks.txt next to the DLL lists hook-name prefixes to skip, one per line.
        std::erase_if(specs, [&](const hooks::HookSpec& h) {
            for (const std::string& prefix : disabled) {
                if (!prefix.empty() && h.name.rfind(prefix, 0) == 0) {
                    log_line("disabled %s (nrsc_disable_hooks.txt)", h.name.c_str());
                    return true;
                }
            }
            return false;
        });
        std::erase_if(specs, [](const hooks::HookSpec& h) {
            const bool scaling = h.name.rfind("hook#7", 0) == 0 || h.name.rfind("hook#10", 0) == 0;
            // P12 (6-slot bloodstain block) crashes a new game (2026-10-15 test, bisected): game code at 0x14072ad50 reads the
            // block with the vanilla 0x68-byte layout (effect handle at +0x58). The original compensates elsewhere (not
            // recovered), so the vanilla 3-slot block stays until that is understood.
            const bool p12 = h.name.rfind("P12", 0) == 0;
            if (scaling) log_line("held back %s (scaling target ids not recovered)", h.name.c_str());
            if (p12) log_line("held back %s (crashes a new game with the 6-slot layout; see docs/plan_gaps.md)", h.name.c_str());
            return scaling || p12;
        });
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
    set_auto_host_enabled(std::find(disabled.begin(), disabled.end(), "no-auto-host") == disabled.end());
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
        fatal(e.what(), "nrsc\\dll\\init.cpp", 0);
    }
    g_ready.store(true);
    g_ready.notify_all();
    return 0;
}

}  // namespace

void start_and_wait() {
    HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, thread_proc, nullptr, 0, nullptr));
    if (!thread) fatal("failed to start the init thread", "nrsc\\dll\\init.cpp", 0);
    CloseHandle(thread);
    g_ready.wait(false);
}

}  // namespace nrsc::dll
