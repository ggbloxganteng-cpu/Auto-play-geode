#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {
    bool g_simulating = false;
    bool g_simDead = false;
    PlayerObject* g_simDummy = nullptr;
    float g_stepDt = 0.25f;
    bool g_warnedStepZero = false;

    // Player-1 physics substeps seen since the last postUpdate, and the
    // substeps-per-frame value measured from it (used to time the jump).
    int g_stepCounter = 0;
    int g_frameSubsteps = 4;

    // Simulation tuning, all in physics substeps (240 per second).
    constexpr int kJumpAirborneSteps = 15; // consecutive airborne substeps that count as a real jump
    constexpr int kLandSettleSteps = 16;   // consecutive grounded substeps after a jump = landed safely
    constexpr int kWaitMarginSteps = 2;    // safety margin when deciding to wait for a later jump
    constexpr int kDoomedMargin = 4;       // when nothing survives, press only if clearly better
    constexpr int kImminentSteps = 16;     // hazard this close: stop waiting, jump now if it works
    constexpr int kTapBudgetSteps = 180;   // max substeps an in-flight tap may wait for its landing
    constexpr int kFlySwitchMargin = 2;    // ship: switch input only if clearly better (anti-flap)

    const cocos2d::ccColor4F kNoColor{0.f, 0.f, 0.f, 0.f};

    bool autoPlayEnabled() {
        return Mod::get()->getSettingValue<bool>("auto-play");
    }

    bool trajectoryEnabled() {
        return Mod::get()->getSettingValue<bool>("show-trajectory");
    }

    bool uiEnabled() {
        return Mod::get()->getSettingValue<bool>("show-ui");
    }

    int trajectorySteps() {
        return static_cast<int>(Mod::get()->getSettingValue<int64_t>("trajectory-steps"));
    }

    int autoPlayLookahead() {
        return static_cast<int>(Mod::get()->getSettingValue<int64_t>("autoplay-lookahead"));
    }

    bool isFlyingMode(PlayerObject* p) {
        return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing;
    }

    void resetCollisionLog(PlayerObject* p) {
        if (!p) return;
        if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
        if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
        if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
        if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
        p->m_lastCollisionTop = -1;
        p->m_lastCollisionBottom = -1;
        p->m_lastCollisionLeft = -1;
        p->m_lastCollisionRight = -1;
    }

    void syncDummy(PlayerObject* dummy, PlayerObject* source) {
        dummy->setVisible(false);
        dummy->copyAttributes(source);
        dummy->setScale(source->getScale());
        dummy->setContentSize(source->getContentSize());
        dummy->m_width = source->m_width;
        dummy->m_height = source->m_height;
        dummy->m_gravityMod = source->m_gravityMod;
        dummy->m_isOnGround = source->m_isOnGround;
        dummy->m_isUpsideDown = source->m_isUpsideDown;
        dummy->m_vehicleSize = source->m_vehicleSize;
        dummy->m_playerSpeed = source->m_playerSpeed;
        dummy->m_isDead = false;
    }
}

struct SimResult {
    bool died = false;
    int survived = 0;
    // Only meaningful when died: true if the death looks like a jump that
    // started too late (hit something while still taking off, or never took
    // off). false = too early (died after landing / late in the jump).
    bool late = false;
    cocos2d::CCPoint deathPos = {0.f, 0.f};
};

enum class SimPlan {
    Release, // never press
    Hold,    // press after the delay and keep holding (cube keeps bunny hopping)
    Tap      // press after the delay, let go as soon as the player has left the ground
};

// What the bot should physically do this frame. The whole point of the bot is
// that the REAL input mirrors the plan the simulation validated; anything else
// desyncs the sim from reality (the original "jumps only once" bug).
enum class BotAction {
    Release,
    Hold,
    Tap
};

