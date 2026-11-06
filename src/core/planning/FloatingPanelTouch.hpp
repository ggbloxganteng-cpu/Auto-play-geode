#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace at::ui {
    // Shared panel geometry (used by main.cpp layout and by the touch layer).
    inline constexpr float kPanelWidth = 210.f;
    inline constexpr float kPanelExpandedHeight = 156.f;
    inline constexpr float kPanelCollapsedHeight = 34.f;
    inline constexpr float kPanelHeaderBand = 26.f;

    // Cocos2d-x 2.x targeted-touch delegate used by the floating panel.
    // It only captures touches that begin on the panel header; normal gameplay
    // touches are passed through to PlayLayer and its other delegates.
    class FloatingPanelTouchLayer final : public cocos2d::CCLayer {
    public:
        static FloatingPanelTouchLayer* create(
            cocos2d::CCNode* owner,
            cocos2d::CCLayerColor* panel,
            bool* collapsed,
            std::function<void()> onHeaderTap
        ) {
            auto* layer = new FloatingPanelTouchLayer();
            if (layer && layer->init()) {
                layer->m_owner = owner;
                layer->m_panel = panel;
                layer->m_collapsed = collapsed;
                layer->m_onHeaderTap = std::move(onHeaderTap);
                layer->autorelease();
                return layer;
            }
            delete layer;
            return nullptr;
        }

        // Called once after the user finishes dragging the panel (not on taps).
        void setOnDragEnd(std::function<void()> cb) { m_onDragEnd = std::move(cb); }

        void registerWithTouchDispatcher() override {
            // CCMenu uses -128. Register just after it so its buttons keep
            // normal click behavior while the otherwise-empty header can drag.
            cocos2d::CCTouchDispatcher::get()
                ->addTargetedDelegate(this, -127, true);
        }

        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            if (!touch || !m_owner || !m_panel || !m_panel->isVisible() || !m_collapsed) {
                return false;
            }
            auto local = m_panel->convertToNodeSpace(touch->getLocation());
            const float headerBottom = *m_collapsed ? 0.f : kPanelExpandedHeight - kPanelHeaderBand;
            const float headerTop = *m_collapsed ? kPanelCollapsedHeight : kPanelExpandedHeight;
            if (local.x < 0.f || local.x > kPanelWidth || local.y < headerBottom || local.y > headerTop) {
                return false;
            }
            m_active = true;
            m_moved = false;
            m_touchStart = m_owner->convertToNodeSpace(touch->getLocation());
            m_panelStart = m_panel->getPosition();
            return true;
        }

        void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override {
            if (!m_active || !touch || !m_owner || !m_panel) return;
            auto current = m_owner->convertToNodeSpace(touch->getLocation());
            auto delta = current - m_touchStart;
            if (std::fabs(delta.x) + std::fabs(delta.y) > 5.f) m_moved = true;
            if (!m_moved) return;

            auto size = cocos2d::CCDirector::sharedDirector()->getWinSize();
            const float height = (*m_collapsed) ? kPanelCollapsedHeight : kPanelExpandedHeight;
            const float x = std::clamp(m_panelStart.x + delta.x, 0.f,
                                       std::max(0.f, size.width - kPanelWidth));
            const float y = std::clamp(m_panelStart.y + delta.y, 0.f,
                                       std::max(0.f, size.height - height));
            m_panel->setPosition({x, y});
        }

        void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override {
            this->finishTouch(true);
        }

        void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override {
            this->finishTouch(false);
        }

    private:
        cocos2d::CCNode* m_owner = nullptr;
        cocos2d::CCLayerColor* m_panel = nullptr;
        bool* m_collapsed = nullptr;
        std::function<void()> m_onHeaderTap;
        std::function<void()> m_onDragEnd;
        cocos2d::CCPoint m_touchStart = {0.f, 0.f};
        cocos2d::CCPoint m_panelStart = {0.f, 0.f};
        bool m_active = false;
        bool m_moved = false;

        void finishTouch(bool allowTap) {
            if (!m_active) return;
            m_active = false;
            if (m_moved && m_onDragEnd) m_onDragEnd();
            if (allowTap && !m_moved && m_collapsed) {
                *m_collapsed = !*m_collapsed;
                if (m_onHeaderTap) m_onHeaderTap();
            }
        }

        bool init() override {
            if (!cocos2d::CCLayer::init()) return false;
            this->setTouchEnabled(true);
            this->setTouchMode(cocos2d::kCCTouchesOneByOne);
            return true;
        }
    };
}
