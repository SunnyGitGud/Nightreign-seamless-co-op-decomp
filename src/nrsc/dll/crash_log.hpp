#pragma once
namespace nrsc::dll {
// Diagnostic vectored exception logger (src/nrsc/dll/crash_log.cpp). Call once the log directory is known.
void install_crash_log();
}  // namespace nrsc::dll
