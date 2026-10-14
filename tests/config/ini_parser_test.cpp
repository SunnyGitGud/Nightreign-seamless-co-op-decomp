// Behaviour of the inih copy inlined in nrsc.dll FUN_180037c50 (ev_a3a8a266709861a9).
#include "nrsc/config/ini_parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

using nrsc::config::ini_parse_text;

namespace {

using Entry = std::tuple<std::string, std::string, std::string>;

struct Collected {
    int error;
    std::vector<Entry> entries;
};

Collected parse(const std::string& text) {
    Collected c;
    c.error = ini_parse_text(text, [&](auto s, auto n, auto v) {
        c.entries.emplace_back(std::string(s), std::string(n), std::string(v));
        return true;
    });
    return c;
}

}  // namespace

TEST(IniParser, SectionsNamesAndValuesAreTrimmed) {
    const auto c = parse("[ A ]\n  key  =  some value  \n");
    EXPECT_EQ(c.error, 0);
    ASSERT_EQ(c.entries.size(), 1u);
    // Inside brackets nothing is trimmed: the section is " A ".
    EXPECT_EQ(c.entries[0], Entry(" A ", "key", "some value"));
}

TEST(IniParser, WholeLineCommentsUseSemicolonOrHash) {
    const auto c = parse("; one\n# two\n   ; indented\n[S]\nk=v\n");
    EXPECT_EQ(c.error, 0);
    ASSERT_EQ(c.entries.size(), 1u);
}

TEST(IniParser, InlineCommentNeedsPrecedingWhitespace) {
    const auto c = parse("[S]\na = 1 ; note\nb = x;y\n");
    ASSERT_EQ(c.entries.size(), 2u);
    EXPECT_EQ(std::get<2>(c.entries[0]), "1");
    EXPECT_EQ(std::get<2>(c.entries[1]), "x;y");
}

TEST(IniParser, ColonIsAlsoASeparator) {
    const auto c = parse("[S]\nk : v\n");
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(c.entries[0], Entry("S", "k", "v"));
}

TEST(IniParser, EmptyValueIsPassedThrough) {
    // The shipped file contains "cooppassword = " with nothing after it.
    const auto c = parse("[PASSWORD]\ncooppassword = \n");
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(std::get<2>(c.entries[0]), "");
}

TEST(IniParser, Utf8BomSkippedOnFirstLineOnly) {
    const auto c = parse("\xEF\xBB\xBF[S]\nk=v\n");
    EXPECT_EQ(c.error, 0);
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(std::get<0>(c.entries[0]), "S");
}

TEST(IniParser, IndentedLineContinuesPreviousValue) {
    const auto c = parse("[S]\nk = first\n  second\n");
    ASSERT_EQ(c.entries.size(), 2u);
    EXPECT_EQ(c.entries[1], Entry("S", "k", "second"));
}

TEST(IniParser, NewSectionStopsContinuation) {
    // After a section header, prev_name is cleared, so an indented line is parsed normally.
    const auto c = parse("[S]\nk = v\n[T]\n  m = w\n");
    ASSERT_EQ(c.entries.size(), 2u);
    EXPECT_EQ(c.entries[1], Entry("T", "m", "w"));
}

TEST(IniParser, MissingSeparatorIsAnError) {
    EXPECT_EQ(parse("[S]\nk=v\njunk\n").error, 3);
}

TEST(IniParser, UnterminatedSectionIsAnError) {
    EXPECT_EQ(parse("[S\nk=v\n").error, 1);
}

TEST(IniParser, FirstErrorIsKeptAndParsingContinues) {
    const auto c = parse("[S]\nbad\nk=v\nalso bad\n");
    EXPECT_EQ(c.error, 2);
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(c.entries[0], Entry("S", "k", "v"));
}

TEST(IniParser, SemicolonAfterSpaceBeforeSeparatorIsAnError) {
    // find_chars_or_comment() stops at the comment before reaching '='.
    EXPECT_EQ(parse("[S]\nk ;x = v\n").error, 2);
}

TEST(IniParser, CrLfLineEndingsBehaveLikeLf) {
    const auto c = parse("[S]\r\nk = v\r\n");
    EXPECT_EQ(c.error, 0);
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(std::get<2>(c.entries[0]), "v");
}

TEST(IniParser, CtrlZEndsTheFileInTextMode) {
    const auto c = parse("[S]\nk=v\n\x1agarbage without separator\n");
    EXPECT_EQ(c.error, 0);
    EXPECT_EQ(c.entries.size(), 1u);
}

TEST(IniParser, LongLinesAreSplitIntoFgetsChunks) {
    // 199 bytes per fgets() call. The remainder becomes its own "line" and counts for numbering.
    const std::string value(220, 'x');
    const auto c = parse("[S]\nk=" + value + "\nm=1\n");
    // Line 2 is "k=" + 197 x's; line 3 is the remaining 23 x's, which has no '='.
    EXPECT_EQ(c.error, 3);
    ASSERT_EQ(c.entries.size(), 2u);
    EXPECT_EQ(std::get<2>(c.entries[0]), std::string(197, 'x'));
    EXPECT_EQ(c.entries[1], Entry("S", "m", "1"));
}

TEST(IniParser, SectionNameTruncatedTo49Characters) {
    const std::string longname(60, 's');
    const auto c = parse("[" + longname + "]\nk=v\n");
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(std::get<0>(c.entries[0]), std::string(49, 's'));
}

TEST(IniParser, KeyNameIsNotTruncated) {
    const std::string longname(60, 'n');
    const auto c = parse("[S]\n" + longname + "=v\n");
    ASSERT_EQ(c.entries.size(), 1u);
    EXPECT_EQ(std::get<1>(c.entries[0]), longname);
}

TEST(IniParser, MissingFileReturnsMinusOne) {
    EXPECT_EQ(nrsc::config::ini_parse_file("/nonexistent/nrsc_settings.ini",
                                           [](auto, auto, auto) { return true; }),
              -1);
}
