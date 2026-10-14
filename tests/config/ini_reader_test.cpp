// INIReader equivalents in nrsc.dll: FUN_180005960/005d80/0064a0/0067d0.
#include "nrsc/config/ini_reader.hpp"

#include <gtest/gtest.h>

#include <climits>

using nrsc::config::IniReader;

TEST(IniReader, MakeKeyLowercasesSectionEqualsName) {
    EXPECT_EQ(IniReader::make_key("GAMEPLAY", "Allow_Invaders"), "gameplay=allow_invaders");
}

TEST(IniReader, LookupIsCaseInsensitive) {
    const auto r = IniReader::from_text("[Gameplay]\nALLOW_INVADERS = 0\n");
    EXPECT_FALSE(r.get_boolean("GAMEPLAY", "allow_invaders", true));
}

TEST(IniReader, RepeatedKeyJoinsValuesWithNewline) {
    const auto r = IniReader::from_text("[S]\nk = a\nk = b\n");
    EXPECT_EQ(r.get("S", "k", ""), "a\nb");
}

TEST(IniReader, MultilineContinuationJoinsWithNewline) {
    const auto r = IniReader::from_text("[S]\nk = a\n  b\n");
    EXPECT_EQ(r.get("S", "k", ""), "a\nb");
}

TEST(IniReader, EmptyFirstValueIsNotFollowedByNewline) {
    // ValueHandler only inserts '\n' if the stored value is non-empty.
    const auto r = IniReader::from_text("[S]\nk =\nk = b\n");
    EXPECT_EQ(r.get("S", "k", ""), "b");
}

TEST(IniReader, MissingKeyReturnsDefault) {
    const auto r = IniReader::from_text("[S]\n");
    EXPECT_EQ(r.get("S", "k", "dflt"), "dflt");
    EXPECT_EQ(r.get_integer("S", "k", 7), 7);
    EXPECT_DOUBLE_EQ(r.get_real("S", "k", 2.5), 2.5);
    EXPECT_TRUE(r.get_boolean("S", "k", true));
}

TEST(IniReader, BooleanAcceptedSpellings) {
    for (const char* t : {"true", "TRUE", "Yes", "on", "1"}) {
        const auto r = IniReader::from_text(std::string("[S]\nk=") + t + "\n");
        EXPECT_TRUE(r.get_boolean("S", "k", false)) << t;
    }
    for (const char* f : {"false", "No", "OFF", "0"}) {
        const auto r = IniReader::from_text(std::string("[S]\nk=") + f + "\n");
        EXPECT_FALSE(r.get_boolean("S", "k", true)) << f;
    }
}

TEST(IniReader, BooleanUnrecognisedOrEmptyFallsBackToDefault) {
    for (const char* v : {"2", "y", "enabled", ""}) {
        const auto r = IniReader::from_text(std::string("[S]\nk=") + v + "\n");
        EXPECT_TRUE(r.get_boolean("S", "k", true)) << v;
        EXPECT_FALSE(r.get_boolean("S", "k", false)) << v;
    }
}

TEST(IniReader, IntegerUsesStrtolBaseZero) {
    const auto r = IniReader::from_text("[S]\nhex=0x10\noct=010\nneg=-3\nlead=12abc\nbad=abc\n");
    EXPECT_EQ(r.get_integer("S", "hex", 0), 16);
    EXPECT_EQ(r.get_integer("S", "oct", 0), 8);
    EXPECT_EQ(r.get_integer("S", "neg", 0), -3);
    EXPECT_EQ(r.get_integer("S", "lead", 0), 12);
    EXPECT_EQ(r.get_integer("S", "bad", 99), 99);
}

TEST(IniReader, IntegerSaturatesLikeWindows32BitLong) {
    const auto r = IniReader::from_text("[S]\nbig=99999999999\nsmall=-99999999999\n");
    EXPECT_EQ(r.get_integer("S", "big", 0), INT32_MAX);
    EXPECT_EQ(r.get_integer("S", "small", 0), INT32_MIN);
}

TEST(IniReader, RealUsesStrtod) {
    const auto r = IniReader::from_text("[S]\na=35\nb=12.5\nc=1e2\nd=x\n");
    EXPECT_DOUBLE_EQ(r.get_real("S", "a", 0), 35.0);
    EXPECT_DOUBLE_EQ(r.get_real("S", "b", 0), 12.5);
    EXPECT_DOUBLE_EQ(r.get_real("S", "c", 0), 100.0);
    EXPECT_DOUBLE_EQ(r.get_real("S", "d", 4.0), 4.0);
}
