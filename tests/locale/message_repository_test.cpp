// nrsc.dll localisation (FUN_18004cbb0/FUN_180050b40, FUN_18000a1c0/FUN_18000a7d0, FUN_18001e1c0).
// The (category, id) values below are TEST FIXTURES: the real key table is built by protected code.
#include "nrsc/locale/message_repository.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace nrsc::locale;

namespace {

const std::filesystem::path kReferenceDir(NRSC_REFERENCE_DIR);

class LocaleDir : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("nrsc_locale_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::create_directories(dir_);
        std::filesystem::copy_file(kReferenceDir / "english.json", dir_ / "english.json",
                                   std::filesystem::copy_options::overwrite_existing);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }
    void write(const std::string& name, const std::string& json) {
        std::ofstream(dir_ / (name + ".json"), std::ios::binary) << json;
    }
    std::shared_ptr<const Locale> english() { return load_locale(dir_, "english", nullptr, nullptr); }
    std::filesystem::path dir_;
};

// Fixture table (not from the binary).
std::vector<MessageKey> fixture_keys() {
    return {
        {1, 10, "MODMESSAGE_YUINEXUSPAUSED"},
        {1, 11, "MODMESSAGE_YUINEXUSNOSEAMLESS"},
        {1, 12, "NOT_IN_ENGLISH"},
        {1, 13, "BROKEN"},
        {kSpecialMessageCategory, kSpecialMessageId, "MODMESSAGE_YUINEXUSPLAYERCOUNTREMAINING"},
    };
}

}  // namespace

// --- shipped data ---------------------------------------------------------------------------------

TEST(NrscLocaleData, AllBinaryKeysExistInShippedEnglish) {
    Locale l("english", nullptr);
    std::ifstream in(kReferenceDir / "english.json", std::ios::binary);
    l.load_json(std::string(std::istreambuf_iterator<char>(in), {}));
    EXPECT_EQ(l.size(), 9u);
    for (auto k : kKnownKeys) EXPECT_TRUE(l.find(std::string(k))) << k;
    EXPECT_TRUE(l.find("CREDITS"));  // in the file, not referenced by the binary
}

// --- loader ---------------------------------------------------------------------------------------

TEST_F(LocaleDir, MissingWithFallbackWarns) {
    std::vector<std::string> warnings;
    auto l = load_locale(dir_, "german", english(), [&](const std::string& m) { warnings.push_back(m); });
    EXPECT_EQ(l->size(), 0u);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0], "Transation file for locale \"german\" not found; will use fallback \"english\"");
    EXPECT_TRUE(l->find("MODMESSAGE_YUINEXUSPAUSED"));
}

TEST_F(LocaleDir, MissingWithoutFallbackIsCritical) {
    std::filesystem::remove(dir_ / "english.json");
    try {
        load_locale(dir_, "english", nullptr, nullptr);
        FAIL();
    } catch (const CriticalError& e) {
        const std::string m = e.what();
        EXPECT_TRUE(m.starts_with("Locale path \"")) << m;
        EXPECT_TRUE(m.find("english.json\" not found which has no fallback!") != std::string::npos) << m;
    }
}

TEST_F(LocaleDir, ParseErrorWithFallbackWarnsAndContinues) {
    write("german", "[1, 2]");
    std::vector<std::string> warnings;
    auto l = load_locale(dir_, "german", english(), [&](const std::string& m) { warnings.push_back(m); });
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0],
              "Failed to parse translation file for locale \"german\"; will use fallback \"english\". Error: "
              "Expected JSON object in translation file for locale \"german\", got array");
    EXPECT_TRUE(l->find("MODMESSAGE_YUINEXUSPAUSED"));
}

TEST_F(LocaleDir, ParseErrorWithoutFallbackIsCritical) {
    write("english", "{\"A\": 5}");
    try {
        load_locale(dir_, "english", nullptr, nullptr);
        FAIL();
    } catch (const CriticalError& e) {
        EXPECT_STREQ(e.what(),
                     "Failed to parse translation file for locale \"english\" which has no fallback! Error: "
                     "[json.exception.type_error.302] type must be string, but is number");
    }
}

// --- lookup ---------------------------------------------------------------------------------------

TEST_F(LocaleDir, LookupIsCategoryThenId) {
    MessageRepository repo(english(), fixture_keys());
    EXPECT_EQ(repo.lookup(1, 10), u"Paused");
    EXPECT_FALSE(repo.lookup(10, 1));
}

TEST_F(LocaleDir, LookupResultsForEachCase) {
    MessageRepository repo(english(), fixture_keys());
    EXPECT_EQ(repo.lookup(1, 11), u"Failed to join lobby\nThe host is not using seamless co-op");
    EXPECT_EQ(repo.lookup(1, 12), u"KEY \"NOT_IN_ENGLISH\" NOT FOUND");
    EXPECT_FALSE(repo.lookup(1, 99));
}

TEST(NrscLookup, NoLocaleGivesStringNotFound) {
    MessageRepository repo(nullptr, fixture_keys());
    EXPECT_EQ(repo.lookup(1, 10), u"[string not found]");
    EXPECT_EQ(repo.lookup(5, 5), u"[string not found]");
}

TEST_F(LocaleDir, FormatErrorIsReportedNotThrown) {
    write("custom", R"({"BROKEN": "Value {}"})");
    MessageRepository repo(load_locale(dir_, "custom", english(), nullptr), fixture_keys());
    const auto text = repo.lookup(1, 13);
    ASSERT_TRUE(text);
    EXPECT_TRUE(text->starts_with(u"FORMAT ERROR FOR \"BROKEN\": "));
}

TEST_F(LocaleDir, FixedTextsAreReturnedUnformatted) {
    MessageRepository repo(english(), fixture_keys(), {{7, 7, std::u16string(kBannerText)}});
    EXPECT_EQ(repo.lookup(7, 7), u"Elden Ring: Nightreign Seamless Co-op v1.1.3 by Yui");
}

// --- message override -------------------------------------------------------------------------------

TEST_F(LocaleDir, SpecialIdFormatsTheValueTwiceInCategory200) {
    MessageRepository repo(english(), fixture_keys());
    int game_calls = 0;
    const auto text = repo.override_lookup(/*category asked by the game*/ 33, kSpecialMessageId, 3, [&] {
        ++game_calls;
        return std::u16string(u"game");
    });
    EXPECT_EQ(text, u"3 / 3");
    EXPECT_EQ(game_calls, 0);
}

TEST_F(LocaleDir, UnknownMessagesFallThroughToTheGame) {
    MessageRepository repo(english(), fixture_keys());
    EXPECT_EQ(repo.override_lookup(1, 10, 0, [] { return std::u16string(u"game"); }), u"Paused");
    EXPECT_EQ(repo.override_lookup(1, 99, 0, [] { return std::u16string(u"game"); }), u"game");
}

TEST(NrscOverride, NoLocaleNeverReachesTheGame) {
    MessageRepository repo(nullptr, fixture_keys());
    EXPECT_EQ(repo.override_lookup(1, 99, 0, [] { return std::u16string(u"game"); }), u"[string not found]");
}

// --- UTF-8 -> UTF-16 ------------------------------------------------------------------------------

TEST(NrscUtf8, Conversion) {
    EXPECT_EQ(utf8_to_utf16(""), u"");
    EXPECT_EQ(utf8_to_utf16("{} / {}"), u"{} / {}");
    EXPECT_EQ(utf8_to_utf16("\xC3\xA9"), u"é");
    EXPECT_EQ(utf8_to_utf16("\xFF"), u"�");
}
