#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <unordered_set>
#include <vector>
#include <string>

#include "core/LevelScan.hpp"
#include "ui/FloatingPanelTouch.hpp"
#include "core/performance/AdaptiveScheduler.hpp"

using namespace geode::prelude;

namespace {
    using at::performance::AdaptiveScheduler;
    using at::performance::Preset;
    using at::performance::Settings;

    AdaptiveScheduler* g_performanceScheduler = nullptr;

    Preset performancePreset() {
        const auto value = Mod::get()->getSettingValue<int64_t>("performance-preset");
        switch (value) {
            case 0: return Preset::UltraLow;
            case 1: return Preset::Low;
            case 2: return Preset::Balanced;
            case 3: return Preset::High;
            default: return Preset::Custom;
        }
    }

    Settings performanceSettings() {
        auto settings = Settings::forPreset(performancePreset());
        settings.adaptiveBudget = Mod::get()->getSettingValue<bool>("adaptive-budget");
        settings.deferScanDuringJump = Mod::get()->getSettingValue<bool>("jump-protection");
        settings.diagnostics = Mod::get()->getSettingValue<bool>("performance-diagnostics");
        settings.sanitize();
        return settings;
    }

    const char* performancePresetName(Preset preset) {
        switch (preset) {
            case Preset::UltraLow: return "ULTRA LOW";
            case Preset::Low: return "LOW";
            case Preset::Balanced: return "BALANCED";
            case Preset::High: return "HIGH";
            default: return "CUSTOM";
        }
    }

    bool autoPlayEnabled() {
        return Mod::get()->getSettingValue<bool>("auto-play");
    }

    bool trajectoryEnabled() {
        return Mod::get()->getSettingValue<bool>("show-trajectory");
    }

    bool uiEnabled() {
        return Mod::get()->getSettingValue<bool>("show-ui");
    }

    bool waveRoomAwareEnabled() {
        return Mod::get()->getSettingValue<bool>("wave-room-aware");
    }

    bool waveDeepSearchEnabled() {
        return Mod::get()->getSettingValue<bool>("wave-deep-search");
    }

    bool debugLogEnabled() {
        return Mod::get()->getSettingValue<bool>("debug-log");
    }

    int trajectorySteps() {
        return static_cast<int>(Mod::get()->getSettingValue<int64_t>("trajectory-steps"));
    }

    int autoPlayLookahead() {
        return static_cast<int>(Mod::get()->getSettingValue<int64_t>("autoplay-lookahead"));
    }
}

#include "core/Simulation.hpp"

class $modify(AutoTrajPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);

        if (g_simulating) return;

        if (auto pl = PlayLayer::get(); pl && pl->m_player1 == this) {
            ++g_stepCounter;
            if (dt > 0.001f && dt < 2.f) {
                float warp = pl->m_gameState.m_timeWarp;
                if (!(warp > 0.f)) warp = 1;
                g_stepDt = dt / warp;
            }
        }
    }

    void incrementJumps() {
        // Jump counters belong to the real PlayLayer; do not increment them
        // while a predictive clone is being evaluated.
        if (g_simulating) return;
        if (g_performanceScheduler) g_performanceScheduler->notifyJump();
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool unk) {
        if (g_simulating && !isSimDummy(this)) return;
        if (g_simulating) {
            snapshotSimObject(ring);
            recordSimOrbActivation(ring, g_simCurrentStep);
        }
        PlayerObject::ringJump(ring, unk);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (g_simulating) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }
};

class $modify(AutoTrajStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        if (g_simulating) return;
        HardStreak::addPoint(point);
    }
};

