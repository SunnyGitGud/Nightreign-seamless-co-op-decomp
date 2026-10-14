#include "nrsc/dll/fatal_box.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

#include "nrsc/core/version.hpp"
#include "nrsc/dll/log.hpp"

namespace nrsc::dll {

void fatal(const std::string& message, const std::string& file, unsigned line) {
    log_line("FATAL: %s (%s:%u)", message.c_str(), file.c_str(), line);
    char text[1024];
    std::snprintf(text, sizeof text, "A fatal error has occurred and the process has been aborted:\n\n%s\n%s\n%u",
                  message.c_str(), file.c_str(), line);
    MessageBoxA(nullptr, text, kFatalBoxTitle, MB_OK | MB_ICONERROR);
    TerminateProcess(GetCurrentProcess(), 1);
    for (;;) Sleep(1000);  // TerminateProcess does not return for the current process; never fall through
}

}  // namespace nrsc::dll
