#pragma once

#include <span>

namespace ds {

// Pure boss phase logic (task 40). The boss advances through phases as its
// health crosses a set of DESCENDING fractional thresholds (e.g. {0.66, 0.33,
// 0.0}); phase 0 is full health, and each crossed threshold bumps the phase by
// one. Kept free of engine/SDL/Jolt state so it can be unit-tested in isolation.
//
// `current` / `max` are integer health; `thresholds` are health fractions in
// (1,0] ordered high-to-low. The returned phase is the count of thresholds the
// current health fraction is at or below, clamped to [0, thresholds.size()].
inline int bossPhaseForHealth(int current, int max, std::span<const float> thresholds) {
    if (max <= 0)
        return static_cast<int>(thresholds.size());
    int c      = current < 0 ? 0 : current;
    float frac = static_cast<float>(c) / static_cast<float>(max);
    int phase  = 0;
    for (float t : thresholds) {
        if (frac <= t)
            ++phase;
    }
    return phase;
}

} // namespace ds
