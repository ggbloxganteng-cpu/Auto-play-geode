#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <unordered_set>
#include <vector>

// ---- BEGIN PORTABLE LOGIC (also compiled by the offline test harness) ----
namespace {
    bool g_simulating = false;
    bool g_simDead = false;
    bool g_simPortal = false;
    bool g_simInputDown = false;
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
    int g_simCurrentStep = -1;
    int g_simOrbContactCount = 0;
    int g_simOrbActivationCount = 0;
    int g_simFirstOrbContactStep = -1;
    int g_simFirstOrbActivationStep = -1;
    std::unordered_set<RingObject*> g_simRecordedOrbContacts;
    std::unordered_set<RingObject*> g_simRecordedOrbActivations;

    // Simulation tuning, all in physics substeps (240 per second).
    constexpr int kWaitMarginSteps = 2;    // safety margin when deciding to wait for a later jump
    constexpr int kDoomedMargin = 4;       // when nothing survives, press only if clearly better
    constexpr int kImminentSteps = 16;     // hazard this close: stop waiting, jump now if it works
    constexpr int kShortExtra = 100;       // fallback horizon: just past the first hazard
    constexpr int kTapProbeBack = 35;      // probe a tap this many substeps before the first hazard
    constexpr int kFlySwitchMargin = 2;    // flying: switch input only if clearly better (anti-flap)
    constexpr int kWaveLightMaxSteps = 108;
    constexpr int kWaveDeepMaxSteps = 144;     // lightweight bounded horizon (~0.6 s at 240 Hz)

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

    // These portals replace the player's control mode, dual-player topology or
    // linked destination. Size portals are intentionally excluded: the game's
    // collision path can update mini/normal size on the hidden clone directly.
    bool isTeleportOrb(GameObject* object) {
        return object && object->m_objectType == GameObjectType::TeleportOrb;
    }

    bool requiresPortalResync(int id) {
        switch (id) {
            case 12: case 13: case 47: case 111: // ball, ship, UFO, wave
            case 660: case 745: case 1331: case 1933: // wave, robot, spider, swing
            case 286: case 287:                      // dual
            case 747:                                // teleport
                return true;
            default:
                return false;
        }
    }

    bool isFlyingMode(PlayerObject* p) {
        return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing;
    }

    bool isBallMode(PlayerObject* p) {
        return p && p->m_isBall;
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

    bool ringUnusedByDummy(RingObject* ring, PlayerObject* player) {
        if (!ring || !player) return false;
        auto* effect = static_cast<EffectGameObject*>(ring);
        if (player == g_simDummies[0]) return !effect->m_activatedByPlayer1;
        if (player == g_simDummies[1]) return !effect->m_activatedByPlayer2;
        return false;
    }

    // A normal orb is not an automatic collision bonus: the player must be
    // pressing Jump when its hitbox is reached. This gate deliberately avoids
    // treating every RingObject contact as a successful orb activation.
    bool ringCanActivateForDummy(RingObject* ring, PlayerObject* player, bool inputDown) {
        return inputDown && ringUnusedByDummy(ring, player);
    }

    void resetSimOrbTracking() {
        g_simCurrentStep = -1;
        g_simOrbContactCount = 0;
        g_simOrbActivationCount = 0;
        g_simFirstOrbContactStep = -1;
        g_simFirstOrbActivationStep = -1;
        g_simRecordedOrbContacts.clear();
        g_simRecordedOrbActivations.clear();
    }

    void recordSimOrbContact(RingObject* ring, int step) {
        if (!ring) return;
        if (g_simRecordedOrbContacts.insert(ring).second) {
            ++g_simOrbContactCount;
        }
        if (g_simFirstOrbContactStep < 0 || step < g_simFirstOrbContactStep) {
            g_simFirstOrbContactStep = step;
        }
    }

    void recordSimOrbActivation(RingObject* ring, int step) {
        if (!ring || !g_simRecordedOrbActivations.insert(ring).second) return;
        ++g_simOrbActivationCount;
        if (g_simFirstOrbActivationStep < 0 || step < g_simFirstOrbActivationStep) {
            g_simFirstOrbActivationStep = step;
        }
    }

    bool isSimInteractable(GameObject* object) {
        if (!object) return false;
        // No-Touch can rewrite m_objectType to Decoration. Keep recognising
        // real ring instances by their runtime class so orbs are not filtered.
        if (typeinfo_cast<RingObject*>(object)) return true;
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
                // Gravity, speed and size portals are handled by the game's
                // normal collision path. Mode/dual/teleport portals remain
                // boundaries because they replace the player's control state
                // or require linked destination state the clone cannot model.
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
    int orbContacts = 0;
    int orbActivations = 0;
    int firstOrbContactStep = -1;
    int firstOrbActivationStep = -1;
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
    Tap,
    DelayedTap, // wait for a predicted orb contact, then issue a one-frame tap
    TimedHold // robot: hold for a validated number of physics substeps
};

static bool requiresFreshPressEdge(BotAction action, bool currentlyHolding) {
    return currentlyHolding && (action == BotAction::Tap || action == BotAction::TimedHold);
}

static bool advanceTapPulse(int& stepsRemaining, int frameSteps) {
    if (stepsRemaining <= 0) return true;
    stepsRemaining -= std::max(1, frameSteps);
    return stepsRemaining <= 0;
}

static bool advanceOrbTapSchedule(int& stepsRemaining, int frameSteps) {
    if (stepsRemaining <= 0) return true;
    stepsRemaining -= std::max(1, frameSteps);
    return stepsRemaining <= 0;
}

static bool orbTapScheduleStillReachable(SimResult const& idle, int remainingSteps) {
    // A death on the exact scheduled substep is still potentially preventable:
    // the simulation presses before that substep's update/collision pass.
    return !idle.died || idle.survived >= std::max(0, remainingSteps);
}

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
    bool deepWaveSearch = false; // optional wider search budget
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
    g_simInputDown = false;
    g_simObjectSnapshots.clear();
    g_simTouchedObjects.clear();
    resetSimOrbTracking();
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
        g_simInputDown = true;
    };
    auto releaseAll = [&]() {
        d1->releaseButton(PlayerButton::Jump);
        if (d2) d2->releaseButton(PlayerButton::Jump);
        g_simInputDown = false;
    };
    auto anyDead = [&]() {
        return g_simDead || d1->m_isDead || (d2 && d2->m_isDead);
    };

