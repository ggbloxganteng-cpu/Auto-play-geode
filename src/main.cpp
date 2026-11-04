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
    cocos2d::CCPoint deathPos = {0.f, 0.f};
};

SimResult runSimulation(
    PlayLayer* pl,
    PlayerObject* dummy,
    PlayerObject* source,
    bool holdJump,
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
    if (holdJump) {
        dummy->pushButton(PlayerButton::Jump);
    } else {
        dummy->releaseButton(PlayerButton::Jump);
    }

    const bool flying = isFlyingMode(source);
    int airborneStreak = 0;
    int landedStreak = 0;

    cocos2d::CCPoint previous = dummy->getPosition();

    for (int i = 0; i < maxSteps; ++i) {
        dummy->update(dt);

        if (g_simDead || dummy->m_isDead) {
            result.died = true;
            result.deathPos = dummy->getPosition();
            if (draw && i <= drawSteps) {
                draw->drawDot(dummy->getPosition(), 4.f, color);
            }
            break;
        }

        resetCollisionLog(dummy);
        pl->checkCollisions(dummy, dt, false);

        if (g_simDead || dummy->m_isDead) {
            result.died = true;
            result.deathPos = dummy->getPosition();
            if (draw && i <= drawSteps) {
                draw->drawDot(dummy->getPosition(), 4.f, color);
            }
            break;
        }

        cocos2d::CCPoint current = dummy->getPosition();
        if (draw && i < drawSteps) {
            draw->drawSegment(previous, current, 0.8f, color);
        }
        previous = current;

        ++result.survived;

        if (!flying && dummy->m_isOnGround) {
            if (airborneStreak >= 15) {
                ++landedStreak;
                if (landedStreak >= 8) {
                    result.survived = maxSteps;
                    break;
                }
            } else {
                airborneStreak = 0;
            }
        } else {
            ++airborneStreak;
        }
    }

    dummy->releaseButton(PlayerButton::Jump);
    dummy->setVisible(false);

    g_simulating = false;
    g_simDummy = nullptr;
    return result;
}

class $modify(AutoTrajPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (g_simulating) return;

        if (auto pl = PlayLayer::get(); pl && pl->m_player1 == this) {
            if (dt > 0.001f && dt < 2.f) {
                float warp = pl->m_gameState.m_timeWarp;
                if (!(warp > 0.f)) warp = 1.f;
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

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        if (g_simulating && g_simDummy && player == g_simDummy) {
            g_simDead = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
        if (player == m_player1) {
            m_fields->botHolding = false;
        }
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        m_fields->botHolding = false;
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
            return;
        }

        if (m_player1->m_isPlatformer || m_gameState.m_isDualMode) {
            this->releaseBot();
            return;
        }

        float dtStep = g_stepDt;
        if (!(dtStep > 0.001f) || dtStep > 2.f) {
            dtStep = 0.25f;
        }

        int drawSteps = trajOn ? std::clamp(trajectorySteps(), 10, 400) : 0;
        int botSteps = autoOn ? std::clamp(autoPlayLookahead(), 24, 240) : 0;
        int horizon = std::max(drawSteps, botSteps);

        if (horizon <= 0) {
            this->releaseBot();
            return;
        }

        SimResult holdResult{};
        SimResult idleResult{};

        holdResult = runSimulation(
            this, m_fields->dummy1, m_player1, true, horizon, drawSteps, dtStep,
            drawSteps > 0 ? m_fields->drawNode : nullptr,
            cocos2d::ccColor4F{0.15f, 1.f, 0.35f, 1.f}
        );
        idleResult = runSimulation(
            this, m_fields->dummy1, m_player1, false, horizon, drawSteps, dtStep,
            drawSteps > 0 ? m_fields->drawNode : nullptr,
            cocos2d::ccColor4F{1.f, 0.3f, 0.3f, 1.f}
        );

        if (trajOn && holdResult.died && idleResult.died &&
            holdResult.survived == 0 && idleResult.survived == 0 && !g_warnedStepZero) {
            g_warnedStepZero = true;
            log::warn(
                "Auto Trajectory: simulation dies at step 0 near x={:.1f} y={:.1f}",
                m_player1->getPosition().x, m_player1->getPosition().y
            );
        }

        bool keepHold = false;
        if (autoOn) {
            if (idleResult.died) {
                keepHold = !holdResult.died ||
                           holdResult.survived > idleResult.survived + 2;
            }

            if (keepHold) {
                if (!m_fields->botHolding) {
                    this->handleButton(true, 1, true);
                    m_fields->botHolding = true;
                }
            } else {
                this->releaseBot();
            }
        } else {
            this->releaseBot();
        }
    }

    void onQuit() {
        this->releaseBot();

        m_fields->drawNode = nullptr;
        m_fields->dummy1 = nullptr;
        m_fields->menu = nullptr;
        m_fields->autoLabel = nullptr;
        m_fields->trajLabel = nullptr;

        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Auto Trajectory v0.2.0 loaded");
}
