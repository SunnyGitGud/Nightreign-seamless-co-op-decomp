#include "nrsc/config/ini_reader.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>

#include "nrsc/config/ini_parser.hpp"

namespace nrsc::config {
namespace {

// MSVC tolower() in the default "C" locale (FUN_180104878): only 'A'-'Z' change; bytes >= 0x80 arrive
// sign-extended, fail the `c - 'A' <= 25` test and are returned unchanged.
char ascii_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = ascii_tolower(c);
    return s;
}

// strtol() as on Windows, where long is 32 bits: out-of-range input saturates at LONG_MIN/LONG_MAX.
bool msvc_strtol(const std::string& s, std::int32_t& out) {
    const char* begin = s.c_str();
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(begin, &end, 0);
    if (end == begin) return false;
    if (v > INT32_MAX) out = INT32_MAX;
    else if (v < INT32_MIN) out = INT32_MIN;
    else out = static_cast<std::int32_t>(v);
    return true;
}

}  // namespace

std::string IniReader::make_key(std::string_view section, std::string_view name) {
    std::string key;
    key.reserve(section.size() + 1 + name.size());
    key.append(section).append("=").append(name);
    return to_lower(std::move(key));
}

bool IniReader::handle(std::string_view section, std::string_view name, std::string_view value) {
    std::string& slot = values_[make_key(section, name)];
    if (!slot.empty()) slot += '\n';  // repeated key: values are joined with a newline
    slot.append(value);
    return true;  // the original handler always returns 1
}

IniReader IniReader::from_text(std::string_view file_bytes) {
    IniReader r;
    r.error_ = ini_parse_text(file_bytes, [&r](auto s, auto n, auto v) { return r.handle(s, n, v); });
    return r;
}

IniReader IniReader::from_file(const std::filesystem::path& path) {
    IniReader r;
    r.error_ = ini_parse_file(path, [&r](auto s, auto n, auto v) { return r.handle(s, n, v); });
    return r;
}

std::string IniReader::get(std::string_view section, std::string_view name,
                           const std::string& default_value) const {
    const auto it = values_.find(make_key(section, name));
    return it == values_.end() ? default_value : it->second;
}

std::int32_t IniReader::get_integer(std::string_view section, std::string_view name,
                                    std::int32_t default_value) const {
    std::int32_t v;
    return msvc_strtol(get(section, name, ""), v) ? v : default_value;
}

double IniReader::get_real(std::string_view section, std::string_view name,
                           double default_value) const {
    const std::string s = get(section, name, "");
    const char* begin = s.c_str();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    return end > begin ? v : default_value;
}

bool IniReader::get_boolean(std::string_view section, std::string_view name,
                            bool default_value) const {
    const std::string v = to_lower(get(section, name, ""));
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return default_value;
}

}  // namespace nrsc::config
