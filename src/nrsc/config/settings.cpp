// Reconstruction of nrsc\mod\preferences\preferences.cpp.
// ev: FUN_180037c50 (ev_a3a8a266709861a9). Line references are to evidence/pseudocode/0x180037c50.c.
// Key and section names are XOR-obfuscated with keys 0x7e0848fd21d1e705 / 0xabcce144cc75dbce.

#include "nrsc/config/settings.hpp"

#include <cmath>
#include <cstring>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::config {
namespace {

constexpr const char* kSourceFile = "nrsc\\mod\\preferences\\preferences.cpp";
constexpr const char* kDefaultExtension = "co2";  // l.1850
constexpr const char* kVanillaExtension = "sl2";  // l.2032

// FUN_1800067d0 returns 100.0 when the key is missing or does not parse.
constexpr double kScalingDefault = 100.0;

}  // namespace

bool ascii_isalnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string settings_path_for_module(std::string_view module_path) {
    // l.225: strrchr(path, '\\'); l.284-300: "\nrsc_settings.ini" from plain immediates
    const auto slash = module_path.rfind('\\');
    if (slash == std::string_view::npos) {
        throw FatalError("Failed to locate settings file", kSourceFile, 9);
    }
    return std::string(module_path.substr(0, slash)) + "\\nrsc_settings.ini";
}

std::string validate_save_file_extension(const std::string& value, const AlnumPredicate& is_alnum) {
    // l.2005-2075
    const std::size_t original_length = value.size();
    std::string ext = value;
    const bool length_ok = original_length >= 1 && original_length <= 0x80;
    const bool is_vanilla = length_ok && original_length <= 3 &&
                            std::memcmp(ext.data(), kVanillaExtension, original_length) == 0;
    if (!length_ok || is_vanilla) {
        ext = kDefaultExtension;
        if (original_length == 0) return ext;
    }
    // Loops to the original length through a bounds-checked index (FUN_18000bba0 ->
    // "invalid string position"), so > 128 characters throws.
    for (std::size_t i = 0; i != original_length; ++i) {
        if (!is_alnum(static_cast<unsigned char>(ext.at(i)))) return kDefaultExtension;
    }
    return ext;
}

float scale_percent(double value) {
    // andpd with 0x7fffffffffffffff (0x18011f250), divsd by 100.0 (0x180145d10), cvtsd2ss
    return static_cast<float>(std::fabs(value) / 100.0);
}

Settings read_settings(const IniReader& ini, const AlnumPredicate& is_alnum) {
    Settings s;
    Preferences& p = s.preferences;

    // [SCALING] l.437-760; stores at +0x30/+0x34/+0x38 (0x18003874b, 0x180038a4e, 0x180038d5a)
    p.health_scaling = scale_percent(ini.get_real("SCALING", "health_scaling", kScalingDefault));
    p.damage_scaling = scale_percent(ini.get_real("SCALING", "damage_scaling", kScalingDefault));
    p.posture_scaling = scale_percent(ini.get_real("SCALING", "posture_scaling", kScalingDefault));

    // [GAMEPLAY] l.785-958: GetBoolean inlined with default true, stored as +0x04 bit 0
    p.allow_invaders = ini.get_boolean("GAMEPLAY", "allow_invaders", true);

    // l.959-1840 set up a session secret from a fixed placeholder; out of scope, not reconstructed.

    // [SAVE] l.1850-2075
    s.save_file_extension =
        validate_save_file_extension(ini.get("SAVE", "save_file_extension", kDefaultExtension), is_alnum);
    return s;
}

Settings load_settings(const std::filesystem::path& ini_path, const AlnumPredicate& is_alnum) {
    const IniReader ini = IniReader::from_file(ini_path);
    if (ini.parse_error() < 0) {
        throw FatalError("Failed to read settings. Unable to find:\n\"" + ini_path.string() + "\"",
                         kSourceFile, 0x16);
    }
    if (ini.parse_error() > 0) {
        throw FatalError("Failed to read settings. Error on line " + std::to_string(ini.parse_error()),
                         kSourceFile, 0x1a);
    }
    return read_settings(ini, is_alnum);
}

}  // namespace nrsc::config
