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

using namespace geode::prelude;

namespace {
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

// ---- BEGIN PORTABLE LOGIC (also compiled by the offline test harness) ----
namespace {
    bool g_simulating = false;
    bool g_simDead = false;
    bool g_simPortal = false;
    PlayerObject* g_simDummies[2] = {nullptr, nullptr};

    // Objects touched by a predictive clone are snapshotted and restored after
    // each run. This prevents a simulated orb/pad/portal from consuming the
    // real level object's activation state.
    struct SimObjectSnapshot {
        GameObject* object = nullptr;
        bool isActivated = false;
        EffectGameObject* effect = nullptr;
        bool effectActivated = false;
        bool activatedByPlayer1 = false;
        bool activatedByPlayer2 = false;
    };
    std::vector<SimObjectSnapshot> g_simObjectSnapshots;
    std::unordered_set<GameObject*> g_simTouchedObjects;
    float g_stepDt = 0.25f;
    bool g_warnedStepZero = false;

    // Player-1 physics substeps seen since the last postUpdate, and the
    // substeps-per-frame value measured from it (used to time the jump).
    int g_stepCounter = 0;
    int g_frameSubsteps = 4;
    long long g_simStepCount = 0; // total simulated substeps (cost statistics)

    // Simulation tuning, all in physics substeps (240 per second).
    constexpr int kWaitMarginSteps = 2;    // safety margin when deciding to wait for a later jump
    constexpr int kDoomedMargin = 4;       // when nothing survives, press only if clearly better
    constexpr int kImminentSteps = 16;     // hazard this close: stop waiting, jump now if it works
    constexpr int kShortExtra = 100;       // fallback horizon: just past the first hazard
    constexpr int kTapProbeBack = 35;      // probe a tap this many substeps before the first hazard
    constexpr int kTapBudgetSteps = 180;   // max substeps an in-flight tap may wait for its landing
    constexpr int kFlySwitchMargin = 2;    // flying: switch input only if clearly better (anti-flap)
    constexpr int kWaveMaxSteps = 160;     // bounded planning horizon for the multi-pattern wave search
    constexpr int kWaveRoomOffsetA = 8;    // robustness probe: small vertical perturbations

    const cocos2d::ccColor4F kNoColor{0.f, 0.f, 0.f, 0.f};

    bool isSimDummy(PlayerObject* p) {
        return p && (p == g_simDummies[0] || p == g_simDummies[1]);
    }

    // Gravity / mode / size / dual / speed / teleport portals (object ids).
    bool isPortalId(int id) {
        switch (id) {
            case 10: case 11:                    // gravity
            case 12: case 13: case 47: case 111: // cube, ship, ball, ufo
            case 660: case 745: case 1331: case 1933: // wave, robot, spider, swing
            case 99: case 101:                   // size
            case 286: case 287:                  // dual
            case 200: case 201: case 202: case 203: case 1334: // speed
            case 747: case 2926:                 // teleport, gravity toggle
                return true;
            default:
                return false;
        }
    }

    // These portals change the player's collision shape or control mode. The
    // clone is allowed to enter them, then planning re-syncs from the real player
    // on the next frame instead of pretending the old mode continues forever.
    bool requiresPortalResync(int id) {
        switch (id) {
            case 12: case 13: case 47: case 111: // ball, ship, UFO, wave
            case 660: case 745: case 1331:      // robot, spider, swing
            case 99: case 101:                  // size
            case 286: case 287:                 // dual
            case 747:                           // teleport
                return true;
            default:
                return false;
        }
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
        dummy->resetTouchedRings(true);
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
        // copy input / special-interaction state: an incomplete clone can miss
        // an orb already under the player or replay a buffered button edge.
        dummy->m_isDashing = source->m_isDashing;
        dummy->m_lastActivatedPortal = source->m_lastActivatedPortal;
        dummy->m_holdingButtons = source->m_holdingButtons;
        dummy->m_jumpBuffered = source->m_jumpBuffered;
        dummy->m_wasJumpBuffered = source->m_wasJumpBuffered;
        dummy->m_stateJumpBuffered = source->m_stateJumpBuffered;
        dummy->m_stateRingJump = source->m_stateRingJump;
        dummy->m_stateRingJump2 = source->m_stateRingJump2;
        dummy->m_wasRobotJump = source->m_wasRobotJump;
        dummy->m_touchedRing = source->m_touchedRing;
        dummy->m_touchedPad = source->m_touchedPad;
        dummy->m_touchedGravityPortal = source->m_touchedGravityPortal;
        dummy->m_isDead = false;
    }

