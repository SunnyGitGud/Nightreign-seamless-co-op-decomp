#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace nrsc::config {

// Receives each name/value pair. Returning false marks the line as an error (inih semantics).
using IniHandler =
    std::function<bool(std::string_view section, std::string_view name, std::string_view value)>;

// Parser limits observed in the embedded inih copy inside FUN_180037c50 (nrsc.dll).
inline constexpr std::size_t kMaxLine = 200;    // fgets(buf, 200, f)
inline constexpr std::size_t kMaxSection = 50;  // strncpy(section, ..., 0x32) + terminator at [49]
inline constexpr std::size_t kMaxName = 50;     // strncpy(prev_name, ..., 0x32) + terminator at [49]

// Parses INI text exactly as the embedded inih does after the CRT has opened the file in text mode.
// Returns 0 on success or the 1-based number of the first line that failed. Parsing continues after
// an error, as in the original.
int ini_parse_text(std::string_view file_bytes, const IniHandler& handler);

// Opens the file and parses it. Returns -1 if it cannot be opened, as the original does.
int ini_parse_file(const std::filesystem::path& path, const IniHandler& handler);

}  // namespace nrsc::config
