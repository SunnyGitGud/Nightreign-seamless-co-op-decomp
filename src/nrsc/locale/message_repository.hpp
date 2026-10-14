#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// nrsc\cs\game_message_repository/../../mod/message_repository/locale.h (nrsc.dll).
// What is readable: the translation-file loader with its error handling, the lookup, the message
// override and UTF-8 -> UTF-16. What is not: the (category, id) -> key table, which fixed texts sit at which
// (category, id), and how the language is chosen. Those are only in protected code, so the repository
// takes them as inputs. See docs/localisation.md.
namespace nrsc::locale {

// The keys present in nrsc.dll's string table (.rdata 0x18014cd37..0x18014d055). None of them is
// referenced from readable code, so their message ids are unknown.
inline constexpr std::string_view kKnownKeys[] = {
    "MODMESSAGE_YUINEXUSNOSEAMLESS",       "MODMESSAGE_YUINEXUSWRONGSSR",
    "MODMESSAGE_YUINEXUSBEGINTWOPLAYER",   "MODMESSAGE_YUINEXUSBEGINONEPLAYER",
    "MODMESSAGE_YUINEXUSBEGINTHREEPLAYER", "MODMESSAGE_YUINEXUSPLAYERCOUNTREMAINING",
    "MODMESSAGE_YUINEXUSPAUSED",           "MODMESSAGE_YUINEXUSRESUMED",
};

// UTF-16 literals compiled into nrsc.dll (.rdata 0x18014e056..). Their (category, id) is unknown.
inline constexpr std::u16string_view kBannerText = u"Elden Ring: Nightreign Seamless Co-op v1.1.3 by Yui";
inline constexpr std::u16string_view kWelcomeText =
    u"Welcome to the seamless co-op mod for Elden Ring: Nightreign!\n[ This is a free mod ]\n"
    u"If you have paid for any launchers or packages with this mod then you've been scammed";
inline constexpr std::u16string_view kSeparateSaveText =
    u"This mod uses a separate save, and does not connect to the matchmaking servers.\n"
    u"It does not contain an anticheat.";
inline constexpr std::u16string_view kStringNotFound = u"[string not found]";

// The id the message override treats specially (FUN_18001e1c0): looked up in category 200 and
// formatted with two integers.
inline constexpr std::int32_t kSpecialMessageId = 0x52409;
inline constexpr std::int32_t kSpecialMessageCategory = 200;

// Raised where the original shows a "Critical Error" message box (MB_ICONERROR) and then calls its fatal
// handler with locale.h line 0x24.
class CriticalError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One translation file plus its fallback.
class Locale {
public:
    Locale(std::string name, std::shared_ptr<const Locale> fallback);

    // Parses the JSON (root must be an object, values strings). Throws std::runtime_error on failure;
    // entries added before the failure are kept.
    void load_json(std::string_view json_text);

    const std::string& name() const { return name_; }
    const std::shared_ptr<const Locale>& fallback() const { return fallback_; }
    std::size_t size() const { return entries_.size(); }
    const std::string* find(const std::string& key) const;

private:
    std::string name_;
    std::shared_ptr<const Locale> fallback_;
    std::unordered_map<std::string, std::string> entries_;
};

// Receives the text the original shows in a "Warning" message box (MB_ICONWARNING) on a worker thread.
using WarningSink = std::function<void(const std::string& message)>;

// FUN_18004cbb0 with its catch funclet FUN_180050b40.
//  - missing file, with fallback:   warning `Transation file for locale "<name>" not found; will use fallback "<fb>"`
//  - missing file, no fallback:     CriticalError `Locale path "<path>" not found which has no fallback! ...`
//  - parse failure, with fallback:  warning `Failed to parse translation file for locale "<name>"; will use
//                                   fallback "<fb>". Error: <what>`
//  - parse failure, no fallback:    CriticalError `Failed to parse translation file for locale "<name>" which
//                                   has no fallback! Error: <what>`
std::shared_ptr<const Locale> load_locale(const std::filesystem::path& locale_dir, const std::string& name,
                                          std::shared_ptr<const Locale> fallback, const WarningSink& warn);

// UTF-8 -> UTF-16 as MultiByteToWideChar(CP_UTF8, 0, ...) (FUN_18000a920).
std::u16string utf8_to_utf16(std::string_view utf8);

struct MessageKey {
    std::int32_t category;
    std::int32_t id;
    std::string key;
};

struct FixedText {
    std::int32_t category;
    std::int32_t id;
    std::u16string text;
};

class MessageRepository {
public:
    MessageRepository(std::shared_ptr<const Locale> locale, std::vector<MessageKey> keys,
                      std::vector<FixedText> fixed = {});

    // FUN_18000a1c0(repo, category, id). Note the argument order (category first), the opposite of ersc.dll.
    //  - no locale loaded                 -> "[string not found]"
    //  - fixed text for (category, id)    -> that text, unformatted
    //  - (category, id) not in key table  -> std::nullopt
    //  - key missing from all locales     -> `KEY "<key>" NOT FOUND`
    //  - text is used as a format string; a format error yields `FORMAT ERROR FOR "<key>": <what>`
    std::optional<std::u16string> lookup(std::int32_t category, std::int32_t id) const;

    // The message-override logic of FUN_18001e1c0. `game_lookup` stands for the game's own function,
    // called when the mod has no text. `special_value` is the integer the override reads for id 0x52409
    // (*(ctx[0] + 400)); the text is formatted with it twice.
    std::u16string override_lookup(std::int32_t category, std::int32_t id, std::int32_t special_value,
                                   const std::function<std::u16string()>& game_lookup) const;

private:
    template <class... Args>
    std::optional<std::u16string> resolve(std::int32_t category, std::int32_t id, const Args&... args) const;

    std::shared_ptr<const Locale> locale_;
    std::map<std::int32_t, std::map<std::int32_t, std::string>> keys_;
    std::map<std::int32_t, std::map<std::int32_t, std::u16string>> fixed_;
};

}  // namespace nrsc::locale
