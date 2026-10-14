#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

// nrsc\cs\auto_control_api\auto_control_api.cpp: FUN_180028c20
// (ev_6539e76aa5b08c41bffaab697186b75175147ef798e46c093b4c0de2dbcd63e1). This is a hook detour that runs
// before the original. It (1) makes sure four task objects (+0x2f8..+0x310) are registered, (2) once the
// game is up, calls the game's AutoControl method "SignIn"(0) by name, (3) may show a warning, then
// (4) tail-calls the original. The hook target is unknown. See docs/world_chr_man.md §4.
namespace nrsc::cs {

// XOR-obfuscated in the binary: 0x96ac6f1c75e80e56 ^ 0x96ac01551b8f6705 = "SignIn".
inline constexpr std::string_view kSignInMethod = "SignIn";

inline constexpr std::u16string_view kUnsupportedCopyWarning =
    u"~ You may be running a pirated or cracked version of the game. Please be aware that Seamless Co-op only "
    u"support up-to-date, legitimate, Steam copies of ELDEN RING: NIGHTREIGN. You may experience bugs and "
    u"issues. ~";

// The game's name -> method table: an MSVC unordered_map<std::string, …> (FNV-1a over the bytes).
// The method is called with 0.
using AutoControlMethods = std::unordered_map<std::string, std::function<void(int)>>;

// Formatted into a 0x100-byte buffer (FUN_180009c20 -> CRT vsprintf, 0x100).
std::string method_unavailable_message(std::string_view method);

// Look up and call `method`(0). A missing method is fatal:
// FatalError(method_unavailable_message(method), auto_control_api.cpp, 0x14).
void invoke_method(const AutoControlMethods& methods, std::string_view method);

// The gate for step (2): the game object exists and its +0x174e8 is non-null, the sign-in object exists,
// and its +0x58 is 0. When true, the detour first calls hooks+0x20 -> [3](1), then invokes "SignIn".
bool should_sign_in(bool game_present, bool game_field_174e8_set, bool sign_in_present, int sign_in_state);

}  // namespace nrsc::cs
