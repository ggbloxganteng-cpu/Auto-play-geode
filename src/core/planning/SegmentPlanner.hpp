#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace at::planning {
struct SegmentWindow {
    int checkpoint = 0;
    float startX = 0.f;
    float endX = 0.f;
    float distance = 0.f;
    int maxSteps = 0;
};

// Converts a checkpoint boundary into a bounded simulation window. The caller
// supplies measured horizontal progress per physics substep; no guessed speed.
inline SegmentWindow makeSegmentWindow(
    int checkpoint, float playerX, float boundaryX, float unitsPerStep,
    int hardCap, int minimumSteps = 1
) {
    SegmentWindow out;
    out.checkpoint = std::clamp(checkpoint, 0, 3);
    out.startX = playerX;
    out.endX = std::max(playerX, boundaryX);
    out.distance = out.endX - out.startX;
    const float speed = std::fabs(unitsPerStep);
    const int estimate = speed > 0.0001f
        ? static_cast<int>(std::ceil(out.distance / speed)) : hardCap;
    out.maxSteps = std::clamp(estimate, std::max(1, minimumSteps), std::max(1, hardCap));
    return out;
}

struct SegmentScore {
    int survivedSteps = 0;
    int plannedSteps = 0;
    int hazards = 0;
    int orbs = 0;
    int pads = 0;
    int portals = 0;
    bool reachedBoundary = false;
    float risk = 0.f;
};

inline float scoreSegment(SegmentScore const& s) {
    const float survival = s.plannedSteps > 0
        ? std::clamp(static_cast<float>(s.survivedSteps) / s.plannedSteps, 0.f, 1.f) : 0.f;
    const float density = static_cast<float>(s.hazards) * 1.0f +
        static_cast<float>(s.orbs) * 0.12f + static_cast<float>(s.pads) * 0.08f +
        static_cast<float>(s.portals) * 0.18f;
    return std::clamp((1.f - survival) * 0.78f + std::min(density / 80.f, 0.22f), 0.f, 1.f);
}
}
