#pragma once
#include <string>

namespace nrsc::dll {

// The original's fatal path (FUN_180029f40): logs "FATAL: ...", shows the message box
// "A fatal error has occurred and the process has been aborted:\n\n<message>\n<file>\n<line>" titled kFatalBoxTitle,
// then terminates the process. `file` and `line` are the original's source labels carried by nrsc::FatalError.
[[noreturn]] void fatal(const std::string& message, const std::string& file, unsigned line);

}  // namespace nrsc::dll
