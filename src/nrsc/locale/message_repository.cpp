// nrsc.dll localisation. Evidence (all in nrsc.dll):
//   FUN_18004cbb0 loader (ev_6ef414403f22f36a), catch funclet FUN_180050b40 (ev_12c4f88e7b84cf46)
//   FUN_18000a1c0 lookup (ev_6301c32ee0f3094100), catch funclet FUN_18000a7d0 (ev_d97e50a4d5128bd0)
//   FUN_18001e1c0 message override (ev_8d839f541f8f3bed)
//   FUN_18000a920 UTF-8 -> UTF-16 (MultiByteToWideChar(0xfde9, 0, ...))
// Obfuscated messages were decoded with tools/decode_sse_strings.py; see docs/localisation.md.

#include "nrsc/locale/message_repository.hpp"

#include <format>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

namespace nrsc::locale {
namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// --- Locale ------------------------------------------------------------------------------------

Locale::Locale(std::string name, std::shared_ptr<const Locale> fallback)
    : name_(std::move(name)), fallback_(std::move(fallback)) {}

void Locale::load_json(std::string_view json_text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(e.what());
    }
    if (!root.is_object()) {
        throw std::runtime_error(std::format("Expected JSON object in translation file for locale \"{}\", got {}",
                                             name_, root.type_name()));
    }
    for (const auto& [key, value] : root.items()) {
        if (!value.is_string()) {
            throw std::runtime_error(
                std::string("[json.exception.type_error.302] type must be string, but is ") + value.type_name());
        }
        entries_.insert_or_assign(key, value.get<std::string>());
    }
}

const std::string* Locale::find(const std::string& key) const {
    for (const Locale* l = this; l != nullptr; l = l->fallback_.get()) {
        if (const auto it = l->entries_.find(key); it != l->entries_.end()) return &it->second;
    }
    return nullptr;
}

std::shared_ptr<const Locale> load_locale(const std::filesystem::path& locale_dir, const std::string& name,
                                          std::shared_ptr<const Locale> fallback, const WarningSink& warn) {
    auto locale = std::make_shared<Locale>(name, fallback);
    const auto path = locale_dir / (name + ".json");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        if (!fallback) {
            // FUN_18004cbb0 0x18004f74d..0x18004fad8 -> FUN_180052d00 (Critical Error)
            throw CriticalError(std::format(
                "Locale path \"{}\" not found which has no fallback! Please make sure to follow the installation "
                "instructions, make sure you have the correct files installed, and then try again.",
                path.string()));
        }
        if (warn) {
            warn(std::format("Transation file for locale \"{}\" not found; will use fallback \"{}\"", name,
                             fallback->name()));
        }
        return locale;
    }
    try {
        locale->load_json(read_file(path));
    } catch (const std::exception& e) {  // FUN_180050b40
        if (!fallback) {
            throw CriticalError(std::format(
                "Failed to parse translation file for locale \"{}\" which has no fallback! Error: {}", name, e.what()));
        }
        if (warn) {
            warn(std::format("Failed to parse translation file for locale \"{}\"; will use fallback \"{}\". Error: {}",
                             name, fallback->name(), e.what()));
        }
    }
    return locale;
}

// --- UTF-8 -> UTF-16 -----------------------------------------------------------------------------

std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    std::size_t i = 0;
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    while (i < s.size()) {
        const unsigned char b0 = byte(i);
        char32_t cp;
        std::size_t len;
        if (b0 < 0x80) { cp = b0; len = 1; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { cp = b0 & 0x1F; len = 2; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { cp = b0 & 0x0F; len = 3; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { cp = b0 & 0x07; len = 4; }
        else { out.push_back(u'�'); ++i; continue; }
        std::size_t k = 1;
        for (; k < len && i + k < s.size(); ++k) {
            const unsigned char b = byte(i + k);
            if ((b & 0xC0) != 0x80) break;
            cp = (cp << 6) | (b & 0x3F);
        }
        const bool overlong = (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
        if (k != len || overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(u'�');
            i += k;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += len;
    }
    return out;
}

// --- MessageRepository ---------------------------------------------------------------------------

MessageRepository::MessageRepository(std::shared_ptr<const Locale> locale, std::vector<MessageKey> keys,
                                     std::vector<FixedText> fixed)
    : locale_(std::move(locale)) {
    for (auto& k : keys) keys_[k.category].emplace(k.id, std::move(k.key));  // map insert: first wins
    for (auto& f : fixed) fixed_[f.category].emplace(f.id, std::move(f.text));
}

template <class... Args>
std::optional<std::u16string> MessageRepository::resolve(std::int32_t category, std::int32_t id,
                                                         const Args&... args) const {
    if (!locale_) return std::u16string(kStringNotFound);
    if (const auto c = fixed_.find(category); c != fixed_.end()) {
        if (const auto m = c->second.find(id); m != c->second.end()) return m->second;
    }
    const auto c = keys_.find(category);
    if (c == keys_.end()) return std::nullopt;
    const auto m = c->second.find(id);
    if (m == c->second.end()) return std::nullopt;

    const std::string& key = m->second;
    const std::string* text = locale_->find(key);
    if (text == nullptr) return utf8_to_utf16(std::format("KEY \"{}\" NOT FOUND", key));
    try {
        return utf8_to_utf16(std::vformat(*text, std::make_format_args(args...)));
    } catch (const std::exception& e) {  // FUN_18000a7d0: first argument is the key (parent local_88)
        return utf8_to_utf16(std::format("FORMAT ERROR FOR \"{}\": {}", key, e.what()));
    }
}

std::optional<std::u16string> MessageRepository::lookup(std::int32_t category, std::int32_t id) const {
    return resolve(category, id);
}

std::u16string MessageRepository::override_lookup(std::int32_t category, std::int32_t id, std::int32_t special_value,
                                                  const std::function<std::u16string()>& game_lookup) const {
    if (id == kSpecialMessageId) {
        if (!locale_) return std::u16string(kStringNotFound);
        // Category 200 is used whatever category the game asked for; the value is passed twice.
        const bool known = (fixed_.contains(kSpecialMessageCategory) &&
                            fixed_.at(kSpecialMessageCategory).contains(id)) ||
                           (keys_.contains(kSpecialMessageCategory) && keys_.at(kSpecialMessageCategory).contains(id));
        if (known) {
            if (auto text = resolve(kSpecialMessageCategory, id, special_value, special_value)) return *text;
        }
    }
    if (auto text = resolve(category, id)) return *text;
    return game_lookup();
}

}  // namespace nrsc::locale
