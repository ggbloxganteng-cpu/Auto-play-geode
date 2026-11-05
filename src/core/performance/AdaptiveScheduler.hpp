#pragma once

#include "PerformanceSettings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace at::performance {

class AdaptiveScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    enum class WorkClass : std::uint8_t {
        GameplayCritical, // input/state application; must stay constant-time
        CachedTrajectory, // consume an already-built route; no search/allocation
        UI,
        TrajectoryReplan,
        GlobalPlan,
        WaveDeep,
        LevelScan,
    };

    struct Metrics {
        float averageFrameMs = 0.0f;
        std::uint32_t currentBudgetUs = 0;
        std::uint32_t deferredSlices = 0;
        std::uint32_t adaptiveReductions = 0;
        std::uint32_t emergencyYields = 0;
        bool jumpGraceActive = false;
    };

    explicit AdaptiveScheduler(Settings settings = Settings::forPreset(Preset::Low))
        : m_settings(settings), m_budgetUs(settings.frameBudgetUs),
          m_frameStart(Clock::now()), m_lastUiUpdate(m_frameStart) {
        m_settings.sanitize();
        m_budgetUs = m_settings.frameBudgetUs;
    }

    void setSettings(Settings settings) noexcept {
        settings.sanitize();
        m_settings = settings;
        m_budgetUs = std::min(m_budgetUs, m_settings.frameBudgetUs);
        if (!m_settings.adaptiveBudget) m_budgetUs = m_settings.frameBudgetUs;
    }

    const Settings& settings() const noexcept { return m_settings; }

    // Call exactly once near the start of each game update, using the previous
    // frame duration. This controller does not hook or modify the game loop.
    void beginFrame(float previousFrameMs, TimePoint now = Clock::now()) noexcept {
        m_frameStart = now;
        if (previousFrameMs >= 45.0f && previousFrameMs < 250.0f) {
            // One very slow frame can be enough to make a phone feel frozen.
            // Skip background work for the next two updates after this sample.
            m_emergencyFrames = std::max<std::uint8_t>(m_emergencyFrames, 3);
            ++m_metrics.emergencyYields;
        }
        if (previousFrameMs > 0.0f && previousFrameMs < 250.0f) {
            if (!m_hasFrameSample) {
                m_averageFrameMs = previousFrameMs;
                m_hasFrameSample = true;
            } else {
                m_averageFrameMs = (m_averageFrameMs * 0.88f) + (previousFrameMs * 0.12f);
            }
            adaptBudget();
        }
        if (m_emergencyFrames > 0) --m_emergencyFrames;
    }

    // Call on the jump edge (not every frame while jump is held).
    void notifyJump(TimePoint now = Clock::now()) noexcept {
        if (!m_settings.deferScanDuringJump || m_settings.jumpGraceMs == 0) return;
        m_jumpSafeUntil = now + std::chrono::milliseconds(m_settings.jumpGraceMs);
        m_hasJumpGrace = true;
    }

    // A severe hitch temporarily suspends only background work. Critical game
    // input and cached-route consumption are never blocked by this controller.
    void notifySevereFrameHitch() noexcept {
        m_emergencyFrames = std::max<std::uint8_t>(m_emergencyFrames, 3);
        ++m_metrics.emergencyYields;
    }

    bool mayRun(WorkClass kind, TimePoint now = Clock::now()) noexcept {
        if (kind == WorkClass::GameplayCritical || kind == WorkClass::CachedTrajectory)
            return true;

        if (kind == WorkClass::UI) {
            if (!m_settings.throttleUi) return true;
            const auto interval = std::chrono::milliseconds(m_settings.uiRefreshIntervalMs);
            if (now - m_lastUiUpdate < interval) return false;
            m_lastUiUpdate = now;
            return true;
        }

        if (isBackground(kind) && backgroundPaused(now)) {
            ++m_metrics.deferredSlices;
            return false;
        }
        if (isBackground(kind) && sliceBudgetExpired(now)) {
            ++m_metrics.deferredSlices;
            return false;
        }
        return true;
    }

    // Check between small units of work inside a planner/scanner loop.
    bool shouldYield(WorkClass kind, TimePoint now = Clock::now()) noexcept {
        if (!isBackground(kind)) return false;
        if (backgroundPaused(now) || sliceBudgetExpired(now)) {
            ++m_metrics.deferredSlices;
            return true;
        }
        return false;
    }

    Metrics metrics(TimePoint now = Clock::now()) const noexcept {
        Metrics m = m_metrics;
        m.averageFrameMs = m_averageFrameMs;
        m.currentBudgetUs = m_budgetUs;
        m.jumpGraceActive = m_hasJumpGrace && now < m_jumpSafeUntil;
        return m;
    }

private:
    static constexpr bool isBackground(WorkClass kind) noexcept {
        return kind == WorkClass::TrajectoryReplan || kind == WorkClass::GlobalPlan ||
               kind == WorkClass::WaveDeep || kind == WorkClass::LevelScan;
    }

    bool backgroundPaused(TimePoint now) const noexcept {
        return m_emergencyFrames > 0 ||
               (m_settings.deferScanDuringJump && m_hasJumpGrace && now < m_jumpSafeUntil);
    }

    bool sliceBudgetExpired(TimePoint now) const noexcept {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_frameStart).count();
        return elapsed >= static_cast<std::int64_t>(m_budgetUs);
    }

    void adaptBudget() noexcept {
        if (!m_settings.adaptiveBudget) {
            m_budgetUs = m_settings.frameBudgetUs;
            return;
        }

        const auto oldBudget = m_budgetUs;
        if (m_averageFrameMs >= 32.0f) {
            m_budgetUs = std::max<std::uint32_t>(100, m_budgetUs * 55 / 100);
        } else if (m_averageFrameMs >= 24.0f) {
            m_budgetUs = std::max<std::uint32_t>(100, m_budgetUs * 70 / 100);
        } else if (m_averageFrameMs >= 18.0f) {
            m_budgetUs = std::max<std::uint32_t>(100, m_budgetUs * 85 / 100);
        } else if (m_averageFrameMs <= 14.0f) {
            if (++m_stableFrames >= 90) {
                m_budgetUs = std::min<std::uint32_t>(m_settings.frameBudgetUs,
                                                     m_budgetUs + std::max<std::uint32_t>(10, m_budgetUs / 20));
                m_stableFrames = 0;
            }
        } else {
            m_stableFrames = 0;
        }

        if (m_budgetUs < oldBudget) ++m_metrics.adaptiveReductions;
    }

    Settings m_settings{};
    std::uint32_t m_budgetUs = 450;
    float m_averageFrameMs = 0.0f;
    bool m_hasFrameSample = false;
    std::uint16_t m_stableFrames = 0;
    std::uint8_t m_emergencyFrames = 0;
    bool m_hasJumpGrace = false;
    TimePoint m_jumpSafeUntil{};
    TimePoint m_frameStart{};
    TimePoint m_lastUiUpdate{};
    Metrics m_metrics{};
};

} // namespace at::performance