SimResult runSimulation(
    PlayLayer* pl,
    PlayerObject* dummy,
    PlayerObject* source,
    SimPlan plan,
    int pressDelay,
    int maxSteps,
    int drawSteps,
    float dt,
    cocos2d::CCDrawNode* draw,
    cocos2d::ccColor4F color
) {
    SimResult result;
    if (!pl || !dummy || !source || maxSteps <= 0) {
        return result;
    }

    g_simulating = true;
    g_simDead = false;
    g_simDummy = dummy;

    syncDummy(dummy, source);

    resetCollisionLog(dummy);
    dummy->releaseButton(PlayerButton::Jump);

    const bool flying = isFlyingMode(source);
    bool pressed = false;
    bool tapDone = false;
    bool tapArmed = false;
    bool jumped = false;
    int airborneStreak = 0;
    int landedStreak = 0;

    cocos2d::CCPoint previous = dummy->getPosition();

    auto markDeath = [&](int step) {
        result.died = true;
        result.deathPos = dummy->getPosition();
        result.late = !flying && !(
            airborneStreak >= kJumpAirborneSteps || (jumped && dummy->m_isOnGround)
        );
        if (draw && step <= drawSteps) {
            draw->drawDot(dummy->getPosition(), 4.f, color);
        }
    };

    for (int i = 0; i < maxSteps; ++i) {
        if (plan != SimPlan::Release && !pressed && i >= pressDelay) {
            dummy->pushButton(PlayerButton::Jump);
            pressed = true;
            tapArmed = dummy->m_isOnGround;
        }

        dummy->update(dt);

        if (g_simDead || dummy->m_isDead) {
            markDeath(i);
            break;
        }

        resetCollisionLog(dummy);
        pl->checkCollisions(dummy, dt, false);

        if (g_simDead || dummy->m_isDead) {
            markDeath(i);
            break;
        }

        cocos2d::CCPoint current = dummy->getPosition();
        if (draw && i < drawSteps) {
            draw->drawSegment(previous, current, 0.8f, color);
        }
        previous = current;

        ++result.survived;

        // Tap: the jump has happened once the player left the ground, so let go.
        if (plan == SimPlan::Tap && pressed && !tapDone) {
            if (dummy->m_isOnGround) {
                tapArmed = true;
            } else if (tapArmed) {
                dummy->releaseButton(PlayerButton::Jump);
                tapDone = true;
            }
        }

        // A real jump that landed and stayed on the ground for a moment counts
        // as safe (the next obstacle is handled by the next decision).
        if (!flying) {
            if (dummy->m_isOnGround) {
                airborneStreak = 0;
                if (jumped && ++landedStreak >= kLandSettleSteps) {
                    result.survived = maxSteps;
                    break;
                }
            } else {
                landedStreak = 0;
                if (++airborneStreak >= kJumpAirborneSteps) {
                    jumped = true;
                }
            }
        }
    }

    dummy->releaseButton(PlayerButton::Jump);
    dummy->setVisible(false);

    g_simulating = false;
    g_simDummy = nullptr;
    return result;
}

enum class JumpOutlook {
    Survives, // a jump starting after the delay gets through
    TooEarly, // it fails, but only because the jump would start too early
    TooLate   // it fails because the jump window is already over
};

// How does a jump that starts after `delay` substeps fare? Tries a single jump
// first, then holding (bunny hop). With earlyExit, a "too early" single jump
// is returned right away (cheaper; enough when you only need to know whether
// waiting is still fine). When it returns Survives, `survivedPlan` (if given)
// tells which plan made it so the bot can execute the same plan for real.
JumpOutlook jumpOutlook(
    PlayLayer* pl, PlayerObject* dummy, PlayerObject* source,
    int delay, int steps, float dt, bool earlyExit, int& bestSurvived,
    SimPlan* survivedPlan = nullptr
) {
    bool allLate = true;
    for (SimPlan plan : {SimPlan::Tap, SimPlan::Hold}) {
        SimResult r = runSimulation(
            pl, dummy, source, plan, delay, steps, 0, dt, nullptr, kNoColor
        );
        bestSurvived = std::max(bestSurvived, r.survived);
        if (!r.died) {
            if (survivedPlan) *survivedPlan = plan;
            return JumpOutlook::Survives;
        }
        if (!r.late) {
            allLate = false;
            if (earlyExit) return JumpOutlook::TooEarly;
        }
    }
    return allLate ? JumpOutlook::TooLate : JumpOutlook::TooEarly;
}