    const bool flying = isFlyingMode(s1);
    bool pressed = false;
    bool released = false;
    bool waveInputDown = false;
    int tapReleaseStep = -1;
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
        g_simCurrentStep = i;
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
                if (plan == SimPlan::Tap) {
                    // Tap is a frame-sized input pulse, whether it is used for
                    // a grounded jump, a ball flip, or an airborne orb contact.
                    tapReleaseStep = i + std::max(1, g_frameSubsteps);
                }
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

        // Match the live controller: Tap is a one-rendered-frame pulse. It
        // must also release while airborne; otherwise an orb tap that did not
        // cause takeoff can keep Jump locked until the old 180-step timeout.
        if (plan == SimPlan::Tap && pressed && !released &&
            tapReleaseStep >= 0 && i + 1 >= tapReleaseStep) {
            releaseAll();
            released = true;
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

    result.orbContacts = g_simOrbContactCount;
    result.orbActivations = g_simOrbActivationCount;
    result.firstOrbContactStep = g_simFirstOrbContactStep;
    result.firstOrbActivationStep = g_simFirstOrbActivationStep;

    releaseAll();
    restoreSimObjects();
    d1->setVisible(false);
    if (d2) d2->setVisible(false);

    g_simulating = false;
    g_simInputDown = false;
    resetSimOrbTracking();
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

// Cube / spider: issue a fresh one-frame jump pulse at a safe window.
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
    BotAction bestNowAction = BotAction::Release;
    int bestWait = 0;

    for (int hi = 0; hi < 2; ++hi) {
        const int steps = horizons[hi];
        if (hi == 1 && steps >= botSteps) break;

        // First test the action that can actually be issued now. The previous
        // implementation returned Release as soon as a delayed tap also
        // survived, even when Tap-now was already safe. That can defer Cube
        // jumps indefinitely as the same future window moves forward each frame.
        SimResult tapNow = probe(rig, SimPlan::Tap, 0, -1, steps);
        if (tapNow.survived > bestNow) {
            bestNow = tapNow.survived;
            bestNowAction = BotAction::Tap;
        }
        if (!tapNow.died) return BotAction::Tap;

        // Only postpone when the immediate tap fails but a later window works.
        SimResult tapWait = probe(rig, SimPlan::Tap, waitDelay, -1, steps);
        bestWait = std::max(bestWait, tapWait.survived);
        bool laterTapWorks = !tapWait.died;
        for (int waitFrames = 2; !laterTapWorks && waitFrames <= 5; ++waitFrames) {
            const int delay = waitFrames * frameSteps + kWaitMarginSteps;
            if (delay <= waitDelay || delay >= idle.survived) continue;
            SimResult delayed = probe(rig, SimPlan::Tap, delay, -1, steps);
            bestWait = std::max(bestWait, delayed.survived);
            laterTapWorks = !delayed.died;
        }

        const int ahead = idle.survived - kTapProbeBack;
        if (!laterTapWorks && ahead > waitDelay && ahead < steps) {
            SimResult tapAhead = probe(rig, SimPlan::Tap, ahead, -1, steps);
            bestWait = std::max(bestWait, tapAhead.survived);
            laterTapWorks = !tapAhead.died;
        }
        if (laterTapWorks) return BotAction::Release;

        // 2. Hold (bunny hop). Prefer it only after a one-frame tap is not
        // sufficient and no later tap window is currently viable.
        SimResult holdNow = probe(rig, SimPlan::Hold, 0, -1, steps);
        if (holdNow.survived > bestNow) {
            bestNow = holdNow.survived;
            bestNowAction = BotAction::Hold;
        }
        if (!holdNow.died) return BotAction::Hold;

        bool earlyAtWait = !tapWait.late;
        SimResult holdWait = probe(rig, SimPlan::Hold, waitDelay, -1, steps);
        bestWait = std::max(bestWait, holdWait.survived);
        if (!holdWait.died) return BotAction::Release;
        if (!holdWait.late) earlyAtWait = true;

        // Nothing works now or one frame later, but the failures look like
        // "too early": the window is still ahead.
        if (earlyAtWait && idle.survived > waitDelay + kImminentSteps) {
            return BotAction::Release;
        }
    }

    // Nothing survives: only press if it is clearly the longest-lasting option.
    if (bestNow > std::max(idle.survived, bestWait) + kDoomedMargin) {
        return bestNowAction;
    }
    return BotAction::Release;
}

// Robot: jump height depends on how long Jump remains held. A plain Tap
// under-predicts tall jumps, while Hold-forever cannot reliably set up a
// second jump. Probe a bounded set of frame-aligned hold durations and release
// at the same postUpdate cadence used by the live controller.
static BotAction decideRobot(
    SimRig const& rig, SimResult const& idle, int botSteps, int* selectedHoldSteps
) {
    if (selectedHoldSteps) *selectedHoldSteps = 0;
    const int frameSteps = std::clamp(g_frameSubsteps, 1, 16);
    const int waitDelay = frameSteps + kWaitMarginSteps;
    // Dense short holds give the Robot more control around low ceilings and
    // chained platforms; longer holds remain available for tall jumps.
    const int frames[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24};

    int bestSurvived = idle.survived;
    int bestHoldSteps = 0;
    // Try an immediate hold first. If it already survives the full horizon,
    // delaying just because a later hold also survives causes the same
    // never-commit behavior that affected Cube.
    for (int frameCount : frames) {
        const int holdSteps = frameCount * frameSteps;
        SimResult now = probe(rig, SimPlan::Hold, 0, holdSteps, botSteps);
        if (!now.died) {
            if (selectedHoldSteps) *selectedHoldSteps = holdSteps;
            return BotAction::TimedHold;
        }
        if (now.survived > bestSurvived) {
            bestSurvived = now.survived;
            bestHoldSteps = holdSteps;
        }
    }

    // When every immediate hold fails, see whether waiting a frame opens a
    // viable jump window before committing to a partial best-effort action.
    for (int frameCount : frames) {
        const int holdSteps = frameCount * frameSteps;
        SimResult wait = probe(rig, SimPlan::Hold, waitDelay, holdSteps, botSteps);
        if (!wait.died) return BotAction::Release;
    }

    if (bestHoldSteps > 0 && bestSurvived > idle.survived + kDoomedMargin) {
        if (selectedHoldSteps) *selectedHoldSteps = bestHoldSteps;
        return BotAction::TimedHold;
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

// Wave planner: test frame-aligned one-switch corrections after the two
// straight paths fail. It remains opt-in because even bounded search costs more.
static BotAction decideWave(
    SimRig const& rig, SimResult const& idle, int lookahead, bool currentlyHolding,
    WavePlan* selectedPlan = nullptr
) {
    if (selectedPlan) *selectedPlan = WavePlan{};
    const int botSteps = std::min(
        lookahead, rig.deepWaveSearch ? kWaveDeepMaxSteps : kWaveLightMaxSteps
    );
    const int frameSteps = std::clamp(g_frameSubsteps, 1, 8);

    struct Candidate {
        WavePlan plan;
        bool alive = false;
        int survived = 0;
        int switches = 0;
    };
    Candidate candidates[6];
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

    const bool opposite = !currentlyHolding;
    // Bounded beam: two straight paths first. The light profile adds two
    // one-switch corrections over 108 steps; the optional deep profile adds
    // four timing probes over 144 steps. Corrections run only when both straight
    // paths fail. Light is cheaper but less reliable in some corridor layouts.
    add(currentlyHolding, {});
    add(opposite, {});
    add(currentlyHolding, {1 * frameSteps});
    add(opposite, {1 * frameSteps});

    bool anyAlive = false;
    int bestIndex = -1;
    int bestHoldSurvival = 0;
    int bestReleaseSurvival = 0;
    auto evaluate = [&](int i) {
        SimResult result = probeWave(rig, candidates[i].plan, botSteps);
        Candidate& candidate = candidates[i];
        candidate.alive = !result.died;
        candidate.survived = result.survived;
        anyAlive = anyAlive || candidate.alive;
        if (candidate.plan.startHolding) bestHoldSurvival = std::max(bestHoldSurvival, result.survived);
        else bestReleaseSurvival = std::max(bestReleaseSurvival, result.survived);
        if (!candidate.alive) return;
        if (bestIndex < 0) { bestIndex = i; return; }
        Candidate const& best = candidates[bestIndex];
        bool better = candidate.survived > best.survived;
        if (candidate.survived == best.survived) {
            if (candidate.switches < best.switches) better = true;
            else if (candidate.switches == best.switches &&
                     candidate.plan.startHolding == currentlyHolding &&
                     best.plan.startHolding != currentlyHolding) better = true;
        }
        if (better) bestIndex = i;
    };

    // Fast path: evaluate the two straight controls first. If either survives
    // the whole horizon, corrections cannot improve survival and are skipped.
    evaluate(0);
    evaluate(1);
    if (bestIndex >= 0) {
        if (selectedPlan) *selectedPlan = candidates[bestIndex].plan;
        return candidates[bestIndex].plan.startHolding ? BotAction::Hold : BotAction::Release;
    }

    // Both straight paths failed. The light profile uses a single early
    // correction in each starting direction. The deep profile spends four
    // extra probes to compare early and later correction timings.
    if (rig.deepWaveSearch) {
        add(currentlyHolding, {2 * frameSteps});
        add(currentlyHolding, {4 * frameSteps});
        add(opposite, {2 * frameSteps});
        add(opposite, {4 * frameSteps});
    } else {
        add(true, {frameSteps});
        add(false, {frameSteps});
    }

    // Only spend the correction-search budget when both straight paths fail.
    for (int i = 2; i < count; ++i) evaluate(i);

    if (!anyAlive || bestIndex < 0) {
        if (bestHoldSurvival > bestReleaseSurvival + kFlySwitchMargin) return BotAction::Hold;
        if (bestReleaseSurvival > bestHoldSurvival + kFlySwitchMargin) return BotAction::Release;
        return currentlyHolding ? BotAction::Hold : BotAction::Release;
    }

    if (selectedPlan) *selectedPlan = candidates[bestIndex].plan;
    return candidates[bestIndex].plan.startHolding ? BotAction::Hold : BotAction::Release;
}

// Ball is an edge-triggered gravity switch, not a variable-height jump. Probe
// one-frame flips at several future windows; never leave Jump held as a way to
// request repeated ball flips.
static BotAction decideBall(SimRig const& rig, SimResult const& idle, int botSteps) {
    if (!(idle.died && idle.survived < botSteps)) return BotAction::Release;
    const int frameSteps = std::clamp(g_frameSubsteps, 1, 16);

    SimResult now = probe(rig, SimPlan::Tap, 0, -1, botSteps);
    if (!now.died) return BotAction::Tap;

    int bestSurvived = std::max(idle.survived, now.survived);
    // If an immediate flip is not enough, try upcoming frame-aligned windows.
    // Return Release only when a later flip really outlives the current one.
    for (int waitFrames = 1; waitFrames <= 4; ++waitFrames) {
        const int delay = waitFrames * frameSteps;
        if (delay >= idle.survived || delay >= botSteps) break;
        SimResult later = probe(rig, SimPlan::Tap, delay, -1, botSteps);
        if (!later.died) return BotAction::Release;
        bestSurvived = std::max(bestSurvived, later.survived);
    }

    // Even a partial improvement matters in Ball: this pulse changes gravity,
    // then the controller gets another decision window after the flip.
    if (now.survived > idle.survived + kDoomedMargin &&
        now.survived >= bestSurvived) return BotAction::Tap;
    return BotAction::Release;
}

struct OrbTapCandidate {
    int delaySteps = 0;
    SimResult result{};
};

struct OrbTapPlan {
    bool found = false;
    int delaySteps = 0;
    int survived = 0;
    int activations = 0;
};

static OrbTapPlan chooseOrbTapCandidate(
    SimResult const& idle, OrbTapCandidate const* candidates, int count
) {
    OrbTapPlan best;
    for (int i = 0; i < count; ++i) {
        auto const& candidate = candidates[i];
        auto const& result = candidate.result;
        // A mere overlap is not enough: the hidden player must have really
        // executed RingObject::ringJump, and the click must improve survival.
        if (result.orbActivations <= 0) continue;
        if (result.died && result.survived <= idle.survived + kDoomedMargin) continue;
        if (!best.found || result.survived > best.survived ||
            (result.survived == best.survived && candidate.delaySteps < best.delaySteps)) {
            best.found = true;
            best.delaySteps = candidate.delaySteps;
            best.survived = result.survived;
            best.activations = result.orbActivations;
        }
    }
    return best;
}

static OrbTapPlan findOrbTapPlan(SimRig const& rig, SimResult const& idle, int botSteps) {
    OrbTapPlan none;
    if (!idle.died || idle.survived >= botSteps || idle.orbContacts <= 0 ||
        idle.firstOrbContactStep < 0 || idle.firstOrbContactStep >= botSteps) {
        return none;
    }

    const int frameSteps = std::clamp(g_frameSubsteps, 1, 16);
    const int contact = idle.firstOrbContactStep;
    // Do not spend extra simulations on orbs far down the level. Re-evaluate
    // as the real player approaches; this keeps scheduled clicks short-lived.
    if (contact > frameSteps * 10) return none;

    int firstDelay = std::max(0, contact - frameSteps);
    firstDelay = (firstDelay / frameSteps) * frameSteps;
    const int lastDelay = std::min(botSteps - 1, contact + 2 * frameSteps);
    const int probeSteps = std::min(
        botSteps,
        std::max(contact + 2 * frameSteps + kShortExtra,
                 idle.survived + kShortExtra)
    );

    // Scan three nearby frame-aligned click windows. The actual activation is
    // confirmed by ringJump(), not by proximity alone.
    OrbTapCandidate candidates[4];
    int count = 0;
    for (int delay = firstDelay; delay <= lastDelay && count < 4; delay += frameSteps) {
        candidates[count].delaySteps = delay;
        candidates[count].result = probe(rig, SimPlan::Tap, delay, -1, probeSteps);
        ++count;
    }
    return chooseOrbTapCandidate(idle, candidates, count);
}

// Decides what the bot should do this frame.
// `idle` is the "release now" simulation (it may be longer than botSteps).
BotAction decideAutoPress(
    SimRig const& rig, SimResult const& idle, int botSteps, bool currentlyHolding,
    WavePlan* selectedWavePlan = nullptr, int* selectedTimedHoldSteps = nullptr,
    int* selectedOrbTapDelaySteps = nullptr
) {
    PlayerObject* p = rig.source;
    if (selectedOrbTapDelaySteps) *selectedOrbTapDelaySteps = 0;

    // Orbs need a click at the moment their hitbox is crossed. Simulate a few
    // nearby frame-aligned pulses and return a live delayed-click action when
    // the best pulse is not due yet.
    OrbTapPlan orbPlan = findOrbTapPlan(rig, idle, botSteps);
    if (orbPlan.found) {
        if (selectedOrbTapDelaySteps) *selectedOrbTapDelaySteps = orbPlan.delaySteps;
        return orbPlan.delaySteps <= 0 ? BotAction::Tap : BotAction::DelayedTap;
    }

    if (p->m_isDart) {
        if (rig.roomAwareWave) {
            return decideWave(rig, idle, botSteps, currentlyHolding, selectedWavePlan);
        }
        if (selectedWavePlan) *selectedWavePlan = WavePlan{};
        return decideWaveClassic(rig, idle, botSteps, currentlyHolding);
    }

    // Ball flips gravity on a fresh input edge, so it needs its own pulse planner.
    if (isBallMode(p)) {
        return decideBall(rig, idle, botSteps);
    }

    // Nothing dangerous inside the lookahead window: stay released.
    if (!(idle.died && idle.survived < botSteps)) {
        return BotAction::Release;
    }

    if (isFlyingMode(p)) return decideFlying(rig, idle, botSteps, currentlyHolding);
    if (p->m_isRobot) return decideRobot(rig, idle, botSteps, selectedTimedHoldSteps);
    return decideGround(rig, idle, botSteps);
}
// ---- END PORTABLE LOGIC ----
