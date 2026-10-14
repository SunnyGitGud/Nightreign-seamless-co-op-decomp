#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace nrsc::config {

// Equivalent of the INIReader class embedded in nrsc.dll.
// ev (nrsc.dll): FUN_180005960 ValueHandler (newline join at 0x180005be9), FUN_180005d80 MakeKey
//     ('=' at 0x180005e58, tolower FUN_180104878), FUN_1800064a0 Get, FUN_1800067d0 GetReal with a fixed
//     default of 100.0 (ev_84a4b5a7538a663e). GetBoolean is inlined into the loader (FUN_180037c50 l.874-960).
//     get_integer is kept for completeness; nrsc.dll does not read any integer key.
class IniReader {
public:
    static IniReader from_text(std::string_view file_bytes);
    static IniReader from_file(const std::filesystem::path& path);

    // 0 = parsed cleanly, -1 = file could not be opened, N > 0 = first line with an error.
    int parse_error() const { return error_; }

    std::string get(std::string_view section, std::string_view name,
                    const std::string& default_value) const;
    // strtol(value, &end, 0) with MSVC's 32-bit long; returns default_value if nothing was converted.
    std::int32_t get_integer(std::string_view section, std::string_view name,
                             std::int32_t default_value) const;
    // strtod(value, &end); returns default_value if nothing was converted.
    double get_real(std::string_view section, std::string_view name, double default_value) const;
    // "true"/"yes"/"on"/"1" and "false"/"no"/"off"/"0", case-insensitive; anything else -> default.
    bool get_boolean(std::string_view section, std::string_view name, bool default_value) const;

    // lower(section + "=" + name)
    static std::string make_key(std::string_view section, std::string_view name);

private:
    bool handle(std::string_view section, std::string_view name, std::string_view value);

    int error_ = 0;
    std::map<std::string, std::string> values_;
};

}  // namespace nrsc::config
