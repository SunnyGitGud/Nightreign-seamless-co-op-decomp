#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "nrsc/config/ini_reader.hpp"

namespace nrsc::config {

// The preferences block FUN_180037c50 fills (param_3). Offsets are those the loader writes.
struct Preferences {
    bool allow_invaders = true;   // +0x04 bit 0
    float health_scaling = 1.0f;  // +0x30
    float damage_scaling = 1.0f;  // +0x34
    float posture_scaling = 1.0f; // +0x38
};

struct Settings {
    Preferences preferences;
    // Validated extension. The original widens it and appends it to a std::wstring (param_4).
    std::string save_file_extension;
};

// Character test for save_file_extension: a user32 import with a 18-19 character name, most likely
// IsCharAlphaNumericA. The default is the ASCII answer.
using AlnumPredicate = std::function<bool(unsigned char)>;
bool ascii_isalnum(unsigned char c);

// "<directory of module_path>\nrsc_settings.ini" (only '\' is a separator).
// Throws FatalError("Failed to locate settings file", ..., 9).
std::string settings_path_for_module(std::string_view module_path);

// Same rules as ersc.dll v2.0.1 (identical code shape): empty or > 128 chars -> "co2"; any prefix of
// "sl2" -> "co2"; a non-alphanumeric character -> "co2"; > 128 chars throws std::out_of_range.
std::string validate_save_file_extension(const std::string& value,
                                         const AlnumPredicate& is_alnum = ascii_isalnum);

// [SCALING] value -> stored factor: (float)(|value| / 100.0). Unlike ersc.dll, negatives are mirrored,
// not clamped to 0.
float scale_percent(double value);

Settings read_settings(const IniReader& ini, const AlnumPredicate& is_alnum = ascii_isalnum);

// Parse, fail fatally on a missing file (line 0x16) or a parse error (line 0x1a), then read.
Settings load_settings(const std::filesystem::path& ini_path, const AlnumPredicate& is_alnum = ascii_isalnum);

}  // namespace nrsc::config
