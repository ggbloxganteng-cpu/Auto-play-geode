#pragma once

#include <algorithm>
#include <cstdint>

namespace at::performance {

enum class Preset : std::uint8_t {
    UltraLow,
    Low,
    Balanced,
    High,
    Custom,
};

struct Settings {
    Preset preset = Preset::Low;

    // Maximum extra CPU time reserved for incremental, non-critical work.
    std::uint32_t frameBudgetUs = 450;
    std::uint16_t objectsPerSlice = 5;
    std::uint8_t waveCandidates = 3;
    std::uint16_t waveHorizon = 56;
    std::uint16_t uiRefreshIntervalMs = 120;
    std::uint16_t jumpGraceMs = 180;

    bool adaptiveBudget = true;
    bool deferScanDuringJump = true;
    bool cacheStaticObjects = true;
    bool throttleUi = true;
    bool diagnostics = false;

    static constexpr Settings forPreset(Preset value) noexcept {
        Settings s{};
        s.preset = value;
        switch (value) {
            case Preset::UltraLow:
                s.frameBudgetUs = 250;
                s.objectsPerSlice = 3;
                s.waveCandidates = 2;
                s.waveHorizon = 40;
                s.uiRefreshIntervalMs = 180;
                s.jumpGraceMs = 230;
                break;
            case Preset::Low:
                s.frameBudgetUs = 450;
                s.objectsPerSlice = 5;
                s.waveCandidates = 3;
                s.waveHorizon = 56;
                s.uiRefreshIntervalMs = 120;
                s.jumpGraceMs = 180;
                break;
            case Preset::Balanced:
                s.frameBudgetUs = 800;
                s.objectsPerSlice = 8;
                s.waveCandidates = 4;
                s.waveHorizon = 80;
                s.uiRefreshIntervalMs = 75;
                s.jumpGraceMs = 140;
                break;
            case Preset::High:
                s.frameBudgetUs = 1200;
                s.objectsPerSlice = 12;
                s.waveCandidates = 6;
                s.waveHorizon = 112;
                s.uiRefreshIntervalMs = 40;
                s.jumpGraceMs = 100;
                break;
            case Preset::Custom:
                // Custom starts from a conservative Low baseline. The settings UI
                // can then edit the individual values and persist them.
                s.preset = Preset::Custom;
                s.frameBudgetUs = 450;
                s.objectsPerSlice = 5;
                s.waveCandidates = 3;
                s.waveHorizon = 56;
                s.uiRefreshIntervalMs = 120;
                s.jumpGraceMs = 180;
                break;
        }
        return s;
    }

    constexpr void sanitize() noexcept {
        frameBudgetUs = std::clamp<std::uint32_t>(frameBudgetUs, 100, 2500);
        objectsPerSlice = std::clamp<std::uint16_t>(objectsPerSlice, 1, 32);
        waveCandidates = std::clamp<std::uint8_t>(waveCandidates, 1, 8);
        waveHorizon = std::clamp<std::uint16_t>(waveHorizon, 24, 180);
        uiRefreshIntervalMs = std::clamp<std::uint16_t>(uiRefreshIntervalMs, 30, 500);
        jumpGraceMs = std::clamp<std::uint16_t>(jumpGraceMs, 0, 500);
    }
};

} // namespace at::performance
