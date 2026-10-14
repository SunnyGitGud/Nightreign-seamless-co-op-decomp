#pragma once
// Version identity of the mod this reconstruction matches (nrsc.dll 1.1.3, sha256 243eec92…ce24e).
#include <string_view>

namespace nrsc {

// Compared byte for byte with the peer's lobby data (session/join_flow: a mismatch is WrongVersion), so it must stay the
// original's string. C (.rdata 0x18014d82a).
inline constexpr std::string_view kModVersion = "1.1.3";

// Title of the fatal-error message box: the original formats "Seamless Coop %s - Fatal Error" with kModVersion
// (FUN_18002a020, docs/baseline.md).
inline constexpr const char* kFatalBoxTitle = "Seamless Coop 1.1.3 - Fatal Error";

}  // namespace nrsc