    void snapshotSimObject(GameObject* object) {
        if (!object || !g_simTouchedObjects.insert(object).second) return;
        SimObjectSnapshot state;
        state.object = object;
        state.isActivated = object->m_isActivated;
        if (auto* effect = typeinfo_cast<EffectGameObject*>(object)) {
            state.effect = effect;
            state.effectActivated = effect->m_activated;
            state.activatedByPlayer1 = effect->m_activatedByPlayer1;
            state.activatedByPlayer2 = effect->m_activatedByPlayer2;
        }
        g_simObjectSnapshots.push_back(state);
    }

    void restoreSimObjects() {
        for (auto const& state : g_simObjectSnapshots) {
            if (!state.object) continue;
            state.object->m_isActivated = state.isActivated;
            if (state.effect) {
                state.effect->m_activated = state.effectActivated;
                state.effect->m_activatedByPlayer1 = state.activatedByPlayer1;
                state.effect->m_activatedByPlayer2 = state.activatedByPlayer2;
            }
        }
        g_simObjectSnapshots.clear();
        g_simTouchedObjects.clear();
    }

    bool isSimInteractable(GameObject* object) {
        if (!object) return false;
        switch (object->m_objectType) {
            case GameObjectType::YellowJumpPad:
            case GameObjectType::PinkJumpPad:
            case GameObjectType::RedJumpPad:
            case GameObjectType::GravityPad:
            case GameObjectType::SpiderPad:
            case GameObjectType::YellowJumpRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::DropRing:
            case GameObjectType::CustomRing:
            case GameObjectType::DashRing:
            case GameObjectType::GravityDashRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::InverseGravityPortal:
            case GameObjectType::NormalGravityPortal:
            case GameObjectType::GravityTogglePortal:
                return true;
            default:
                // Gravity and speed portals are handled by the normal collision
                // path. Complex mode/size/dual/teleport portals are boundaries.
                return isPortalId(object->m_objectID) &&
                       !requiresPortalResync(object->m_objectID);
        }
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
    Hold,    // press after the delay; hold forever (or for holdSteps)
    Tap,     // press after the delay, let go as soon as the player has left the ground
    WavePattern // follow a bounded hold/release sequence used by wave pathfinding
};

struct WavePlan {
    bool startHolding = false;
    int switchCount = 0;
    std::array<int, 8> switchSteps{};
};

// What the bot should physically do this frame. The REAL input mirrors the
// plan the simulation validated; anything else desyncs sim and reality.
enum class BotAction {
    Release,
    Hold,
    Tap
};

// Everything a simulation needs: the level layer plus one hidden clone per
// real player (player 2 only exists in dual mode).
struct SimRig {
    PlayLayer* pl = nullptr;
    PlayerObject* dummy = nullptr;
    PlayerObject* source = nullptr;
    PlayerObject* dummy2 = nullptr;
    PlayerObject* source2 = nullptr;
    float dt = 0.25f;
    float yOffset = 0.f; // shift the clones vertically to test how much room a path has
    bool roomAwareWave = false; // experimental wave controller (setting)
};

SimResult runSimulation(
    SimRig const& rig,
    SimPlan plan,
    int pressDelay,
    int holdSteps, // Hold only: release this many substeps after pressing (<0 = never)
    int maxSteps,
    int drawSteps,
    cocos2d::CCDrawNode* draw,
    cocos2d::ccColor4F color,
    WavePlan const* wavePlan = nullptr
) {
    SimResult result;
    PlayLayer* pl = rig.pl;
    PlayerObject* d1 = rig.dummy;
    PlayerObject* s1 = rig.source;
    PlayerObject* d2 = (rig.dummy2 && rig.source2) ? rig.dummy2 : nullptr;
    if (!pl || !d1 || !s1 || maxSteps <= 0) {
        return result;
    }
    const float dt = rig.dt;

    g_simulating = true;
    g_simDead = false;
    g_simPortal = false;
    g_simObjectSnapshots.clear();
    g_simTouchedObjects.clear();
    g_simDummies[0] = d1;
    g_simDummies[1] = d2;

    syncDummy(d1, s1);
    resetCollisionLog(d1);
    d1->releaseButton(PlayerButton::Jump);
    if (d2) {
        syncDummy(d2, rig.source2);
        resetCollisionLog(d2);
        d2->releaseButton(PlayerButton::Jump);
    }

    if (rig.yOffset != 0.f) {
        auto p1 = d1->getPosition();
        d1->setPosition(cocos2d::CCPoint(p1.x, p1.y + rig.yOffset));
        if (d2) {
            auto p2 = d2->getPosition();
            d2->setPosition(cocos2d::CCPoint(p2.x, p2.y + rig.yOffset));
        }
    }

    auto pressAll = [&]() {
        d1->pushButton(PlayerButton::Jump);
        if (d2) d2->pushButton(PlayerButton::Jump);
    };
    auto releaseAll = [&]() {
        d1->releaseButton(PlayerButton::Jump);
        if (d2) d2->releaseButton(PlayerButton::Jump);
    };
    auto anyDead = [&]() {
        return g_simDead || d1->m_isDead || (d2 && d2->m_isDead);
    };

    const bool flying = isFlyingMode(s1);
    bool pressed = false;
    bool released = false;
    bool waveInputDown = false;
    bool tapArmed = false;
    bool jumped = false;
    int airborneStreak = 0;

    cocos2d::CCPoint previous = d1->getPosition();

    auto markDeath = [&](int step) {
        result.died = true;
        result.deathPos = d1->getPosition();
        result.late = !flying && !(
            airborneStreak >= 15 || (jumped && d1->m_isOnGround)
        );
        if (draw && step <= drawSteps) {
            draw->drawDot(d1->getPosition(), 4.f, color);
        }
    };

    for (int i = 0; i < maxSteps; ++i) {
        ++g_simStepCount;
        if (plan == SimPlan::WavePattern && wavePlan) {
            bool desiredDown = wavePlan->startHolding;
            for (int change = 0; change < wavePlan->switchCount; ++change) {
                if (i >= wavePlan->switchSteps[change]) desiredDown = !desiredDown;
            }
            if (desiredDown != waveInputDown) {
                if (desiredDown) pressAll();
                else releaseAll();
                waveInputDown = desiredDown;
            }
        } else {
            if (plan != SimPlan::Release && !pressed && i >= pressDelay) {
                pressAll();
                pressed = true;
                tapArmed = d1->m_isOnGround;
            }
            if (plan == SimPlan::Hold && pressed && !released && holdSteps >= 0 &&
                i >= pressDelay + holdSteps) {
                releaseAll();
                released = true;
            }
        }

        d1->update(dt);
        if (d2) d2->update(dt);

        if (anyDead()) {
            markDeath(i);
            break;
        }

        resetCollisionLog(d1);
        pl->checkCollisions(d1, dt, false);
        if (d2) {
            resetCollisionLog(d2);
            pl->checkCollisions(d2, dt, false);
        }

        if (anyDead()) {
            markDeath(i);
            break;
        }

        cocos2d::CCPoint current = d1->getPosition();
        if (draw && i < drawSteps) {
            draw->drawSegment(previous, current, 0.8f, color);
        }
        previous = current;

        ++result.survived;

        // Tap: the jump has happened once the player left the ground, so let go.
        if (plan == SimPlan::Tap && pressed && !released) {
            if (d1->m_isOnGround) {
                tapArmed = true;
            } else if (tapArmed) {
                releaseAll();
                released = true;
            }
        }

        // The path reached a portal: what comes after it is not modelled, so
        // the prediction ends here (counts as safe; the next frame re-syncs
        // with the real player once it has actually gone through).
        if (g_simPortal) {
            result.survived = maxSteps;
            break;
        }

        // Only used to tell "jump too early" from "jump too late" on death.
        if (d1->m_isOnGround) {
            airborneStreak = 0;
        } else if (++airborneStreak >= 15) {
            jumped = true;
        }
    }

    releaseAll();
    restoreSimObjects();
    d1->setVisible(false);
    if (d2) d2->setVisible(false);

    g_simulating = false;
    g_simDummies[0] = nullptr;
    g_simDummies[1] = nullptr;
    return result;
}

static SimResult probe(
    SimRig const& rig, SimPlan plan, int delay, int holdSteps, int steps
) {
    return runSimulation(rig, plan, delay, holdSteps, steps, 0, nullptr, kNoColor);
}

static SimResult probeWave(SimRig const& rig, WavePlan const& plan, int steps) {
    return runSimulation(rig, SimPlan::WavePattern, 0, -1, steps, 0, nullptr, kNoColor, &plan);
}

// Cube / ball / robot / spider: jump at the last moment a jump still works.
//
// A jump is judged by simulating it for the whole lookahead (so a jump is
// chosen that also sets up the NEXT obstacle, e.g. landing on the start of a
// narrow pillar). Single taps are preferred; holding (bunny hop) is only used
// when no single tap gets through, e.g. on pillar staircases.
//  - a jump one frame later still works       -> keep waiting
//  - a jump now works but one frame later not -> the window is closing, jump
//  - a tap window lies ahead                  -> keep waiting for it
// If nothing gets through the full lookahead, judge again with a shorter
// window that only covers the first hazard.
static BotAction decideGround(
    SimRig const& rig, SimResult const& idle, int botSteps
) {
    const int frameSteps = std::clamp(g_frameSubsteps, 1, 16);
    const int waitDelay = frameSteps + kWaitMarginSteps;

    const int horizons[2] = {
        botSteps, std::min(botSteps, idle.survived + kShortExtra)
    };
    int bestNow = 0;
    int bestWait = 0;

    for (int hi = 0; hi < 2; ++hi) {
        const int steps = horizons[hi];
        if (hi == 1 && steps >= botSteps) break;

        // 1. Single tap.
        SimResult tapWait = probe(rig, SimPlan::Tap, waitDelay, -1, steps);
        bestWait = std::max(bestWait, tapWait.survived);
        if (!tapWait.died) return BotAction::Release;

        SimResult tapNow = probe(rig, SimPlan::Tap, 0, -1, steps);
        bestNow = std::max(bestNow, tapNow.survived);
        if (!tapNow.died) return BotAction::Tap;

        // Is there a tap window further ahead? (probe the middle of where a
        // jump over the first hazard would have to start)
        const int ahead = idle.survived - kTapProbeBack;
        if (ahead > waitDelay) {
            SimResult tapAhead = probe(rig, SimPlan::Tap, ahead, -1, steps);
            if (!tapAhead.died) return BotAction::Release;
        }

        // 2. Hold (bunny hop).
        bool earlyAtWait = !tapWait.late;
        SimResult holdWait = probe(rig, SimPlan::Hold, waitDelay, -1, steps);
        bestWait = std::max(bestWait, holdWait.survived);
        if (!holdWait.died) return BotAction::Release;
        if (!holdWait.late) earlyAtWait = true;

        SimResult holdNow = probe(rig, SimPlan::Hold, 0, -1, steps);
        bestNow = std::max(bestNow, holdNow.survived);
        if (!holdNow.died) return BotAction::Hold;

        // Nothing works now or one frame later, but the failures look like
        // "too early": the window is still ahead.
        if (earlyAtWait && idle.survived > waitDelay + kImminentSteps) {
            return BotAction::Release;
        }
    }

    // Nothing survives: only press if it is clearly the longest-lasting option.
    if (bestNow > std::max(idle.survived, bestWait) + kDoomedMargin) {
        return BotAction::Hold;
    }
    return BotAction::Release;
}

// Ship / UFO / swing: try short pulses as well as hold-forever / release-forever.
// Each candidate is "input A for a while, then the opposite input forever".
// Keep the current input if any candidate that starts with it gets through.
static BotAction decideFlying(
    SimRig const& rig, SimResult const& idle, int botSteps, bool currentlyHolding
) {
    static constexpr int kPulse[3] = {8, 24, 64};

    // Best survival among candidates starting with each input (for doomed cases).
    int bestHold = 0;
    int bestRelease = idle.survived;

    auto startsHolding = [&]() {
        SimResult r = probe(rig, SimPlan::Hold, 0, -1, botSteps);
        bestHold = std::max(bestHold, r.survived);
        if (!r.died) return true;
        for (int h : kPulse) {
            r = probe(rig, SimPlan::Hold, 0, h, botSteps);
            bestHold = std::max(bestHold, r.survived);
            if (!r.died) return true;
        }
        return false;
    };
    auto startsReleasing = [&]() {
        for (int d : kPulse) {
            SimResult r = probe(rig, SimPlan::Hold, d, -1, botSteps);
            bestRelease = std::max(bestRelease, r.survived);
            if (!r.died) return true;
        }
        return false;
    };

    if (currentlyHolding) {
        if (startsHolding()) return BotAction::Hold;
        if (startsReleasing()) return BotAction::Release;
    } else {
        if (startsReleasing()) return BotAction::Release;
        if (startsHolding()) return BotAction::Hold;
    }

    // Nothing gets through: switch only if clearly better, otherwise keep input.
    if (bestHold > bestRelease + kFlySwitchMargin) return BotAction::Hold;
    if (bestRelease > bestHold + kFlySwitchMargin) return BotAction::Release;
    return currentlyHolding ? BotAction::Hold : BotAction::Release;
}

// Wave (default): hold-vs-release comparison with hysteresis. Cheap and works
// well in practice.
static BotAction decideWaveClassic(
    SimRig const& rig, SimResult const& idle, int botSteps, bool currentlyHolding
) {
    if (!(idle.died && idle.survived < botSteps)) return BotAction::Release;
    SimResult hold = probe(rig, SimPlan::Hold, 0, -1, botSteps);
    if (!hold.died && idle.died) return BotAction::Hold;
    if (hold.died && !idle.died) return BotAction::Release;
    if (!hold.died && !idle.died) {
        return currentlyHolding ? BotAction::Hold : BotAction::Release;
    }
    if (hold.survived > idle.survived + kFlySwitchMargin) return BotAction::Hold;
    if (idle.survived > hold.survived + kFlySwitchMargin) return BotAction::Release;
    return currentlyHolding ? BotAction::Hold : BotAction::Release;
}

// Wave planner: test short multi-switch sequences instead of only comparing
// permanent hold/release or a single switch. A small robustness pass favors
// paths that still survive when the starting Y position is perturbed.
static BotAction decideWave(
    SimRig const& rig, SimResult const& idle, int lookahead, bool currentlyHolding,
    WavePlan* selectedPlan = nullptr
) {
    if (selectedPlan) *selectedPlan = WavePlan{};
    const int botSteps = std::min(lookahead, kWaveMaxSteps);

    // In open space both straight inputs are safe. Do not perturb a stable
    // wave line merely because a more complicated sequence also survives.
    SimResult holdStraight = probe(rig, SimPlan::Hold, 0, -1, botSteps);
    if (!idle.died && idle.survived >= botSteps &&
        !holdStraight.died && holdStraight.survived >= botSteps) {
        return currentlyHolding ? BotAction::Hold : BotAction::Release;
    }

    struct Candidate {
        WavePlan plan;
        bool alive = false;
        int survived = 0;
        int roomPasses = -1;
        int switches = 0;
    };
    Candidate candidates[12];
    int count = 0;
    auto add = [&](bool startHold, std::initializer_list<int> changes) {
        if (count >= static_cast<int>(std::size(candidates))) return;
        Candidate& candidate = candidates[count++];
        candidate.plan.startHolding = startHold;
        candidate.plan.switchCount = 0;
        int previous = 0;
        for (int at : changes) {
            if (at <= previous || at >= botSteps || candidate.plan.switchCount >= 8) continue;
            candidate.plan.switchSteps[candidate.plan.switchCount++] = at;
            previous = at;
        }
        candidate.switches = candidate.plan.switchCount;
    };

    // Stable paths and one-switch timing windows.
    add(false, {});
    add(true, {});
    for (int at : {16, 32, 48}) {
        add(false, {at});
        add(true, {at});
    }

    // A second switch permits a recovery turn after the first correction.
    add(false, {12, 32});
    add(true, {12, 32});

    // One compact pulse train handles passages that require repeated changes.
    for (bool startHold : {false, true}) {
        WavePlan plan;
        plan.startHolding = startHold;
        for (int at = 12; at < botSteps && plan.switchCount < 8; at += 12) {
            plan.switchSteps[plan.switchCount++] = at;
        }
        Candidate& candidate = candidates[count++];
        candidate.plan = plan;
        candidate.switches = plan.switchCount;
    }

    bool anyAlive = false;
    int bestHoldSurvival = 0;
    int bestReleaseSurvival = 0;
    for (int i = 0; i < count; ++i) {
        SimResult result = probeWave(rig, candidates[i].plan, botSteps);
        candidates[i].alive = !result.died;
        candidates[i].survived = result.survived;
        anyAlive = anyAlive || candidates[i].alive;
        if (candidates[i].plan.startHolding) {
            bestHoldSurvival = std::max(bestHoldSurvival, result.survived);
        } else {
            bestReleaseSurvival = std::max(bestReleaseSurvival, result.survived);
        }
    }

    if (!anyAlive) {
        if (bestHoldSurvival > bestReleaseSurvival + kFlySwitchMargin) return BotAction::Hold;
        if (bestReleaseSurvival > bestHoldSurvival + kFlySwitchMargin) return BotAction::Release;
        return currentlyHolding ? BotAction::Hold : BotAction::Release;
    }

    // Evaluate only the best two survivors with a small vertical perturbation;
    // this bounds CPU use on mobile while still rejecting fragile trajectories.
    for (int pass = 0; pass < 2; ++pass) {
        int bestIndex = -1;
        for (int i = 0; i < count; ++i) {
            if (!candidates[i].alive || candidates[i].roomPasses >= 0) continue;
            if (bestIndex < 0 || candidates[i].survived > candidates[bestIndex].survived ||
                (candidates[i].survived == candidates[bestIndex].survived &&
                 candidates[i].switches < candidates[bestIndex].switches)) {
                bestIndex = i;
            }
        }
        if (bestIndex < 0) break;
        Candidate& candidate = candidates[bestIndex];
        candidate.roomPasses = 0;
        SimRig up = rig;
        SimRig down = rig;
        up.yOffset = static_cast<float>(kWaveRoomOffsetA);
        down.yOffset = -static_cast<float>(kWaveRoomOffsetA);
        if (!probeWave(up, candidate.plan, botSteps).died) ++candidate.roomPasses;
        if (!probeWave(down, candidate.plan, botSteps).died) ++candidate.roomPasses;
    }

    int bestIndex = -1;
    for (int i = 0; i < count; ++i) {
        Candidate const& candidate = candidates[i];
        if (!candidate.alive) continue;
        if (bestIndex < 0) {
            bestIndex = i;
            continue;
        }
        Candidate const& best = candidates[bestIndex];
        bool better = candidate.roomPasses > best.roomPasses;
        if (candidate.roomPasses == best.roomPasses) {
            if (candidate.survived > best.survived) better = true;
            else if (candidate.survived == best.survived && candidate.switches < best.switches) better = true;
            else if (candidate.survived == best.survived && candidate.switches == best.switches &&
                     candidate.plan.startHolding == currentlyHolding &&
                     best.plan.startHolding != currentlyHolding) better = true;
        }
        if (better) bestIndex = i;
    }

    if (bestIndex < 0) return currentlyHolding ? BotAction::Hold : BotAction::Release;
    if (selectedPlan) *selectedPlan = candidates[bestIndex].plan;
    return candidates[bestIndex].plan.startHolding ? BotAction::Hold : BotAction::Release;
}

// Decides what the bot should do this frame.
// `idle` is the "release now" simulation (it may be longer than botSteps).
BotAction decideAutoPress(
    SimRig const& rig, SimResult const& idle, int botSteps, bool currentlyHolding,
    WavePlan* selectedWavePlan = nullptr
) {
    PlayerObject* p = rig.source;

    if (p->m_isDart) {
        if (rig.roomAwareWave) {
            return decideWave(rig, idle, botSteps, currentlyHolding, selectedWavePlan);
        }
        if (selectedWavePlan) *selectedWavePlan = WavePlan{};
        return decideWaveClassic(rig, idle, botSteps, currentlyHolding);
    }

    // Nothing dangerous inside the lookahead window: stay released.
    if (!(idle.died && idle.survived < botSteps)) {
        return BotAction::Release;
    }

    if (isFlyingMode(p)) return decideFlying(rig, idle, botSteps, currentlyHolding);
    return decideGround(rig, idle, botSteps);
}
// ---- END PORTABLE LOGIC ----

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
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool unk) {
        if (g_simulating && !isSimDummy(this)) return;
        if (g_simulating) snapshotSimObject(ring);
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
        cocos2d::CCMenu* menu = nullptr;
        cocos2d::CCLabelBMFont* autoLabel = nullptr;
        cocos2d::CCLabelBMFont* trajLabel = nullptr;
        bool botHolding = false;
        bool waveTransitionActive = false;
        int waveStepsUntilTransition = 0;
        bool waveTransitionTarget = false;
        bool botTapActive = false;
        bool botTapGrounded = false;
        int botTapBudget = 0;
        bool loggedSetup = false;
        int logFrame = 0;
        int lastLoggedAction = -1;
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

    void cancelWaveTransition() {
        m_fields->waveTransitionActive = false;
        m_fields->waveStepsUntilTransition = 0;
        m_fields->waveTransitionTarget = false;
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
            this->cancelWaveTransition();
        }
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        m_fields->botHolding = false;
        this->cancelTap();
        this->cancelWaveTransition();
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
            this->cancelWaveTransition();
            return;
        }

