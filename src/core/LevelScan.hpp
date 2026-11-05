#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <utility>

namespace at {
    enum class ObjectClass : unsigned char {
        Other,
        Hazard,
        Orb,
        Pad,
        Portal,
    };

    struct ObjectRecord {
        float x = 0.f;
        int id = 0;
        ObjectClass category = ObjectClass::Other;
    };

    struct CheckpointSummary {
        float startX = 0.f;
        float endX = 0.f;
        int objects = 0;
        int hazards = 0;
        int orbs = 0;
        int pads = 0;
        int portals = 0;
    };

    // Static level pre-scan. It intentionally records geometry metadata only;
    // interactive state and physics remain owned by the live simulation.
    class LevelScan {
    public:
        void build(std::vector<ObjectRecord> records, float routeStartX = 0.f) {
            m_records = std::move(records);
            m_checkpoints = {};
            m_minX = 0.f;
            m_maxX = 0.f;
            m_valid = !m_records.empty();
            if (!m_valid) return;

            auto bounds = std::minmax_element(
                m_records.begin(), m_records.end(),
                [](ObjectRecord const& a, ObjectRecord const& b) { return a.x < b.x; }
            );
            m_minX = std::min(bounds.first->x, routeStartX);
            m_maxX = bounds.second->x;
            if (m_maxX - m_minX < 1.f) m_maxX = m_minX + 1.f;

            for (auto const& record : m_records) {
                int index = checkpointFor(record.x);
                auto& cp = m_checkpoints[static_cast<std::size_t>(index)];
                ++cp.objects;
                switch (record.category) {
                    case ObjectClass::Hazard: ++cp.hazards; break;
                    case ObjectClass::Orb: ++cp.orbs; break;
                    case ObjectClass::Pad: ++cp.pads; break;
                    case ObjectClass::Portal: ++cp.portals; break;
                    default: break;
                }
            }
            for (int i = 0; i < 4; ++i) {
                auto& cp = m_checkpoints[static_cast<std::size_t>(i)];
                cp.startX = m_minX + (m_maxX - m_minX) * (static_cast<float>(i) / 4.f);
                cp.endX = m_minX + (m_maxX - m_minX) * (static_cast<float>(i + 1) / 4.f);
            }
        }

        bool valid() const { return m_valid; }
        float minX() const { return m_minX; }
        float maxX() const { return m_maxX; }
        std::size_t objectCount() const { return m_records.size(); }
        std::array<CheckpointSummary, 4> const& checkpoints() const { return m_checkpoints; }

        int checkpointFor(float x) const {
            if (!m_valid || x <= m_minX) return 0;
            if (x >= m_maxX) return 3;
            float progress = (x - m_minX) / (m_maxX - m_minX);
            return std::clamp(static_cast<int>(progress * 4.f), 0, 3);
        }

        int progressPercent(float x) const {
            if (!m_valid) return 0;
            float progress = (x - m_minX) / (m_maxX - m_minX);
            return std::clamp(static_cast<int>(progress * 100.f), 0, 100);
        }

        CheckpointSummary const* checkpoint(int index) const {
            if (!m_valid || index < 0 || index >= 4) return nullptr;
            return &m_checkpoints[static_cast<std::size_t>(index)];
        }

    private:
        std::vector<ObjectRecord> m_records;
        std::array<CheckpointSummary, 4> m_checkpoints{};
        float m_minX = 0.f;
        float m_maxX = 0.f;
        bool m_valid = false;
    };
}
