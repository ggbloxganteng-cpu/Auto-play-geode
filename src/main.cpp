#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    bool g_simulating = false;
    bool g_simDead = false;
    PlayerObject* g_simDummy = nullptr;

    bool autoPlayEnabled() {
        return Mod::get()->getSettingValue<bool>("auto-play");
    }

    bool trajectoryEnabled() {
        return Mod::get()->getSettingValue<bool>("show-trajectory");
    }

    bool uiEnabled() {
        return Mod::get()->getSettingValue<bool>("show-ui");
    }

    int64_t trajectorySteps() {
        return Mod::get()->getSettingValue<int64_t>("trajectory-steps");
    }

    int64_t autoPlayLookahead() {
        return Mod::get()->getSettingValue<int64_t>("autoplay-lookahead");
    }
}

struct SimResult {
    bool died = false;
    int survived = 0;
    cocos2d::CCPoint end = {0.f, 0.f};
};

SimResult runSimulation(
    PlayLayer* pl,
    PlayerObject* dummy,
    PlayerObject* source,
    float dt,
    bool holdJump,
    int maxSteps,
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

    dummy->copyAttributes(source);
    dummy->setPosition(source->getPosition());
    dummy->setVisible(false);

    dummy->m_yVelocity = source->m_yVelocity;
    dummy->m_gravityMod = source->m_gravityMod;
    dummy->m_isOnGround = source->m_isOnGround;
    dummy->m_isUpsideDown = source->m_isUpsideDown;
    dummy->m_speedMultiplier = source->m_speedMultiplier;
    dummy->m_playerSpeed = source->m_playerSpeed;
    dummy->m_vehicleSize = source->m_vehicleSize;
    dummy->m_isDead = false;
    dummy->releaseAllButtons();
    dummy->m_isDead = false;

    auto resetCollisionLogs = [dummy]() {
        if (dummy->m_collisionLogTop) dummy->m_collisionLogTop->removeAllObjects();
        if (dummy->m_collisionLogBottom) dummy->m_collisionLogBottom->removeAllObjects();
        if (dummy->m_collisionLogLeft) dummy->m_collisionLogLeft->removeAllObjects();
        if (dummy->m_collisionLogRight) dummy->m_collisionLogRight->removeAllObjects();
    };

    cocos2d::CCPoint previous = dummy->getPosition();

    for (int i = 0; i < maxSteps; ++i) {
        if (g_simDead || dummy->m_isDead) {
            result.died = true;
            break;
        }

        resetCollisionLogs();
        pl->checkCollisions(dummy, dt, false);

        if (g_simDead) {
            result.died = true;
            result.end = dummy->getPosition();
            break;
        }

        if (i == 0) {
            if (holdJump) {
                dummy->pushButton(PlayerButton::Jump);
            } else {
                dummy->releaseButton(PlayerButton::Jump);
            }
        }

        dummy->update(dt);

        cocos2d::CCPoint current = dummy->getPosition();
        if (draw) {
            draw->drawSegment(previous, current, 0.8f, color);
        }
        previous = current;

        result.survived = i + 1;
        result.end = current;
    }

    g_simulating = false;
    g_simDummy = nullptr;
    return result;
}

class $modify(AutoTrajBaseLayer, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        if (g_simulating && player == g_simDummy && objects) {
            gd::vector<GameObject*> filtered;
            for (int i = 0; i < count; ++i) {
                auto* object = objects->at(i);
                if (!object) continue;

                switch (object->m_objectType) {
                    case GameObjectType::Solid:
                    case GameObjectType::Hazard:
                    case GameObjectType::AnimatedHazard:
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
        PlayerObject* dummy2 = nullptr;
        cocos2d::CCMenu* menu = nullptr;
        cocos2d::CCLabelBMFont* autoLabel = nullptr;
        cocos2d::CCLabelBMFont* trajLabel = nullptr;
        bool botHolding = false;
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
        m_fields->dummy2 = PlayerObject::create(1, 1, this, this, true);

        if (m_objectLayer) {
            if (m_fields->dummy1) {
                m_fields->dummy1->setVisible(false);
                m_fields->dummy1->setID("autotraj-dummy-1"_spr);
                m_objectLayer->addChild(m_fields->dummy1, 999);
            }
            if (m_fields->dummy2) {
                m_fields->dummy2->setVisible(false);
                m_fields->dummy2->setID("autotraj-dummy-2"_spr);
                m_objectLayer->addChild(m_fields->dummy2, 999);
            }
        }

        this->setupMenu();
        m_fields->botHolding = false;

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

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        if (g_simulating && g_simDummy && player == g_simDummy) {
            g_simDead = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        m_fields->botHolding = false;
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

        if (!m_player1 || m_player1->m_isDead) {
            if (m_fields->botHolding) {
                this->handleButton(false, 1, true);
                m_fields->botHolding = false;
            }
            return;
        }

        float step = dt;
        if (m_gameState.m_timeWarp > 0.01f) {
            step = dt / m_gameState.m_timeWarp;
        }

        if (trajOn && m_fields->dummy1) {
            const int steps = static_cast<int>(trajectorySteps());
            runSimulation(
                this, m_fields->dummy1, m_player1, step, true, steps,
                m_fields->drawNode, cocos2d::ccColor4F{0.1f, 1.f, 0.3f, 1.f}
            );
            runSimulation(
                this, m_fields->dummy1, m_player1, step, false, steps,
                m_fields->drawNode, cocos2d::ccColor4F{1.f, 0.25f, 0.25f, 1.f}
            );
        }

        if (autoOn && m_fields->dummy2) {
            const int lookahead = static_cast<int>(autoPlayLookahead());

            SimResult stay = runSimulation(
                this, m_fields->dummy2, m_player1, step, false, lookahead,
                nullptr, cocos2d::ccColor4F{0.f, 0.f, 0.f, 0.f}
            );

            bool shouldJump = false;
            if (stay.died) {
                SimResult jump = runSimulation(
                    this, m_fields->dummy2, m_player1, step, true, lookahead,
                    nullptr, cocos2d::ccColor4F{0.f, 0.f, 0.f, 0.f}
                );
                shouldJump = !jump.died || (jump.survived > stay.survived + 3);
            }

            if (shouldJump) {
                if (!m_fields->botHolding) {
                    this->handleButton(true, 1, true);
                    m_fields->botHolding = true;
                }
            } else if (m_fields->botHolding) {
                this->handleButton(false, 1, true);
                m_fields->botHolding = false;
            }
        } else if (m_fields->botHolding) {
            this->handleButton(false, 1, true);
            m_fields->botHolding = false;
        }
    }

    void onQuit() {
        if (m_fields->botHolding && m_player1) {
            this->handleButton(false, 1, true);
            m_fields->botHolding = false;
        }

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
    log::info("Auto Trajectory loaded");
}