        // Platformer levels are not supported (no fixed forward motion).
        if (m_player1->m_isPlatformer) {
            this->releaseBot();
            this->cancelTap();
            this->cancelWaveTransition();
            return;
        }

        float dtStep = g_stepDt;
        if (!(dtStep > 0.001f) || dtStep > 2.f) {
            dtStep = 0.25f;
        }

        // The overlay length is purely visual. The bot has its own lookahead,
        // so changing one never changes how the other behaves.
        int drawSteps = trajOn ? std::clamp(trajectorySteps(), 10, 400) : 0;
        int botSteps = autoOn ? std::clamp(autoPlayLookahead(), 24, 480) : 0;
        int horizon = std::max(drawSteps, botSteps);

        if (horizon <= 0) {
            this->releaseBot();
            this->cancelTap();
            this->cancelWaveTransition();
            return;
        }

        SimRig rig;
        rig.pl = this;
        rig.dummy = m_fields->dummy1;
        rig.source = m_player1;
        rig.dt = dtStep;
        rig.roomAwareWave = waveRoomAwareEnabled();
        if (m_gameState.m_isDualMode && m_player2 && m_fields->dummy2 && !m_player2->m_isDead) {
            rig.dummy2 = m_fields->dummy2;
            rig.source2 = m_player2;
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

        // Finish an in-flight tap before anything else: the simulation that
        // validated it assumed the button stays down until the player has left
        // the ground after touching it. Releasing earlier would desync the
        // real input from the validated plan.
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
            BotAction action = BotAction::Release;
            const bool scheduledWave = m_player1->m_isDart && rig.roomAwareWave;
            bool waveDidSwitch = false;
            if (!scheduledWave && m_fields->waveTransitionActive) {
                this->cancelWaveTransition();
            }
            if (tapForcedHold) {
                action = BotAction::Hold;
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

            if (!tapForcedHold && !waveDidSwitch && (!scheduledWave || !m_fields->waveTransitionActive)) {
                WavePlan selectedWavePlan;
                action = decideAutoPress(
                    rig, idleResult, botSteps, m_fields->botHolding,
                    scheduledWave ? &selectedWavePlan : nullptr
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
                        "[AT] f={} x={:.0f} y={:.0f} gnd={} up={} mode={}{}{}{}{} dual={} "
                        "idle(died={} surv={}) tap={} action={}",
                        m_fields->logFrame, m_player1->getPositionX(), m_player1->getPositionY(),
                        m_player1->m_isOnGround, m_player1->m_isUpsideDown,
                        m_player1->m_isShip ? "ship" : "", m_player1->m_isBird ? "ufo" : "",
                        m_player1->m_isDart ? "wave" : "", m_player1->m_isSwing ? "swing" : "",
                        (!isFlyingMode(m_player1)) ? "ground" : "",
                        rig.dummy2 != nullptr, idleResult.died, idleResult.survived,
                        m_fields->botTapActive, static_cast<int>(action)
                    );
                }
            }

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
            this->cancelWaveTransition();
        }
    }

    void onQuit() {
        this->releaseBot();
        this->cancelTap();
        this->cancelWaveTransition();

        m_fields->drawNode = nullptr;
        m_fields->dummy1 = nullptr;
        m_fields->dummy2 = nullptr;
        m_fields->menu = nullptr;
        m_fields->autoLabel = nullptr;
        m_fields->trajLabel = nullptr;

        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Auto Trajectory v0.5.0 loaded");
}
