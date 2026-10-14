// Reconstruction of the inih ini_parse_stream copy that nrsc.dll's FUN_180037c50 inlines.
// ev: ev_a3a8a266709861a9 (evidence/pseudocode/0x180037c50.c l.342-430, 2190-2235)
// Behaviour matches public inih r5x compiled with INI_ALLOW_MULTILINE=1, INI_ALLOW_BOM=1,
// INI_ALLOW_INLINE_COMMENTS=1 (prefix ";"), INI_START_COMMENT_PREFIXES=";#", INI_STOP_ON_FIRST_ERROR=0,
// INI_MAX_LINE=200 and INI_CALL_HANDLER_ON_NEW_SECTION=0.

#include "nrsc/config/ini_parser.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace nrsc::config {
namespace {

// MSVC isspace() in the "C" locale. The original passes the byte zero-extended, so bytes >= 0x80
// are never whitespace.
bool c_isspace(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// Emulates fopen(path, "r") on the MSVC CRT: CRLF becomes LF and Ctrl-Z (0x1A) ends the stream.
// (Strongly inferred from the "r" mode string; CRT behaviour, not code in the DLL.)
std::string apply_text_mode(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\x1a') break;
        if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;
        out.push_back(c);
    }
    return out;
}

// fgets(buf, kMaxLine, f): at most kMaxLine-1 bytes, stopping after a newline.
std::vector<std::string> fgets_chunks(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t n = 0;
        while (pos + n < text.size() && n < kMaxLine - 1) {
            ++n;
            if (text[pos + n - 1] == '\n') break;
        }
        lines.emplace_back(text.substr(pos, n));
        pos += n;
    }
    return lines;
}

// inih rstrip(): trims trailing whitespace in place.
void rstrip(char* s) {
    char* p = s + std::strlen(s);
    while (p > s && c_isspace(static_cast<unsigned char>(*--p))) *p = '\0';
}

// inih lskip()
char* lskip(char* s) {
    while (*s && c_isspace(static_cast<unsigned char>(*s))) ++s;
    return s;
}

// inih find_chars_or_comment(): first char from `chars`, or a ';' that follows whitespace, or the end.
char* find_chars_or_comment(char* s, const char* chars) {
    bool was_space = false;
    while (*s && (!chars || !std::strchr(chars, *s)) && !(was_space && *s == ';')) {
        was_space = c_isspace(static_cast<unsigned char>(*s));
        ++s;
    }
    return s;
}

// inih strncpy0(): copies at most size-1 characters and always terminates.
void strncpy0(char* dest, const char* src, std::size_t size) {
    std::strncpy(dest, src, size);
    dest[size - 1] = '\0';
}

}  // namespace

int ini_parse_text(std::string_view file_bytes, const IniHandler& handler) {
    char section[kMaxSection] = "";
    char prev_name[kMaxName] = "";
    int lineno = 0;
    int error = 0;

    for (const std::string& chunk : fgets_chunks(apply_text_mode(file_bytes))) {
        char line[kMaxLine] = {};
        std::memcpy(line, chunk.data(), chunk.size());
        ++lineno;

        char* start = line;
        if (lineno == 1 && static_cast<unsigned char>(start[0]) == 0xEF &&
            static_cast<unsigned char>(start[1]) == 0xBB &&
            static_cast<unsigned char>(start[2]) == 0xBF) {
            start += 3;
        }
        rstrip(start);
        start = lskip(start);

        if (*start == ';' || *start == '#') {
            continue;  // whole-line comment
        }
        if (*prev_name && *start && start > line) {
            // Indented non-empty line: continuation of the previous value. No inline-comment stripping.
            if (!handler(section, prev_name, start) && !error) error = lineno;
            continue;
        }
        if (*start == '[') {
            char* end = find_chars_or_comment(start + 1, "]");
            if (*end == ']') {
                *end = '\0';
                strncpy0(section, start + 1, sizeof(section));
                *prev_name = '\0';
            } else if (!error) {
                error = lineno;  // "[section" without "]"
            }
            continue;
        }
        if (*start) {
            char* end = find_chars_or_comment(start, "=:");
            if (*end == '=' || *end == ':') {
                *end = '\0';
                char* name = start;
                rstrip(name);
                char* value = end + 1;
                end = find_chars_or_comment(value, nullptr);
                if (*end) *end = '\0';  // inline comment
                value = lskip(value);
                rstrip(value);

                strncpy0(prev_name, name, sizeof(prev_name));
                if (!handler(section, name, value) && !error) error = lineno;
            } else if (!error) {
                error = lineno;  // no '=' or ':'
            }
        }
    }
    return error;
}

int ini_parse_file(const std::filesystem::path& path, const IniHandler& handler) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ini_parse_text(bytes, handler);
}

}  // namespace nrsc::config
