#pragma once
// nrsc_reconstruction.log next to the DLL (diagnostic; the original has no such log). dist/TESTING.md tells testers which
// lines to look for, so existing message texts are part of the interface.
#include <filesystem>

namespace nrsc::dll {

// Sets the directory of the log file; until then log_line() is a no-op. Called once, first thing on the init thread.
void set_log_directory(const std::filesystem::path& dll_dir);

// Appends "[hh:mm:ss.mmm] <formatted text>\n" (printf format). Opens and closes the file per line so nothing is lost on
// a crash.
void log_line(const char* fmt, ...);

}  // namespace nrsc::dll
