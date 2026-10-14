// Diagnostic only (not part of the original): logs fatal-looking exceptions to nrsc_reconstruction.log so crashes in a
// test run can be located. A first-chance vectored handler that never handles anything (always CONTINUE_SEARCH); it logs
// at most kMaxReports distinct faults: the code, the faulting address as module+offset, the argument registers and the
// return addresses on the stack that point into nightreign.exe or this DLL. Addresses inside nrsc.dll map to functions
// through build-win/nrsc.map.
#include "nrsc/dll/crash_log.hpp"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "nrsc/dll/init.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nrsc::dll {
namespace {

constexpr int kMaxReports = 8;
std::atomic<int> g_reports{0};

bool is_fatal_code(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN (fast fail)
    case 0xE06D7363:  // C++ exception (logged so an escaping FatalError shows up)
        return true;
    default:
        return false;
    }
}

// "nightreign.exe+0x1234" / "nrsc.dll+0x..." / "other.dll+0x..." / "0x..." for an address.
void describe(std::uintptr_t a, char* out, std::size_t n) {
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(a), &m) && m) {
        char name[MAX_PATH] = {};
        GetModuleBaseNameA(GetCurrentProcess(), m, name, sizeof name);
        std::snprintf(out, n, "%s+0x%llx", name, (unsigned long long)(a - reinterpret_cast<std::uintptr_t>(m)));
    } else {
        std::snprintf(out, n, "0x%llx", (unsigned long long)a);
    }
}

bool in_interesting_module(std::uintptr_t a) {
    const auto game = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto self = reinterpret_cast<std::uintptr_t>(&__ImageBase);
    MODULEINFO gi{}, si{};
    GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(game), &gi, sizeof gi);
    GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(self), &si, sizeof si);
    return (a >= game && a < game + gi.SizeOfImage) || (a >= self && a < self + si.SizeOfImage);
}

LONG CALLBACK handler(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!is_fatal_code(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (g_reports.fetch_add(1) >= kMaxReports) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT& c = *ep->ContextRecord;
    char where[160];
    describe(c.Rip, where, sizeof where);
    log_line("EXCEPTION 0x%08lx at %s (rip 0x%llx) info[0]=0x%llx info[1]=0x%llx", code, where,
             (unsigned long long)c.Rip,
             (unsigned long long)(ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0] : 0),
             (unsigned long long)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0));
    log_line("  rax=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx rsp=%llx", (unsigned long long)c.Rax, (unsigned long long)c.Rcx,
             (unsigned long long)c.Rdx, (unsigned long long)c.R8, (unsigned long long)c.R9, (unsigned long long)c.Rsp);
    // Return addresses on the stack into the game or this DLL (heuristic scan of 512 slots).
    const auto* sp = reinterpret_cast<const std::uintptr_t*>(c.Rsp);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 16; ++i) {
        std::uintptr_t v;
        __try {
            v = sp[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (!in_interesting_module(v)) continue;
        describe(v, where, sizeof where);
        log_line("  stack[%d] %s", i, where);
        ++shown;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install_crash_log() {
    AddVectoredExceptionHandler(1, handler);
    log_line("crash logger installed");
}

}  // namespace nrsc::dll
