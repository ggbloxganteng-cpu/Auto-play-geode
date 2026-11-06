#pragma once

#include "PerformanceSettings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace at::performance {

constexpr std::size_t kStrategyPresetCount = 4;

constexpr Preset strategyPresetFromIndex(int index) noexcept {
    switch (index) {
        case 0: return Preset::UltraLow;
        case 1: return Preset::Low;
        case 2: return Preset::Balanced;
        case 3: return Preset::High;
        default: return Preset::Low;
    }
}

constexpr int strategyPresetIndex(Preset preset) noexcept {
    switch (preset) {
        case Preset::UltraLow: return 0;
        case Preset::Low: return 1;
        case Preset::Balanced: return 2;
        case Preset::High: return 3;
        case Preset::Custom: return 1;
    }
    return 1;
}

struct PresetTrialStats {
    std::uint8_t attempts = 0;
    std::uint16_t totalProgress = 0;
    std::uint8_t bestProgress = 0;
    std::uint8_t predictionMisses = 0;

    constexpr int averageProgress() const noexcept {
        return attempts == 0 ? 0 : totalProgress / attempts;
    }
};

// Small persistent per-level learner. It first samples each strategy once,
// starting with the user's fallback, then chooses the best observed progress.
// Ties prefer the cheaper search profile. This is intentionally independent
// from AdaptiveScheduler: device frame-time budget remains a separate control.
class LevelStrategyProfile {
public:
    void load(int index, std::int64_t packed) noexcept {
        if (index < 0 || index >= static_cast<int>(kStrategyPresetCount)) return;
        auto raw = static_cast<std::uint64_t>(packed);
        auto& stats = m_stats[static_cast<std::size_t>(index)];
        stats.attempts = static_cast<std::uint8_t>(raw & 0xffu);
        stats.totalProgress = static_cast<std::uint16_t>((raw >> 8) & 0xffffu);
        stats.bestProgress = static_cast<std::uint8_t>((raw >> 24) & 0x7fu);
        stats.predictionMisses = static_cast<std::uint8_t>((raw >> 31) & 0xffu);
        stats.bestProgress = std::min<std::uint8_t>(stats.bestProgress, 100);
    }

    constexpr std::int64_t save(int index) const noexcept {
        if (index < 0 || index >= static_cast<int>(kStrategyPresetCount)) return 0;
        auto const& stats = m_stats[static_cast<std::size_t>(index)];
        const std::uint64_t packed =
            static_cast<std::uint64_t>(stats.attempts) |
            (static_cast<std::uint64_t>(stats.totalProgress) << 8) |
            (static_cast<std::uint64_t>(stats.bestProgress) << 24) |
            (static_cast<std::uint64_t>(stats.predictionMisses) << 31);
        return static_cast<std::int64_t>(packed);
    }

    void record(Preset preset, int progress, bool predictionMismatch) noexcept {
        const int index = strategyPresetIndex(preset);
        auto& stats = m_stats[static_cast<std::size_t>(index)];
        const auto safeProgress = static_cast<std::uint8_t>(std::clamp(progress, 0, 100));
        if (stats.attempts < 255) ++stats.attempts;
        stats.totalProgress = static_cast<std::uint16_t>(std::min<unsigned>(
            65535u, static_cast<unsigned>(stats.totalProgress) + safeProgress
        ));
        stats.bestProgress = std::max(stats.bestProgress, safeProgress);
        if (predictionMismatch && stats.predictionMisses < 255) ++stats.predictionMisses;
    }

    Preset chooseNext(Preset fallback) const noexcept {
        const int start = strategyPresetIndex(fallback);
        for (int offset = 0; offset < static_cast<int>(kStrategyPresetCount); ++offset) {
            const int index = (start + offset) % static_cast<int>(kStrategyPresetCount);
            if (m_stats[static_cast<std::size_t>(index)].attempts == 0)
                return strategyPresetFromIndex(index);
        }

        int bestIndex = 0;
        double bestScore = -1.0;
        for (int index = 0; index < static_cast<int>(kStrategyPresetCount); ++index) {
            auto const& stats = m_stats[static_cast<std::size_t>(index)];
            if (stats.attempts == 0) continue;
            const double average = static_cast<double>(stats.totalProgress) / stats.attempts;
            const double mismatchRate = static_cast<double>(stats.predictionMisses) / stats.attempts;
            const double score = average - mismatchRate * 8.0;
            // Strict comparison deliberately preserves the lower-cost preset on ties.
            if (score > bestScore + 0.001) {
                bestScore = score;
                bestIndex = index;
            }
        }
        return strategyPresetFromIndex(bestIndex);
    }

    constexpr PresetTrialStats const& stats(int index) const noexcept {
        return m_stats[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    }

private:
    std::array<PresetTrialStats, kStrategyPresetCount> m_stats{};
};

} // namespace at::performance
