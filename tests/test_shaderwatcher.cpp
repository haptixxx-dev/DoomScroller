#include "engine/ShaderWatcher.h"

#include <catch2/catch_test_macros.hpp>

using ds::detectChanges;
using ds::WatchEntry;

TEST_CASE("detectChanges reports nothing when mtimes are unchanged", "[shaderwatcher]") {
    std::vector<WatchEntry> state   = {{"mesh", 100}, {"particle", 200}};
    std::vector<WatchEntry> current = {{"mesh", 100}, {"particle", 200}};

    auto changed = detectChanges(state, current);
    CHECK(changed.empty());
    // State is preserved.
    REQUIRE(state.size() == 2);
}

TEST_CASE("detectChanges reports only the shader whose mtime increased", "[shaderwatcher]") {
    std::vector<WatchEntry> state   = {{"mesh", 100}, {"particle", 200}};
    std::vector<WatchEntry> current = {{"mesh", 150}, {"particle", 200}};

    auto changed = detectChanges(state, current);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0] == "mesh");

    // State now matches current, so a second identical poll is quiet.
    auto again = detectChanges(state, current);
    CHECK(again.empty());
}

TEST_CASE("detectChanges reports a newly appearing entry", "[shaderwatcher]") {
    std::vector<WatchEntry> state   = {{"mesh", 100}};
    std::vector<WatchEntry> current = {{"mesh", 100}, {"blur", 50}};

    auto changed = detectChanges(state, current);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0] == "blur");

    // The new entry is now tracked; re-polling the same snapshot reports nothing.
    REQUIRE(state.size() == 2);
    auto again = detectChanges(state, current);
    CHECK(again.empty());
}

TEST_CASE("detectChanges does not report a shader whose mtime decreased", "[shaderwatcher]") {
    // A clock going backwards (or a restored older file) should not be treated
    // as a change: only strictly-increasing mtimes count.
    std::vector<WatchEntry> state   = {{"mesh", 200}};
    std::vector<WatchEntry> current = {{"mesh", 100}};

    auto changed = detectChanges(state, current);
    CHECK(changed.empty());
    // State still tracks the latest snapshot value.
    REQUIRE(state.size() == 1);
    CHECK(state[0].lastModified == 100);
}

TEST_CASE("detectChanges reports multiple changes in one poll", "[shaderwatcher]") {
    std::vector<WatchEntry> state   = {{"mesh", 100}, {"particle", 200}, {"blur", 300}};
    std::vector<WatchEntry> current = {{"mesh", 101}, {"particle", 200}, {"blur", 301}};

    auto changed = detectChanges(state, current);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0] == "mesh");
    CHECK(changed[1] == "blur");
}