// Decides what the bot should do this frame.
// `idle` is the "release now" simulation (it may be longer than botSteps).
// `currentlyHolding` is the real current input, used for ship hysteresis.
BotAction decideAutoPress(
    PlayLayer* pl, PlayerObject* dummy, PlayerObject* source,
    SimResult const& idle, int botSteps, float dt, bool currentlyHolding
) {
    // Nothing dangerous inside the lookahead window: stay released.
    if (!(idle.died && idle.survived < botSteps)) {
        return BotAction::Release;
    }

    // Flying modes (ship / swing / wave): bang-bang control with hysteresis so
    // the input does not flap every frame. Hold to climb, release to dive;
    // only switch when the other option is better or the current one dies.
    if (isFlyingMode(source)) {
        SimResult hold = runSimulation(
            pl, dummy, source, SimPlan::Hold, 0, botSteps, 0, dt, nullptr, kNoColor
        );
        if (!hold.died && idle.died) return BotAction::Hold;
        if (hold.died && !idle.died) return BotAction::Release;
        if (!hold.died && !idle.died) {
            // Both fine inside the window: keep the current input.
            return currentlyHolding ? BotAction::Hold : BotAction::Release;
        }
        // Both fail: switch only if it is clearly the longer-surviving path.
        if (hold.survived > idle.survived + kFlySwitchMargin) return BotAction::Hold;
        if (idle.survived > hold.survived + kFlySwitchMargin) return BotAction::Release;
        return currentlyHolding ? BotAction::Hold : BotAction::Release;
    }

    // Cube / ball / etc: jump at the LAST safe moment instead of the moment the
    // danger first shows up. Look at a jump that would start one frame from now:
    //  - it works, or it only fails for being too early -> keep waiting
    //  - it fails for being too late -> the window is closing, jump now
    const int frameSteps = std::clamp(g_frameSubsteps, 1, 16);
    const int waitDelay = frameSteps + kWaitMarginSteps;

    int waitBest = 0;
    JumpOutlook waitOutlook = jumpOutlook(
        pl, dummy, source, waitDelay, botSteps, dt, true, waitBest
    );
    if (waitOutlook == JumpOutlook::Survives) return BotAction::Release;
    if (waitOutlook == JumpOutlook::TooEarly &&
        idle.survived > waitDelay + kImminentSteps) {
        return BotAction::Release;
    }

    // Waiting is over: jump now with the SAME plan the simulation validated.
    SimPlan winning = SimPlan::Hold;
    int pressBest = 0;
    if (jumpOutlook(pl, dummy, source, 0, botSteps, dt, false, pressBest, &winning) ==
        JumpOutlook::Survives) {
        return winning == SimPlan::Hold ? BotAction::Hold : BotAction::Tap;
    }

    // Nothing survives: only press if it is clearly the longest-lasting option.
    if (pressBest > std::max(idle.survived, waitBest) + kDoomedMargin) {
        return BotAction::Hold;
    }
    return BotAction::Release;
}

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
        if (g_simulating) return;
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool unk) {
        if (g_simulating) return;
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
        if (g_simulating) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (g_simulating) return;
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        if (g_simulating && player == g_simDummy && objects) {
            gd::vector<GameObject*> filtered;
            filtered.reserve(count);
            for (int i = 0; i < count; ++i) {
                auto* object = objects->at(i);
                if (!object) continue;

                switch (object->m_objectType) {
                    case GameObjectType::Solid:
                    case GameObjectType::Hazard:
                    case GameObjectType::AnimatedHazard:
                    case GameObjectType::CollisionObject:
                    case GameObjectType::Slope:
                        filtered.push_back(object);
                        break;
                    default:
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
        cocos2d::CCMenu* menu = nullptr;
        cocos2d::CCLabelBMFont* autoLabel = nullptr;
        cocos2d::CCLabelBMFont* trajLabel = nullptr;
        bool botHolding = false;
        bool botTapActive = false;
        bool botTapGrounded = false;
        int botTapBudget = 0;
        bool loggedSetup = false;
        bool lastAuto = false;
        bool lastTraj = true;
        bool lastUi = true;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

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

        this->setupMenu();
        m_fields->botHolding = false;
        m_fields->botTapActive = false;
        m_fields->botTapGrounded = false;
        m_fields->botTapBudget = 0;
        m_fields->loggedSetup = false;
        g_warnedStepZero = false;

        return true;
    }

    void setupMenu() {
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

        auto menu = cocos2d::CCMenu::create(autoItem, trajItem, nullptr);
        menu->alignItemsVerticallyWithPadding(3.f);
        menu->setPosition({96.f, 40.f});
        menu->setID("autotraj-menu"_spr);
        this->addChild(menu, 999);

        m_fields->menu = menu;
        m_fields->autoLabel = autoLabel;
        m_fields->trajLabel = trajLabel;
        m_fields->lastAuto = autoPlayEnabled();
        m_fields->lastTraj = trajectoryEnabled();
        m_fields->lastUi = uiEnabled();

        autoLabel->setString(m_fields->lastAuto ? "AUTO PLAY: ON" : "AUTO PLAY: OFF");
        trajLabel->setString(m_fields->lastTraj ? "TRAJECTORY: ON" : "TRAJECTORY: OFF");
        menu->setVisible(m_fields->lastUi);
    }

    void onToggleAutoPlay(CCObject*) {
        bool value = !autoPlayEnabled();
        Mod::get()->setSettingValue<bool>("auto-play", value);
    }

    void onToggleTrajectory(CCObject*) {
        bool value = !trajectoryEnabled();
        Mod::get()->setSettingValue<bool>("show-trajectory", value);
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
        m_fields->botTapGrounded = false;
        m_fields->botTapBudget = 0;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        if (g_simulating && g_simDummy && player == g_simDummy) {
            g_simDead = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
        if (player == m_player1) {
            m_fields->botHolding = false;
            this->cancelTap();
        }
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        m_fields->botHolding = false;
        this->cancelTap();
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) {
        if (g_simulating) return;
        PlayLayer::playEndAnimationToPos(pos);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (g_simulating) return;
        PlayLayer::flipGravity(player, flip, noEffects);
    }

    void postUpdate(float dt) override {
        PlayLayer::postUpdate(dt);

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
            if (m_fields->menu) {
                m_fields->menu->setVisible(uiOn);
            }
            m_fields->lastUi = uiOn;
        }

        if (m_fields->drawNode) {
            m_fields->drawNode->clear();
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
            return;
        }

        if (m_player1->m_isPlatformer || m_gameState.m_isDualMode) {
            this->releaseBot();
            this->cancelTap();
            return;
        }

        float dtStep = g_stepDt;
        if (!(dtStep > 0.001f) || dtStep > 2.f) {
            dtStep = 0.25f;
        }

        // The overlay length is purely visual. The bot has its own lookahead,
        // so changing one never changes how the other behaves.
        int drawSteps = trajOn ? std::clamp(trajectorySteps(), 10, 400) : 0;
        int botSteps = autoOn ? std::clamp(autoPlayLookahead(), 24, 240) : 0;
        int horizon = std::max(drawSteps, botSteps);

        if (horizon <= 0) {
            this->releaseBot();
            this->cancelTap();
            return;
        }

        auto* drawNode = drawSteps > 0 ? m_fields->drawNode : nullptr;

        // Green: press & hold now (overlay only).
        SimResult holdResult{};
        if (drawSteps > 0) {
            holdResult = runSimulation(
                this, m_fields->dummy1, m_player1, SimPlan::Hold, 0, drawSteps, drawSteps, dtStep,
                drawNode, cocos2d::ccColor4F{0.15f, 1.f, 0.35f, 1.f}
            );
        }

        // Red: release now. Shared with the bot's "is it dangerous?" check.
        SimResult idleResult = runSimulation(
            this, m_fields->dummy1, m_player1, SimPlan::Release, 0, horizon, drawSteps, dtStep,
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

        // Finish an in-flight tap before anything else: the simulation that
        // validated it assumed the button stays down until the player has left
        // the ground after touching it. Releasing earlier would desync the
        // real input from the validated plan (this is what made the bot jump
        // only once at multi-pillar hold sections).
        bool tapForcedHold = false;
        if (m_fields->botTapActive) {
            if (m_player1->m_isOnGround) {
                m_fields->botTapGrounded = true;
            }
            m_fields->botTapBudget -= std::max(1, g_frameSubsteps);
            bool launched = m_fields->botTapGrounded && !m_player1->m_isOnGround;
            if (launched || m_fields->botTapBudget <= 0) {
                this->cancelTap();
            } else {
                tapForcedHold = true;
            }
        }

        if (autoOn) {
            BotAction action = tapForcedHold
                ? BotAction::Hold
                : decideAutoPress(
                      this, m_fields->dummy1, m_player1, idleResult, botSteps, dtStep,
                      m_fields->botHolding
                  );

            switch (action) {
                case BotAction::Hold:
                    this->cancelTap();
                    this->pressBot();
                    break;
                case BotAction::Tap:
                    // Mirrors SimPlan::Tap: press now, keep holding until the
                    // player has been grounded and left the ground again.
                    this->pressBot();
                    if (!m_fields->botTapActive) {
                        m_fields->botTapActive = true;
                        m_fields->botTapGrounded = m_player1->m_isOnGround;
                        m_fields->botTapBudget = kTapBudgetSteps;
                    }
                    break;
                case BotAction::Release:
                default:
                    this->cancelTap();
                    this->releaseBot();
                    break;
            }
        } else {
            this->releaseBot();
            this->cancelTap();
        }
    }

    void onQuit() {
        this->releaseBot();
        this->cancelTap();

        m_fields->drawNode = nullptr;
        m_fields->dummy1 = nullptr;
        m_fields->menu = nullptr;
        m_fields->autoLabel = nullptr;
        m_fields->trajLabel = nullptr;

        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Auto Trajectory v0.3.0 loaded");
}
