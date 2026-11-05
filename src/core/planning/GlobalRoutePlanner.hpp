#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace at::planning {
struct RouteNode {
    int segment = 0;
    bool holding = false;
    float cumulativeRisk = 0.f;
    int switches = 0;
    std::array<bool, 4> controls{};
};
struct RoutePlan {
    bool valid = false;
    float risk = 1.f;
    int switches = 0;
    std::array<bool, 4> controls{};
    std::array<float, 4> segmentRisk{};
};

// Bounded beam search over the four checkpoint segments. Per-segment costs
// come from actual clone simulations supplied by the caller, not object counts.
inline RoutePlan planGlobalRoute(
    std::array<std::array<float, 2>, 4> const& measuredRisk,
    bool initialHolding, int beamWidth = 6, float switchPenalty = 0.015f
) {
    std::vector<RouteNode> beam(1);
    beam.front().holding = initialHolding;
    beamWidth = std::clamp(beamWidth, 1, 16);
    for (int segment = 0; segment < 4; ++segment) {
        std::vector<RouteNode> next;
        next.reserve(beam.size() * 2);
        for (auto const& node : beam) {
            for (int control = 0; control < 2; ++control) {
                RouteNode candidate = node;
                const bool hold = control != 0;
                candidate.segment = segment;
                candidate.holding = hold;
                candidate.controls[static_cast<std::size_t>(segment)] = hold;
                candidate.cumulativeRisk += std::clamp(measuredRisk[segment][control], 0.f, 1.f);
                if (segment > 0 && hold != node.holding) {
                    ++candidate.switches;
                    candidate.cumulativeRisk += switchPenalty;
                }
                next.push_back(candidate);
            }
        }
        std::sort(next.begin(), next.end(), [](RouteNode const& a, RouteNode const& b) {
            if (a.cumulativeRisk != b.cumulativeRisk) return a.cumulativeRisk < b.cumulativeRisk;
            return a.switches < b.switches;
        });
        if (next.size() > static_cast<std::size_t>(beamWidth)) next.resize(static_cast<std::size_t>(beamWidth));
        beam = std::move(next);
    }
    RoutePlan out;
    if (beam.empty()) return out;
    out.valid = true;
    out.risk = beam.front().cumulativeRisk / 4.f;
    out.switches = beam.front().switches;
    out.controls = beam.front().controls;
    for (int i = 0; i < 4; ++i) {
        out.segmentRisk[static_cast<std::size_t>(i)] = std::min(
            measuredRisk[static_cast<std::size_t>(i)][out.controls[static_cast<std::size_t>(i)] ? 1 : 0], 1.f);
    }
    return out;
}
}
