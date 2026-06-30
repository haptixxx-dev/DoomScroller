#include "engine/StyleMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ds;

TEST_CASE("StyleConfig sane defaults", "[style]") {
    StyleConfig cfg;
    REQUIRE(cfg.thresholds[0] == Catch::Approx(0.f)); // D starts at 0
    // Thresholds are strictly ascending.
    for (int i = 1; i < 7; ++i)
        REQUIRE(cfg.thresholds[i] > cfg.thresholds[i - 1]);
    REQUIRE(cfg.maxPoints > cfg.thresholds[6]); // can reach SSS below the cap
}

TEST_CASE("styleEventPoints rewards flashier kills", "[style]") {
    REQUIRE(styleEventPoints(StyleEvent::Kill) == Catch::Approx(20.f));
    REQUIRE(styleEventPoints(StyleEvent::AirKill) == Catch::Approx(45.f));
    REQUIRE(styleEventPoints(StyleEvent::DashKill) == Catch::Approx(50.f));
    REQUIRE(styleEventPoints(StyleEvent::WeaponSwitchKill) == Catch::Approx(40.f));
    REQUIRE(styleEventPoints(StyleEvent::MultiKill) == Catch::Approx(70.f));
    REQUIRE(styleEventPoints(StyleEvent::Parry) == Catch::Approx(60.f));
    // A plain kill is the floor.
    REQUIRE(styleEventPoints(StyleEvent::Kill) < styleEventPoints(StyleEvent::AirKill));
}

TEST_CASE("rankForPoints zero is D", "[style]") {
    REQUIRE(rankForPoints(0.f) == StyleRank::D);
    REQUIRE(rankForPoints(-5.f) == StyleRank::D); // below floor still D
}

TEST_CASE("rankForPoints uses highest threshold <= points", "[style]") {
    StyleConfig cfg; // thresholds {0,80,200,380,600,800,950}
    // Just below a threshold gives the lower rank.
    REQUIRE(rankForPoints(79.f, cfg) == StyleRank::D);
    REQUIRE(rankForPoints(199.f, cfg) == StyleRank::C);
    // Exactly at a threshold gives the higher rank.
    REQUIRE(rankForPoints(80.f, cfg) == StyleRank::C);
    REQUIRE(rankForPoints(200.f, cfg) == StyleRank::B);
    // Above a threshold keeps the higher rank.
    REQUIRE(rankForPoints(381.f, cfg) == StyleRank::A);
    REQUIRE(rankForPoints(605.f, cfg) == StyleRank::S);
    REQUIRE(rankForPoints(805.f, cfg) == StyleRank::SS);
    REQUIRE(rankForPoints(960.f, cfg) == StyleRank::SSS);
}

TEST_CASE("addStyleEvent raises points and can raise rank", "[style]") {
    StyleState s;
    REQUIRE(s.points == Catch::Approx(0.f));
    REQUIRE(s.rank == StyleRank::D);

    addStyleEvent(s, StyleEvent::Kill); // +20 -> still D (< 80)
    REQUIRE(s.points == Catch::Approx(20.f));
    REQUIRE(s.rank == StyleRank::D);

    addStyleEvent(s, StyleEvent::DashKill); // +50 -> 70, still D
    REQUIRE(s.points == Catch::Approx(70.f));
    REQUIRE(s.rank == StyleRank::D);

    addStyleEvent(s, StyleEvent::MultiKill); // +70 -> 140 -> rank C
    REQUIRE(s.points == Catch::Approx(140.f));
    REQUIRE(s.rank == StyleRank::C);
}

TEST_CASE("addStyleEvent clamps points at maxPoints", "[style]") {
    StyleConfig cfg;
    StyleState s;
    s.points = cfg.maxPoints - 10.f;
    addStyleEvent(s, StyleEvent::MultiKill, cfg); // would overshoot the cap
    REQUIRE(s.points == Catch::Approx(cfg.maxPoints));
    REQUIRE(s.rank == StyleRank::SSS);
}

TEST_CASE("tickStyle decays points toward zero and rank falls", "[style]") {
    StyleConfig cfg;  // decayPerSec 12
    StyleState s;
    s.points = 210.f; // rank B (>=200)
    s.rank   = rankForPoints(s.points, cfg);
    REQUIRE(s.rank == StyleRank::B);

    tickStyle(s, 1.f, cfg); // -12 -> 198 -> drops to rank C
    REQUIRE(s.points == Catch::Approx(198.f));
    REQUIRE(s.rank == StyleRank::C);

    // A huge dt cannot drive points negative.
    tickStyle(s, 100.f, cfg);
    REQUIRE(s.points == Catch::Approx(0.f));
    REQUIRE(s.rank == StyleRank::D);
}

TEST_CASE("rankLabel maps ranks to HUD strings", "[style]") {
    REQUIRE(std::string(rankLabel(StyleRank::D)) == "D");
    REQUIRE(std::string(rankLabel(StyleRank::C)) == "C");
    REQUIRE(std::string(rankLabel(StyleRank::B)) == "B");
    REQUIRE(std::string(rankLabel(StyleRank::A)) == "A");
    REQUIRE(std::string(rankLabel(StyleRank::S)) == "S");
    REQUIRE(std::string(rankLabel(StyleRank::SS)) == "SS");
    REQUIRE(std::string(rankLabel(StyleRank::SSS)) == "SSS");
}