class $modify(AutoTrajEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (g_simulating) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(AutoTrajGameObject, GameObject) {
    void playShineEffect() {
        if (g_simulating) return;
        GameObject::playShineEffect();
    }
};

class $modify(AutoTrajBaseLayer, GJBaseGameLayer) {
    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (g_simulating) {
            if (!isSimDummy(player) || !isSimInteractable(object)) return false;
            snapshotSimObject(object);

            // A hidden clone is not one of PlayLayer's registered players, so
            // the base gate can reject an otherwise valid RingObject. Let the
            // standard orb interaction through for the clone, while respecting
            // the per-player activation flag in dual mode. The real game path
            // is untouched because this branch only runs during prediction.
            if (auto* ring = typeinfo_cast<RingObject*>(object)) {
                return ringCanActivateForDummy(ring, player, g_simInputDown);
            }
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (g_simulating) {
            if (!isSimDummy(player) || !ring) return;
            snapshotSimObject(ring);
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        if (g_simulating && isSimDummy(player) && objects) {
            gd::vector<GameObject*> filtered;
            filtered.reserve(count);
            auto playerRect = player->getObjectRect();
            for (int i = 0; i < count; ++i) {
                auto* object = objects->at(i);
                if (!object) continue;

                // Record actual hitbox contact even when Jump is released. This
                // gives the planner a real contact window to schedule around.
                if (auto* ring = typeinfo_cast<RingObject*>(object)) {
                    if (playerRect.intersectsRect(object->getObjectRect())) {
                        recordSimOrbContact(ring, g_simCurrentStep);
                    }
                }

                // Teleport orbs depend on linked destination / trigger state,
                // which the clone intentionally does not mutate. Stop exactly
                // at their hitbox and let the next real frame re-sync.
                if (isTeleportOrb(object)) {
                    if (playerRect.intersectsRect(object->getObjectRect())) {
                        snapshotSimObject(object);
                        g_simPortal = true;
                        continue;
                    }
                }

                // Use actual hitbox intersection rather than a loose centre-point
                // radius. Supported portals are passed to the game's collision
                // code so the clone receives the real portal transition.
                if (isPortalId(object->m_objectID) &&
                    playerRect.intersectsRect(object->getObjectRect())) {
                    snapshotSimObject(object);
                    if (requiresPortalResync(object->m_objectID)) {
                        // Stop at the portal instead of pretending the old mode
                        // continues. The real player crosses it in the live tick;
                        // the next frame clones the newly transformed state.
                        g_simPortal = true;
                        continue;
                    }
                    filtered.push_back(object);
                    continue;
                }

                switch (object->m_objectType) {
                    case GameObjectType::Solid:
                    case GameObjectType::Hazard:
                    case GameObjectType::AnimatedHazard:
                    case GameObjectType::CollisionObject:
                    case GameObjectType::Slope:
                        filtered.push_back(object);
                        break;
                    default:
                        if (isSimInteractable(object)) {
                            snapshotSimObject(object);
                            filtered.push_back(object);
                        }
                        break;
                }
            }

            GJBaseGameLayer::collisionCheckObjects(
                player, &filtered, static_cast<int>(filtered.size()), dt
            );
            return;
        }

        GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
    }
};

class $modify(AutoTrajPlayLayer, PlayLayer) {
    struct Fields {
        cocos2d::CCDrawNode* drawNode = nullptr;
        PlayerObject* dummy1 = nullptr;
        PlayerObject* dummy2 = nullptr;
        cocos2d::CCLayerColor* panel = nullptr;
        at::ui::FloatingPanelTouchLayer* panelTouch = nullptr;
        cocos2d::CCMenu* menu = nullptr;
        cocos2d::CCLabelBMFont* headerLabel = nullptr;
        cocos2d::CCLabelBMFont* scanLabel = nullptr;
        cocos2d::CCLabelBMFont* autoLabel = nullptr;
        cocos2d::CCLabelBMFont* trajLabel = nullptr;
        cocos2d::CCLabelBMFont* waveLabel = nullptr;
        cocos2d::CCLabelBMFont* performanceLabel = nullptr;
        cocos2d::CCLabelBMFont* adaptiveLabel = nullptr;
        cocos2d::CCLabelBMFont* metricsLabel = nullptr;
        AdaptiveScheduler scheduler{performanceSettings()};
        at::LevelScan levelScan;
        int lastCheckpoint = -1;
        int lastScanPercent = -1;
        bool panelCollapsed = false;
        bool panelDragActive = false;
        bool panelDragMoved = false;
        cocos2d::CCPoint panelTouchStart = {0.f, 0.f};
        cocos2d::CCPoint panelPositionStart = {0.f, 0.f};
        bool botHolding = false;
        bool waveTransitionActive = false;
        int waveStepsUntilTransition = 0;
        bool waveTransitionTarget = false;
        bool botTapActive = false;
        int botTapStepsRemaining = 0;
        bool orbTapScheduled = false;
        int orbTapStepsRemaining = 0;
        bool timedHoldActive = false;
        int timedHoldStepsRemaining = 0;
        bool timedHoldRobotMode = false;
        bool loggedSetup = false;
        int logFrame = 0;
        int performanceLogFrame = 0;
        int lastLoggedAction = -1;
        bool lastAuto = false;
        bool lastTraj = true;
        bool lastUi = true;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        m_fields->scheduler.setSettings(performanceSettings());
        g_performanceScheduler = &m_fields->scheduler;

        if (m_objectLayer) {
            auto* draw = cocos2d::CCDrawNode::create();
            draw->setID("autotraj-draw-node"_spr);
            m_objectLayer->addChild(draw, 1000);
            m_fields->drawNode = draw;
        }

        m_fields->dummy1 = PlayerObject::create(1, 1, this, this, true);
        if (m_fields->dummy1 && m_objectLayer) {
            m_fields->dummy1->setVisible(false);
            m_fields->dummy1->setID("autotraj-dummy-1"_spr);
            m_fields->dummy1->retain();
            m_objectLayer->addChild(m_fields->dummy1, 999);
        } else {
            m_fields->dummy1 = nullptr;
        }

        // Second hidden player for dual mode.
        m_fields->dummy2 = PlayerObject::create(1, 1, this, this, false);
        if (m_fields->dummy2 && m_objectLayer) {
            m_fields->dummy2->setVisible(false);
            m_fields->dummy2->setID("autotraj-dummy-2"_spr);
            m_fields->dummy2->retain();
            m_objectLayer->addChild(m_fields->dummy2, 999);
        } else {
            m_fields->dummy2 = nullptr;
        }

        this->scanLevel();
        this->setupFloatingPanel();
        m_fields->botHolding = false;
        m_fields->botTapActive = false;
        m_fields->botTapStepsRemaining = 0;
        m_fields->orbTapScheduled = false;
        m_fields->orbTapStepsRemaining = 0;
        m_fields->timedHoldActive = false;
        m_fields->timedHoldStepsRemaining = 0;
        m_fields->timedHoldRobotMode = false;
        m_fields->loggedSetup = false;
        g_warnedStepZero = false;

        return true;
    }

    void scanLevel() {
        std::vector<at::ObjectRecord> records;
        records.reserve(m_objects ? m_objects->count() : 0);
        if (m_objects) {
            for (unsigned i = 0; i < m_objects->count(); ++i) {
                auto* object = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                if (!object) continue;
                at::ObjectClass category = at::ObjectClass::Other;
                if (typeinfo_cast<RingObject*>(object)) {
                    category = at::ObjectClass::Orb;
                } else {
                    switch (object->m_objectType) {
                        case GameObjectType::Hazard:
                        case GameObjectType::AnimatedHazard:
                        case GameObjectType::CollisionObject:
                        case GameObjectType::Slope:
                            category = at::ObjectClass::Hazard;
                            break;
                        case GameObjectType::YellowJumpPad:
                        case GameObjectType::PinkJumpPad:
                        case GameObjectType::RedJumpPad:
                        case GameObjectType::GravityPad:
                        case GameObjectType::SpiderPad:
                            category = at::ObjectClass::Pad;
                            break;
                        default:
                            if (isPortalId(object->m_objectID)) category = at::ObjectClass::Portal;
                            break;
                    }
                }
                records.push_back({object->getPositionX(), object->m_objectID, category});
            }
        }
        m_fields->levelScan.build(std::move(records), m_player1 ? m_player1->getPositionX() : 0.f);
        m_fields->lastCheckpoint = -1;
        m_fields->lastScanPercent = -1;
        log::info("[AT] level scan: {} objects, x={:.1f}..{:.1f}",
            m_fields->levelScan.objectCount(), m_fields->levelScan.minX(),
            m_fields->levelScan.maxX());
    }

    void updatePanelLayout() {
        if (!m_fields->panel) return;
        const float height = m_fields->panelCollapsed ? 34.f : 156.f;
        m_fields->panel->setContentSize({190.f, height});
        if (m_fields->headerLabel) {
            m_fields->headerLabel->setPosition({95.f, height - 13.f});
            m_fields->headerLabel->setString(
                m_fields->panelCollapsed ? "AUTO TRAJECTORY  [+]" : "AUTO TRAJECTORY  [drag / hide]"
            );
        }
        if (m_fields->scanLabel) {
            m_fields->scanLabel->setVisible(!m_fields->panelCollapsed);
            m_fields->scanLabel->setPosition({95.f, 126.f});
        }
        if (m_fields->metricsLabel) {
            m_fields->metricsLabel->setVisible(!m_fields->panelCollapsed);
            m_fields->metricsLabel->setPosition({95.f, 108.f});
        }
        if (m_fields->menu) m_fields->menu->setVisible(!m_fields->panelCollapsed);
    }

    void setupFloatingPanel() {
        auto panel = cocos2d::CCLayerColor::create(
            cocos2d::ccColor4B{13, 18, 26, 232}, 190.f, 156.f
        );
        if (!panel) return;
        panel->setAnchorPoint({0.f, 0.f});
        panel->setID("autotraj-floating-panel"_spr);
        panel->setPosition({112.f, 105.f});
        this->addChild(panel, 1001);
        m_fields->panel = panel;

        auto header = cocos2d::CCLabelBMFont::create("AUTO TRAJECTORY  [drag / hide]", "bigFont.fnt");
        header->setScale(0.38f);
        header->setPosition({95.f, 143.f});
        panel->addChild(header, 2);
        m_fields->headerLabel = header;

        auto scan = cocos2d::CCLabelBMFont::create("ROUTE SCAN: READY", "bigFont.fnt");
        scan->setScale(0.32f);
        scan->setPosition({95.f, 126.f});
        panel->addChild(scan, 2);
        m_fields->scanLabel = scan;

        auto metrics = cocos2d::CCLabelBMFont::create("PERF: LOW | BUDGET: 450us", "bigFont.fnt");
        metrics->setScale(0.27f);
        metrics->setPosition({95.f, 108.f});
        panel->addChild(metrics, 2);
        m_fields->metricsLabel = metrics;

        auto autoLabel = cocos2d::CCLabelBMFont::create("AUTO PLAY: OFF", "bigFont.fnt");
        autoLabel->setScale(0.45f);
        auto autoItem = cocos2d::CCMenuItemLabel::create(
            autoLabel, this, menu_selector(AutoTrajPlayLayer::onToggleAutoPlay)
        );
        auto trajLabel = cocos2d::CCLabelBMFont::create("TRAJECTORY: ON", "bigFont.fnt");
        trajLabel->setScale(0.45f);
        auto trajItem = cocos2d::CCMenuItemLabel::create(
            trajLabel, this, menu_selector(AutoTrajPlayLayer::onToggleTrajectory)
        );
        auto waveLabel = cocos2d::CCLabelBMFont::create("WAVE PLAN: OFF", "bigFont.fnt");
        waveLabel->setScale(0.45f);
        auto waveItem = cocos2d::CCMenuItemLabel::create(
            waveLabel, this, menu_selector(AutoTrajPlayLayer::onToggleWavePlanner)
        );
        auto perfLabel = cocos2d::CCLabelBMFont::create("PERF PRESET: LOW  >", "bigFont.fnt");
        perfLabel->setScale(0.43f);
        auto perfItem = cocos2d::CCMenuItemLabel::create(
            perfLabel, this, menu_selector(AutoTrajPlayLayer::onCyclePerformancePreset)
        );
        auto adaptiveLabel = cocos2d::CCLabelBMFont::create("ADAPTIVE BUDGET: ON", "bigFont.fnt");
        adaptiveLabel->setScale(0.43f);
        auto adaptiveItem = cocos2d::CCMenuItemLabel::create(
            adaptiveLabel, this, menu_selector(AutoTrajPlayLayer::onToggleAdaptiveBudget)
        );
        auto menu = cocos2d::CCMenu::create(
            autoItem, trajItem, waveItem, perfItem, adaptiveItem, nullptr
        );
        menu->alignItemsVerticallyWithPadding(1.f);
        menu->setPosition({95.f, 53.f});
        menu->setScale(0.68f);
        menu->setID("autotraj-menu"_spr);
        panel->addChild(menu, 2);

        m_fields->menu = menu;
        m_fields->autoLabel = autoLabel;
        m_fields->trajLabel = trajLabel;
        m_fields->waveLabel = waveLabel;
        m_fields->performanceLabel = perfLabel;
        m_fields->adaptiveLabel = adaptiveLabel;
        m_fields->lastAuto = autoPlayEnabled();
        m_fields->lastTraj = trajectoryEnabled();
        m_fields->lastUi = uiEnabled();
        autoLabel->setString(m_fields->lastAuto ? "AUTO PLAY: ON" : "AUTO PLAY: OFF");
        trajLabel->setString(m_fields->lastTraj ? "TRAJECTORY: ON" : "TRAJECTORY: OFF");
        waveLabel->setString(waveRoomAwareEnabled() ? "WAVE PLAN: ON" : "WAVE PLAN: OFF");
        perfLabel->setString(fmt::format("PERF PRESET: {}  >", performancePresetName(performancePreset())).c_str());
        adaptiveLabel->setString(Mod::get()->getSettingValue<bool>("adaptive-budget") ? "ADAPTIVE BUDGET: ON" : "ADAPTIVE BUDGET: OFF");
        panel->setVisible(m_fields->lastUi);
        this->updatePanelLayout();

        // The touch delegate only captures the header; menu buttons and the
        // rest of the game keep their regular touch handling.
        auto touchLayer = at::ui::FloatingPanelTouchLayer::create(
            this, panel, &m_fields->panelCollapsed,
            [this]() { this->updatePanelLayout(); }
        );
        if (touchLayer) {
            touchLayer->setID("autotraj-panel-touch"_spr);
            this->addChild(touchLayer, 1002);
            m_fields->panelTouch = touchLayer;
        }
    }

    void onToggleAutoPlay(CCObject*) {
        bool value = !autoPlayEnabled();
        Mod::get()->setSettingValue<bool>("auto-play", value);
    }

    void onToggleTrajectory(CCObject*) {
        bool value = !trajectoryEnabled();
        Mod::get()->setSettingValue<bool>("show-trajectory", value);
    }

    void onCyclePerformancePreset(CCObject*) {
        auto value = Mod::get()->getSettingValue<int64_t>("performance-preset");
        value = (value + 1) % 4;
        Mod::get()->setSettingValue<int64_t>("performance-preset", value);
        m_fields->scheduler.setSettings(performanceSettings());
        if (m_fields->performanceLabel) {
            m_fields->performanceLabel->setString(
                fmt::format("PERF PRESET: {}  >", performancePresetName(performancePreset())).c_str()
            );
        }
    }

    void onToggleAdaptiveBudget(CCObject*) {
        const bool value = !Mod::get()->getSettingValue<bool>("adaptive-budget");
        Mod::get()->setSettingValue<bool>("adaptive-budget", value);
        m_fields->scheduler.setSettings(performanceSettings());
        if (m_fields->adaptiveLabel) {
            m_fields->adaptiveLabel->setString(value ? "ADAPTIVE BUDGET: ON" : "ADAPTIVE BUDGET: OFF");
        }
    }

    void onToggleWavePlanner(CCObject*) {
        bool value = !waveRoomAwareEnabled();
        Mod::get()->setSettingValue<bool>("wave-room-aware", value);
        this->cancelWaveTransition();
        if (m_fields->waveLabel) {
            m_fields->waveLabel->setString(value ? "WAVE PLAN: ON" : "WAVE PLAN: OFF");
        }
    }

    void releaseBot() {
        if (m_fields->botHolding && m_player1) {
            this->handleButton(false, 1, true);
        }
        m_fields->botHolding = false;
    }

    void pressBot() {
        if (!m_fields->botHolding && m_player1) {
            this->handleButton(true, 1, true);
            m_fields->botHolding = true;
        }
    }

    void cancelTap() {
        m_fields->botTapActive = false;
        m_fields->botTapStepsRemaining = 0;
    }

    void cancelOrbTapSchedule() {
        m_fields->orbTapScheduled = false;
        m_fields->orbTapStepsRemaining = 0;
    }

    void cancelWaveTransition() {
        m_fields->waveTransitionActive = false;
        m_fields->waveStepsUntilTransition = 0;
        m_fields->waveTransitionTarget = false;
    }

    void cancelTimedHold() {
        m_fields->timedHoldActive = false;
        m_fields->timedHoldStepsRemaining = 0;
        m_fields->timedHoldRobotMode = false;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        if (g_simulating && isSimDummy(player)) {
            g_simDead = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
        if (player == m_player1) {
            m_fields->botHolding = false;
            this->cancelTap();
            this->cancelOrbTapSchedule();
            this->cancelWaveTransition();
            this->cancelTimedHold();
        }
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        m_fields->botHolding = false;
        this->cancelTap();
        this->cancelOrbTapSchedule();
        this->cancelWaveTransition();
        this->cancelTimedHold();
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) {
        if (g_simulating) return;
        PlayLayer::playEndAnimationToPos(pos);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (g_simulating && !isSimDummy(player)) return;
        PlayLayer::flipGravity(player, flip, noEffects);
    }

    void postUpdate(float dt) override {
        PlayLayer::postUpdate(dt);

        m_fields->scheduler.setSettings(performanceSettings());
        m_fields->scheduler.beginFrame(dt > 0.f ? dt * 1000.f : 0.f);
        const auto perfMetrics = m_fields->scheduler.metrics();
        if (perfMetrics.diagnostics && ++m_fields->performanceLogFrame % 120 == 0) {
            log::info("[AT PERF] preset={} avgFrame={:.2f}ms budget={}us deferred={} adaptiveCuts={} emergencyYields={} jumpGrace={}",
                performancePresetName(performancePreset()), perfMetrics.averageFrameMs,
                perfMetrics.currentBudgetUs, perfMetrics.deferredSlices,
                perfMetrics.adaptiveReductions, perfMetrics.emergencyYields,
                perfMetrics.jumpGraceActive);
        }
        if (m_fields->metricsLabel && m_fields->scheduler.mayRun(AdaptiveScheduler::WorkClass::UI)) {
            m_fields->metricsLabel->setString(fmt::format(
                "{} | {:d}us | {:.1f}ms{}",
                performancePresetName(performancePreset()), perfMetrics.currentBudgetUs,
                perfMetrics.averageFrameMs, perfMetrics.jumpGraceActive ? " | JUMP SAFE" : ""
            ).c_str());
        }

        if (g_stepCounter > 0) {
            g_frameSubsteps = std::clamp(g_stepCounter, 1, 16);
        }
        g_stepCounter = 0;

        const bool autoOn = autoPlayEnabled();
        const bool trajOn = trajectoryEnabled();
        const bool uiOn = uiEnabled();

        if (autoOn != m_fields->lastAuto) {
            if (m_fields->autoLabel) {
                m_fields->autoLabel->setString(autoOn ? "AUTO PLAY: ON" : "AUTO PLAY: OFF");
            }
            m_fields->lastAuto = autoOn;
        }

        if (trajOn != m_fields->lastTraj) {
            if (m_fields->trajLabel) {
                m_fields->trajLabel->setString(trajOn ? "TRAJECTORY: ON" : "TRAJECTORY: OFF");
            }
            if (m_fields->drawNode) {
                m_fields->drawNode->setVisible(trajOn);
            }
            m_fields->lastTraj = trajOn;
        }

        if (uiOn != m_fields->lastUi) {
            if (m_fields->panel) {
                m_fields->panel->setVisible(uiOn);
            }
            m_fields->lastUi = uiOn;
        }

        const bool drawDue = trajOn && m_fields->scheduler.mayRun(
            AdaptiveScheduler::WorkClass::TrajectoryReplan
        );
        if (m_fields->drawNode) {
            if (drawDue) m_fields->drawNode->clear();
            m_fields->drawNode->setVisible(trajOn);
        }

        if (!m_fields->loggedSetup) {
            m_fields->loggedSetup = true;
            log::info(
                "Auto Trajectory active: tick dt {:.4f}, preview {}, lookahead {}",
                g_stepDt, trajectorySteps(), autoPlayLookahead()
            );
        }

        if (!m_player1 || m_player1->m_isDead || !m_fields->dummy1) {
            this->releaseBot();
            this->cancelTap();
            this->cancelOrbTapSchedule();
            this->cancelWaveTransition();
            this->cancelTimedHold();
            return;
        }

        // Platformer levels are not supported (no fixed forward motion).
        if (m_player1->m_isPlatformer) {
            this->releaseBot();
            this->cancelTap();
            this->cancelOrbTapSchedule();
            this->cancelWaveTransition();
            this->cancelTimedHold();
            return;
        }

        if (!m_fields->levelScan.valid() && m_objects && m_objects->count() > 0) {
            this->scanLevel();
        }
        if (m_fields->levelScan.valid()) {
            const float playerX = m_player1->getPositionX();
            const int checkpoint = m_fields->levelScan.checkpointFor(playerX);
            const int percent = m_fields->levelScan.progressPercent(playerX);
            auto const* summary = m_fields->levelScan.checkpoint(checkpoint);
            if (percent != m_fields->lastScanPercent && m_fields->scanLabel) {
                m_fields->lastScanPercent = percent;
                auto scanText = fmt::format(
                    "ROUTE {:d}% | ZONE {:d}/4 | {} OBJECTS",
                    percent, checkpoint + 1, summary ? summary->objects : 0
                );
                m_fields->scanLabel->setString(scanText.c_str());
            }
            if (checkpoint != m_fields->lastCheckpoint) {
                m_fields->lastCheckpoint = checkpoint;
                if (summary) {
                    log::info(
                        "[AT] checkpoint {} ({}%): objects={} hazards={} orbs={} pads={} portals={}",
                        checkpoint + 1, checkpoint * 25, summary->objects,
                        summary->hazards, summary->orbs, summary->pads, summary->portals
                    );
                }
            }
        }

        // With Auto Play disabled, the overlay is the only reason to simulate.
        // Skip the whole predictive pass on throttled frames and during jump grace.
        if (!autoOn && !drawDue) return;

        float dtStep = g_stepDt;
        if (!(dtStep > 0.001f) || dtStep > 2.f) {
            dtStep = 0.25f;
        }

        // The overlay length is purely visual. The bot has its own lookahead,
        // so changing one never changes how the other behaves.
        const int presetHorizon = performancePreset() == Preset::UltraLow ? 72 :
            (performancePreset() == Preset::Low ? 120 :
            (performancePreset() == Preset::Balanced ? 200 : 320));
        int drawSteps = drawDue ? std::min(std::clamp(trajectorySteps(), 10, 400), presetHorizon) : 0;
        int botSteps = autoOn ? std::min(std::clamp(autoPlayLookahead(), 24, 480), presetHorizon) : 0;
        int horizon = std::max(drawSteps, botSteps);

        if (horizon <= 0) {
            this->releaseBot();
            this->cancelTap();
            this->cancelOrbTapSchedule();
            this->cancelWaveTransition();
            this->cancelTimedHold();
            return;
        }

        SimRig rig;
        rig.pl = this;
        rig.dummy = m_fields->dummy1;
        rig.source = m_player1;
        rig.dt = dtStep;
        rig.roomAwareWave = waveRoomAwareEnabled();
        // Deep search is intentionally disabled on low-end presets and during
        // the jump grace window to avoid candidate bursts near input events.
        rig.deepWaveSearch = waveDeepSearchEnabled() &&
            (performancePreset() == Preset::High || performancePreset() == Preset::Custom) &&
            !perfMetrics.jumpGraceActive;
        if (m_gameState.m_isDualMode && m_player2 && m_fields->dummy2 && !m_player2->m_isDead) {
            rig.dummy2 = m_fields->dummy2;
            rig.source2 = m_player2;
        }

        bool timedHoldForced = false;
        bool timedHoldJustReleased = false;
        if (m_fields->timedHoldActive) {
            const bool modeChanged = !m_player1->m_isRobot ||
                m_player1->m_isRobot != m_fields->timedHoldRobotMode;
            if (modeChanged) {
                this->cancelTimedHold();
                this->releaseBot();
                timedHoldJustReleased = true;
            } else {
                m_fields->timedHoldStepsRemaining -= std::max(1, g_frameSubsteps);
                if (m_fields->timedHoldStepsRemaining <= 0) {
                    this->releaseBot();
                    this->cancelTimedHold();
                    // Re-plan in this same postUpdate. Skipping one extra frame
                    // here can miss a chained Robot jump or a landing window.
                } else {
                    timedHoldForced = true;
                }
            }
        }

        auto* drawNode = drawSteps > 0 ? m_fields->drawNode : nullptr;

        // Green: press & hold now (overlay only).
        SimResult holdResult{};
        if (drawSteps > 0) {
            holdResult = runSimulation(
                rig, SimPlan::Hold, 0, -1, drawSteps, drawSteps,
                drawNode, cocos2d::ccColor4F{0.15f, 1.f, 0.35f, 1.f}
            );
        }

        // Red: release now. Shared with the bot's "is it dangerous?" check.
        SimResult idleResult = runSimulation(
            rig, SimPlan::Release, 0, -1, horizon, drawSteps,
            drawNode, cocos2d::ccColor4F{1.f, 0.3f, 0.3f, 1.f}
        );

        if (trajOn && holdResult.died && idleResult.died &&
            holdResult.survived == 0 && idleResult.survived == 0 && !g_warnedStepZero) {
            g_warnedStepZero = true;
            log::warn(
                "Auto Trajectory: simulation dies at step 0 near x={:.1f} y={:.1f}",
                m_player1->getPosition().x, m_player1->getPosition().y
            );
        }

        // Every Tap is a single-frame pulse. Release it at this frame boundary
        // and re-plan from the actual player state; do not hold indefinitely
        // while waiting for a landing or a takeoff that may never happen.
        if (m_fields->botTapActive &&
            advanceTapPulse(m_fields->botTapStepsRemaining, g_frameSubsteps)) {
            this->releaseBot();
            this->cancelTap();
        }

        bool orbTapForced = false;
        BotAction orbTapForcedAction = BotAction::Release;
        if (m_fields->orbTapScheduled) {
            const bool clickDue = advanceOrbTapSchedule(
                m_fields->orbTapStepsRemaining, g_frameSubsteps
            );
            if (clickDue) {
                this->cancelOrbTapSchedule();
                orbTapForced = true;
                orbTapForcedAction = BotAction::Tap;
            } else if (!orbTapScheduleStillReachable(
                idleResult, m_fields->orbTapStepsRemaining
            )) {
                // The predicted release path now dies before the scheduled click;
                // discard the stale timing and let the controller re-plan.
                this->cancelOrbTapSchedule();
            } else {
                orbTapForced = true;
                orbTapForcedAction = BotAction::Release;
            }
        }

        if (autoOn) {
            BotAction action = BotAction::Release;
            const bool scheduledWave = m_player1->m_isDart && rig.roomAwareWave;
            bool waveDidSwitch = false;
            if (!scheduledWave && m_fields->waveTransitionActive) {
                this->cancelWaveTransition();
            }
            int selectedTimedHoldSteps = 0;
            int selectedOrbTapDelaySteps = 0;
            if (orbTapForced) {
                action = orbTapForcedAction;
            } else if (timedHoldForced) {
                action = BotAction::Hold;
            } else if (timedHoldJustReleased) {
                action = BotAction::Release;
            } else if (scheduledWave && m_fields->waveTransitionActive) {
                // Execute the first planned switch at a frame boundary. Recheck
                // the current straight segment; cancel the schedule if the live
                // state is no longer predicted to survive until that switch.
                m_fields->waveStepsUntilTransition -= std::max(1, g_frameSubsteps);
                if (m_fields->waveStepsUntilTransition <= 0) {
                    action = m_fields->waveTransitionTarget ? BotAction::Hold : BotAction::Release;
                    this->cancelWaveTransition();
                    waveDidSwitch = true;
                } else {
                    const int checkSteps = std::min(m_fields->waveStepsUntilTransition, botSteps);
                    SimResult currentPath = probe(
                        rig, m_fields->botHolding ? SimPlan::Hold : SimPlan::Release,
                        0, -1, checkSteps
                    );
                    if (currentPath.died && currentPath.survived < checkSteps) {
                        this->cancelWaveTransition();
                    } else {
                        action = m_fields->botHolding ? BotAction::Hold : BotAction::Release;
                    }
                }
            }

            if (!orbTapForced && !timedHoldForced && !timedHoldJustReleased &&
                !waveDidSwitch && (!scheduledWave || !m_fields->waveTransitionActive)) {
                WavePlan selectedWavePlan;
                action = decideAutoPress(
                    rig, idleResult, botSteps, m_fields->botHolding,
                    scheduledWave ? &selectedWavePlan : nullptr,
                    &selectedTimedHoldSteps,
                    &selectedOrbTapDelaySteps
                );
                if (scheduledWave && selectedWavePlan.switchCount > 0) {
                    m_fields->waveTransitionActive = true;
                    m_fields->waveStepsUntilTransition = selectedWavePlan.switchSteps[0];
                    m_fields->waveTransitionTarget = !selectedWavePlan.startHolding;
                }
            }

            if (debugLogEnabled()) {
                ++m_fields->logFrame;
                const bool hazard = idleResult.died && idleResult.survived < botSteps;
                if (static_cast<int>(action) != m_fields->lastLoggedAction ||
                    (hazard && m_fields->logFrame % 15 == 0)) {
                    m_fields->lastLoggedAction = static_cast<int>(action);
                    log::info(
                        "[AT] f={} x={:.0f} y={:.0f} gnd={} up={} mode={} dual={} "
                        "idle(died={} surv={} orbContact={} orbFirst={} orbActivated={}) "
                        "tapPulse={} tapLeft={} orbPlanDelay={} orbDelayLeft={} robotHold={} action={}",
                        m_fields->logFrame, m_player1->getPositionX(), m_player1->getPositionY(),
                        m_player1->m_isOnGround, m_player1->m_isUpsideDown,
                        m_player1->m_isBall ? "ball" : (m_player1->m_isRobot ? "robot" :
                            (m_player1->m_isShip ? "ship" : (m_player1->m_isBird ? "ufo" :
                            (m_player1->m_isDart ? "wave" : (m_player1->m_isSwing ? "swing" :
                            (m_player1->m_isSpider ? "spider" : "cube")))))),
                        rig.dummy2 != nullptr, idleResult.died, idleResult.survived,
                        idleResult.orbContacts, idleResult.firstOrbContactStep,
                        idleResult.orbActivations, m_fields->botTapActive,
                        m_fields->botTapStepsRemaining, selectedOrbTapDelaySteps,
                        m_fields->orbTapStepsRemaining,
                        m_fields->timedHoldStepsRemaining, static_cast<int>(action)
                    );
                }
            }

            switch (action) {
                case BotAction::Hold:
                    this->cancelTap();
                    this->cancelOrbTapSchedule();
                    if (!timedHoldForced) this->cancelTimedHold();
                    this->pressBot();
                    break;
                case BotAction::TimedHold:
                    this->cancelTap();
                    this->cancelOrbTapSchedule();
                    this->cancelWaveTransition();
                    // A planned finite pulse also starts from released input
                    // in the clone. Force a fresh live edge if a previous
                    // Hold left Jump down.
                    if (requiresFreshPressEdge(action, m_fields->botHolding)) this->releaseBot();
                    this->pressBot();
                    m_fields->timedHoldActive = true;
                    m_fields->timedHoldStepsRemaining = std::max(
                        std::max(1, g_frameSubsteps), selectedTimedHoldSteps
                    );
                    m_fields->timedHoldRobotMode = m_player1->m_isRobot;
                    break;
                case BotAction::DelayedTap:
                    this->cancelTap();
                    this->cancelTimedHold();
                    this->cancelWaveTransition();
                    this->releaseBot();
                    m_fields->orbTapScheduled = true;
                    m_fields->orbTapStepsRemaining = std::max(
                        std::max(1, g_frameSubsteps), selectedOrbTapDelaySteps
                    );
                    break;
                case BotAction::Tap:
                    this->cancelOrbTapSchedule();
                    this->cancelTimedHold();
                    // SimPlan::Tap always starts from released input. Mirror
                    // that edge in the live game even when a previous Hold
                    // left Jump down; otherwise an orb/cube tap is swallowed.
                    if (requiresFreshPressEdge(action, m_fields->botHolding)) this->releaseBot();
                    this->pressBot();
                    m_fields->botTapActive = true;
                    m_fields->botTapStepsRemaining = std::max(1, g_frameSubsteps);
                    break;
                case BotAction::Release:
                default:
                    this->cancelTap();
                    if (!orbTapForced) this->cancelOrbTapSchedule();
                    this->cancelTimedHold();
                    this->releaseBot();
                    break;
            }
        } else {
            this->releaseBot();
            this->cancelTap();
            this->cancelOrbTapSchedule();
            this->cancelWaveTransition();
            this->cancelTimedHold();
        }
    }

    void onQuit() {
        this->releaseBot();
        this->cancelTap();
        this->cancelOrbTapSchedule();
        this->cancelWaveTransition();
        this->cancelTimedHold();

        if (g_performanceScheduler == &m_fields->scheduler) g_performanceScheduler = nullptr;
        m_fields->drawNode = nullptr;
        m_fields->panel = nullptr;
        m_fields->panelTouch = nullptr;
        m_fields->headerLabel = nullptr;
        m_fields->scanLabel = nullptr;
        m_fields->dummy1 = nullptr;
        m_fields->dummy2 = nullptr;
        m_fields->menu = nullptr;
        m_fields->autoLabel = nullptr;
        m_fields->trajLabel = nullptr;
        m_fields->waveLabel = nullptr;
        m_fields->performanceLabel = nullptr;
        m_fields->adaptiveLabel = nullptr;
        m_fields->metricsLabel = nullptr;

        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Auto Trajectory v0.7.0 loaded");
}
