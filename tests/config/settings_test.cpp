// nrsc\mod\preferences\preferences.cpp (FUN_180037c50, ev_a3a8a266709861a9).
#include "nrsc/config/settings.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "nrsc/core/fatal_error.hpp"

using namespace nrsc::config;
using nrsc::FatalError;

namespace {

const std::filesystem::path kShippedIni = std::filesystem::path(NRSC_REFERENCE_DIR) / "nrsc_settings.ini";

Settings read(const std::string& text) { return read_settings(IniReader::from_text(text)); }

}  // namespace

// --- shipped file ------------------------------------------------------------------------------

TEST(NrscShipped, ShippedFileLoads) {
    const Settings s = load_settings(kShippedIni);
    EXPECT_FLOAT_EQ(s.preferences.health_scaling, 1.0f);
    EXPECT_FLOAT_EQ(s.preferences.damage_scaling, 1.0f);
    EXPECT_FLOAT_EQ(s.preferences.posture_scaling, 1.0f);
    EXPECT_TRUE(s.preferences.allow_invaders);  // not in the shipped file -> default
    EXPECT_EQ(s.save_file_extension, "co2");
}

TEST(NrscShipped, NoPasswordIsRequired) {
    // Unlike ersc.dll, nrsc.dll reads no [PASSWORD] key, so an empty file is accepted.
    EXPECT_NO_THROW(read(""));
}

// --- scaling -------------------------------------------------------------------------------------

TEST(NrscScaling, DefaultsAreOneHundredPercent) {
    const Preferences p = read("").preferences;
    EXPECT_FLOAT_EQ(p.health_scaling, 1.0f);
    EXPECT_FLOAT_EQ(p.damage_scaling, 1.0f);
    EXPECT_FLOAT_EQ(p.posture_scaling, 1.0f);
}

TEST(NrscScaling, DividedByOneHundred) {
    const Preferences p = read("[SCALING]\nhealth_scaling=35\ndamage_scaling=250\nposture_scaling=0\n").preferences;
    EXPECT_FLOAT_EQ(p.health_scaling, 0.35f);
    EXPECT_FLOAT_EQ(p.damage_scaling, 2.5f);
    EXPECT_FLOAT_EQ(p.posture_scaling, 0.0f);
}

TEST(NrscScaling, NegativeValuesAreMirroredNotClamped) {
    EXPECT_FLOAT_EQ(read("[SCALING]\nhealth_scaling=-50\n").preferences.health_scaling, 0.5f);
}

TEST(NrscScaling, UnparsableFallsBackToOneHundred) {
    EXPECT_FLOAT_EQ(read("[SCALING]\nhealth_scaling=lots\n").preferences.health_scaling, 1.0f);
}

TEST(NrscScaling, NanStaysNan) {
    EXPECT_TRUE(std::isnan(read("[SCALING]\nhealth_scaling=nan\n").preferences.health_scaling));
}

TEST(NrscScaling, KeysAreCaseInsensitive) {
    EXPECT_FLOAT_EQ(read("[scaling]\nHEALTH_SCALING=20\n").preferences.health_scaling, 0.2f);
}

// --- allow_invaders ------------------------------------------------------------------------------

TEST(NrscGameplay, AllowInvadersBooleanSpellings) {
    EXPECT_FALSE(read("[GAMEPLAY]\nallow_invaders=0\n").preferences.allow_invaders);
    EXPECT_FALSE(read("[GAMEPLAY]\nallow_invaders=Off\n").preferences.allow_invaders);
    EXPECT_TRUE(read("[GAMEPLAY]\nallow_invaders=yes\n").preferences.allow_invaders);
    EXPECT_TRUE(read("[GAMEPLAY]\nallow_invaders=maybe\n").preferences.allow_invaders);  // default
    EXPECT_TRUE(read("[GAMEPLAY]\nallow_invaders=\n").preferences.allow_invaders);
}

// --- save_file_extension -------------------------------------------------------------------------

TEST(NrscSaveExtension, SameRulesAsErsc) {
    EXPECT_EQ(validate_save_file_extension("co3"), "co3");
    EXPECT_EQ(validate_save_file_extension(""), "co2");
    EXPECT_EQ(validate_save_file_extension("sl2"), "co2");
    EXPECT_EQ(validate_save_file_extension("sl"), "co2");
    EXPECT_EQ(validate_save_file_extension("a.b"), "co2");
    EXPECT_EQ(validate_save_file_extension(std::string(128, 'x')), std::string(128, 'x'));
    EXPECT_THROW(validate_save_file_extension(std::string(129, 'x')), std::out_of_range);
}

// --- location and fatal paths ------------------------------------------------------------------

TEST(NrscSettingsPath, NextToTheModule) {
    EXPECT_EQ(settings_path_for_module("D:\\Steam\\NIGHTREIGN\\Game\\SeamlessCoop\\nrsc.dll"),
              "D:\\Steam\\NIGHTREIGN\\Game\\SeamlessCoop\\nrsc_settings.ini");
    EXPECT_THROW(settings_path_for_module("nrsc.dll"), FatalError);
}

TEST(NrscSettingsLoad, MissingFileIsFatalAtLine0x16) {
    try {
        load_settings("/nonexistent/nrsc_settings.ini");
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_EQ(e.line(), 0x16u);
        EXPECT_STREQ(e.what(), "Failed to read settings. Unable to find:\n\"/nonexistent/nrsc_settings.ini\"");
    }
}

TEST(NrscSettingsLoad, ParseErrorIsFatalAtLine0x1a) {
    const auto path = std::filesystem::temp_directory_path() / "nrsc_bad_settings.ini";
    std::ofstream(path) << "[SCALING]\nhealth_scaling = 10\noops\n";
    try {
        load_settings(path);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_EQ(e.line(), 0x1au);
        EXPECT_STREQ(e.what(), "Failed to read settings. Error on line 3");
    }
    std::filesystem::remove(path);
}
