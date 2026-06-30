#include "engine/SettingsStore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("serialize/parse round-trips every field", "[settings]") {
    GameSettings in;
    in.masterVolume    = 0.5f;
    in.sfxVolume       = 0.75f;
    in.musicVolume     = 0.25f;
    in.uiVolume        = 0.6f;
    in.lookSensitivity = 0.2f;
    in.windowWidth     = 1920;
    in.windowHeight    = 1080;
    in.fullscreen      = true;
    in.vsync           = false;

    auto parsed = parseSettings(serializeSettings(in));
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->masterVolume == Catch::Approx(in.masterVolume));
    REQUIRE(parsed->sfxVolume == Catch::Approx(in.sfxVolume));
    REQUIRE(parsed->musicVolume == Catch::Approx(in.musicVolume));
    REQUIRE(parsed->uiVolume == Catch::Approx(in.uiVolume));
    REQUIRE(parsed->lookSensitivity == Catch::Approx(in.lookSensitivity));
    REQUIRE(parsed->windowWidth == in.windowWidth);
    REQUIRE(parsed->windowHeight == in.windowHeight);
    REQUIRE(parsed->fullscreen == in.fullscreen);
    REQUIRE(parsed->vsync == in.vsync);
}

TEST_CASE("float round-trip is bit-exact (%.9g precision)", "[settings]") {
    // %.9g preserves every IEEE-754 float bit-for-bit; assert exact == (not
    // Approx) on an awkward value that %.6g would round and corrupt.
    GameSettings in;
    in.lookSensitivity = 0.123456789f;
    in.masterVolume    = 0.333333343f; // not representable in 6 sig digits
    auto parsed        = parseSettings(serializeSettings(in));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->lookSensitivity == in.lookSensitivity);
    REQUIRE(parsed->masterVolume == in.masterVolume);
}

TEST_CASE("defaults round-trip cleanly", "[settings]") {
    GameSettings in; // all defaults
    auto parsed = parseSettings(serializeSettings(in));
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->masterVolume == Catch::Approx(1.f));
    REQUIRE(parsed->sfxVolume == Catch::Approx(1.f));
    REQUIRE(parsed->musicVolume == Catch::Approx(0.8f));
    REQUIRE(parsed->uiVolume == Catch::Approx(0.9f));
    REQUIRE(parsed->lookSensitivity == Catch::Approx(0.1f));
    REQUIRE(parsed->windowWidth == 1280);
    REQUIRE(parsed->windowHeight == 720);
    REQUIRE(parsed->fullscreen == false);
    REQUIRE(parsed->vsync == true);
}

TEST_CASE("wrong version is rejected", "[settings]") {
    std::string blob = "DSSETTINGS 2\nmasterVolume 0.5\n";
    REQUIRE_FALSE(parseSettings(blob).has_value());
}

TEST_CASE("missing magic is rejected", "[settings]") {
    REQUIRE_FALSE(parseSettings("masterVolume 0.5\nvsync 1\n").has_value());
    REQUIRE_FALSE(parseSettings("").has_value());
    REQUIRE_FALSE(parseSettings("   \n\n").has_value());
}

TEST_CASE("missing keys keep struct defaults", "[settings]") {
    // Only two keys present; the rest must stay at their defaults.
    std::string blob = "DSSETTINGS 1\nmasterVolume 0.3\nwindowWidth 800\n";
    auto parsed      = parseSettings(blob);
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->masterVolume == Catch::Approx(0.3f));
    REQUIRE(parsed->windowWidth == 800);

    // Untouched fields stay at their defaults.
    REQUIRE(parsed->sfxVolume == Catch::Approx(1.f));
    REQUIRE(parsed->musicVolume == Catch::Approx(0.8f));
    REQUIRE(parsed->lookSensitivity == Catch::Approx(0.1f));
    REQUIRE(parsed->windowHeight == 720);
    REQUIRE(parsed->fullscreen == false);
    REQUIRE(parsed->vsync == true);
}

TEST_CASE("unknown keys are ignored", "[settings]") {
    std::string blob = "DSSETTINGS 1\nbogusKey hello\nvsync 0\nanother 42\n";
    auto parsed      = parseSettings(blob);
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->vsync == false);
    // Defaults untouched by the noise lines.
    REQUIRE(parsed->masterVolume == Catch::Approx(1.f));
}

TEST_CASE("malformed numeric lines are skipped, keeping defaults", "[settings]") {
    std::string blob = "DSSETTINGS 1\nmasterVolume notanumber\nwindowWidth 12x3\n"
                       "sfxVolume 0.42\n";
    auto parsed      = parseSettings(blob);
    REQUIRE(parsed.has_value());

    // Bad values were skipped; defaults remain.
    REQUIRE(parsed->masterVolume == Catch::Approx(1.f));
    REQUIRE(parsed->windowWidth == 1280);
    // A well-formed line after the bad ones still applies.
    REQUIRE(parsed->sfxVolume == Catch::Approx(0.42f));
}

TEST_CASE("bools accept true/false and 0/1", "[settings]") {
    std::string blob = "DSSETTINGS 1\nfullscreen true\nvsync false\n";
    auto parsed      = parseSettings(blob);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->fullscreen == true);
    REQUIRE(parsed->vsync == false);

    std::string blob2 = "DSSETTINGS 1\nfullscreen 1\nvsync 0\n";
    auto parsed2      = parseSettings(blob2);
    REQUIRE(parsed2.has_value());
    REQUIRE(parsed2->fullscreen == true);
    REQUIRE(parsed2->vsync == false);
}
