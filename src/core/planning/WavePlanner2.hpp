#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace at::planning {
struct WaveCandidate {
    bool startHolding = false;
    std::array<int, 8> switchSteps{};
    int switchCount = 0;
    int estimatedCost = 0;
};

// Deterministic, frame-aligned candidate expansion. Breadth and depth are
// capped before any physics clone is evaluated, so low presets stay bounded.
inline std::vector<WaveCandidate> generateWaveCandidates(
    bool currentHolding, int frameSteps, int horizon, int maxCandidates,
    int maxSwitches = 2
) {
    frameSteps = std::clamp(frameSteps, 1, 16);
    horizon = std::max(frameSteps, horizon);
    maxCandidates = std::clamp(maxCandidates, 2, 24);
    maxSwitches = std::clamp(maxSwitches, 1, 4);
    std::vector<WaveCandidate> out;
    out.reserve(static_cast<std::size_t>(maxCandidates));
    auto push = [&](bool start, std::initializer_list<int> steps) {
        if (out.size() >= static_cast<std::size_t>(maxCandidates)) return;
        WaveCandidate c;
        c.startHolding = start;
        int previous = 0;
        for (int step : steps) {
            if (step <= previous || step >= horizon || c.switchCount >= maxSwitches) continue;
            c.switchSteps[static_cast<std::size_t>(c.switchCount++)] = step;
            previous = step;
        }
        c.estimatedCost = 1 + c.switchCount;
        out.push_back(c);
    };
    push(currentHolding, {});
    push(!currentHolding, {});
    for (int frame = 1; frame <= 4 && out.size() < static_cast<std::size_t>(maxCandidates); ++frame) {
        const int at = frame * frameSteps;
        push(currentHolding, {at});
        push(!currentHolding, {at});
    }
    if (maxSwitches >= 2) {
        for (int first = 1; first <= 3 && out.size() < static_cast<std::size_t>(maxCandidates); ++first) {
            for (int second = first + 1; second <= 5 && out.size() < static_cast<std::size_t>(maxCandidates); ++second) {
                push(currentHolding, {first * frameSteps, second * frameSteps});
                push(!currentHolding, {first * frameSteps, second * frameSteps});
            }
        }
    }
    return out;
}
}
