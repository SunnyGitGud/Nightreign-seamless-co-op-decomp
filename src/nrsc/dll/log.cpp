#include "nrsc/dll/log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace nrsc::dll {

namespace {
std::filesystem::path g_log_dir;
}  // namespace

void set_log_directory(const std::filesystem::path& dll_dir) { g_log_dir = dll_dir; }

void log_line(const char* fmt, ...) {
    if (g_log_dir.empty()) return;
    std::filesystem::path path = g_log_dir / "nrsc_reconstruction.log";
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

}  // namespace nrsc::dll
