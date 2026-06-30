#pragma once

#include <algorithm>
#include <cstdint>

namespace ds {

// Pure (engine-independent) ULTRAKILL-style style/rank meter math: a point pool
// that rewards flashy kills and bleeds back down over time, mapped to a D..SSS
// rank for the HUD. Kept free of SDL3 / Jolt / EnTT so it can be unit-tested
// against engine_headers only.

// Style ranks, ascending. The numeric values index StyleConfig::thresholds.
enum class StyleRank : uint8_t { D, C, B, A, S, SS, SSS };

// Live meter state owned by the player / game layer.
struct StyleState {
    float points   = 0.f;
    StyleRank rank = StyleRank::D;
};

// Tunables for the meter. `thresholds` are the ascending point floors for each
// rank (D starts at 0); a rank is earned when points reach its floor.
struct StyleConfig {
    float decayPerSec = 12.f; // points bled per second
    float maxPoints   = 1000.f;
    // ascending thresholds for D,C,B,A,S,SS,SSS (D starts at 0):
    float thresholds[7] = {0.f, 80.f, 200.f, 380.f, 600.f, 800.f, 950.f};
};

// Things the player can do that grant style points. Values from styleEventPoints.
enum class StyleEvent : uint8_t { Kill, AirKill, DashKill, WeaponSwitchKill, MultiKill, Parry };

// Points awarded for a given style event. Pure; ascending-ish reward for
// flashier actions.
inline float styleEventPoints(StyleEvent e) {
    switch (e) {
    case StyleEvent::Kill:
        return 20.f;
    case StyleEvent::AirKill:
        return 45.f;
    case StyleEvent::DashKill:
        return 50.f;
    case StyleEvent::WeaponSwitchKill:
        return 40.f;
    case StyleEvent::MultiKill:
        return 70.f;
    case StyleEvent::Parry:
        return 60.f;
    }
    return 0.f;
}

// Map a point total to the highest rank whose threshold is <= points. Pure;
// thresholds are assumed ascending with thresholds[0] == 0.
inline StyleRank rankForPoints(float points, const StyleConfig& cfg = {}) {
    StyleRank rank = StyleRank::D;
    for (uint8_t i = 0; i < 7; ++i) {
        if (points >= cfg.thresholds[i])
            rank = static_cast<StyleRank>(i);
        else
            break;
    }
    return rank;
}

// Apply a style event: add its points (clamped at maxPoints) and recompute rank.
inline void addStyleEvent(StyleState& s, StyleEvent e, const StyleConfig& cfg = {}) {
    s.points = std::min(cfg.maxPoints, s.points + styleEventPoints(e));
    s.rank   = rankForPoints(s.points, cfg);
}

// Advance the meter by dt: bleed points toward zero and recompute rank.
inline void tickStyle(StyleState& s, float dt, const StyleConfig& cfg = {}) {
    s.points = std::max(0.f, s.points - cfg.decayPerSec * dt);
    s.rank   = rankForPoints(s.points, cfg);
}

// HUD label for a rank: "D".."SSS". Pure; never null.
inline const char* rankLabel(StyleRank rank) {
    switch (rank) {
    case StyleRank::D:
        return "D";
    case StyleRank::C:
        return "C";
    case StyleRank::B:
        return "B";
    case StyleRank::A:
        return "A";
    case StyleRank::S:
        return "S";
    case StyleRank::SS:
        return "SS";
    case StyleRank::SSS:
        return "SSS";
    }
    return "D";
}

} // namespace ds
