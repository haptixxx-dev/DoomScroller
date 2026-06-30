#include "engine/InputMap.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

using namespace ds;

TEST_CASE("defaultInputMap binds every action to a distinct nonzero code", "[input]") {
    InputMap map = defaultInputMap();
    for (int i = 0; i < kActionCount; ++i)
        REQUIRE(map.codes[i] != kUnbound);

    // MoveForward and MoveBack must not share a binding.
    REQUIRE(bindingFor(map, Action::MoveForward) != bindingFor(map, Action::MoveBack));

    // Every action code is distinct.
    for (int i = 0; i < kActionCount; ++i)
        for (int j = i + 1; j < kActionCount; ++j)
            REQUIRE(map.codes[i] != map.codes[j]);
}

TEST_CASE("setBinding then bindingFor round-trips", "[input]") {
    InputMap map = defaultInputMap();
    setBinding(map, Action::Jump, 4242u);
    REQUIRE(bindingFor(map, Action::Jump) == 4242u);

    setBinding(map, Action::Jump, kUnbound);
    REQUIRE(bindingFor(map, Action::Jump) == kUnbound);
}

TEST_CASE("actionForCode finds bound codes and rejects unbound", "[input]") {
    InputMap map = defaultInputMap();
    setBinding(map, Action::Fire, 900u);

    auto found = actionForCode(map, 900u);
    REQUIRE(found.has_value());
    REQUIRE(*found == Action::Fire);

    // A code bound to nothing yields nullopt.
    REQUIRE_FALSE(actionForCode(map, 999999u).has_value());

    // kUnbound (0) never matches.
    REQUIRE_FALSE(actionForCode(map, kUnbound).has_value());
}

TEST_CASE("serialize then parse reproduces all bindings", "[input]") {
    InputMap map = defaultInputMap();
    setBinding(map, Action::Dash, 7000u);
    setBinding(map, Action::Weapon3, 8000u);

    std::string text = serializeInputMap(map);
    auto parsed      = parseInputMap(text);
    REQUIRE(parsed.has_value());

    for (int i = 0; i < kActionCount; ++i)
        REQUIRE(parsed->codes[i] == map.codes[i]);
}

TEST_CASE("parseInputMap rejects a wrong-version header", "[input]") {
    std::string text = "DSINPUT 2\nmove_forward 101\n";
    REQUIRE_FALSE(parseInputMap(text).has_value());

    // Missing header entirely also fails.
    REQUIRE_FALSE(parseInputMap("move_forward 101\n").has_value());
}

TEST_CASE("parseInputMap ignores unknown actions and keeps defaults for missing", "[input]") {
    InputMap defaults = defaultInputMap();

    // Only rebinds Fire; bogus_action is unknown and ignored; everything else
    // keeps its default.
    std::string text = "DSINPUT 1\nbogus_action 555\nfire 321\n";
    auto parsed      = parseInputMap(text);
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->codes[static_cast<int>(Action::Fire)] == 321u);
    REQUIRE(bindingFor(*parsed, Action::MoveForward) == bindingFor(defaults, Action::MoveForward));
    REQUIRE(actionForCode(*parsed, 555u) == std::nullopt);
}

TEST_CASE("actionFromName inverts actionName", "[input]") {
    REQUIRE(actionFromName(actionName(Action::MoveForward)) == Action::MoveForward);
    REQUIRE(actionFromName(actionName(Action::Parry)) == Action::Parry);
    REQUIRE(actionFromName(actionName(Action::Weapon2)) == Action::Weapon2);

    REQUIRE_FALSE(actionFromName("not_a_real_action").has_value());
}
