#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace at::performance {

// Fixed-size, allocation-free cache for scan results derived from static level
// objects. The caller supplies a stable key (prefer a unique object ID combined
// with pointer identity) and a fingerprint of the fields relevant to its scan.
// Returns true only when the key was seen before with an identical fingerprint.
template <std::size_t Capacity = 256>
class StaticFingerprintCache {
    static_assert(Capacity > 0, "Cache capacity must be non-zero");

    struct Entry {
        std::uint64_t key = 0;
        std::uint64_t fingerprint = 0;
        bool occupied = false;
    };

public:
    bool unchanged(std::uint64_t key, std::uint64_t fingerprint) noexcept {
        const std::size_t start = hash(key) % Capacity;
        for (std::size_t offset = 0; offset < Capacity; ++offset) {
            auto& entry = m_entries[(start + offset) % Capacity];
            if (!entry.occupied) {
                entry = Entry{key, fingerprint, true};
                ++m_size;
                return false;
            }
            if (entry.key == key) {
                const bool same = entry.fingerprint == fingerprint;
                entry.fingerprint = fingerprint;
                return same;
            }
        }

        // Table is full: replace the home slot rather than allocating or growing.
        m_entries[start] = Entry{key, fingerprint, true};
        ++m_replacements;
        return false;
    }

    void clear() noexcept {
        for (auto& entry : m_entries) entry = Entry{};
        m_size = 0;
        m_replacements = 0;
    }

    std::size_t size() const noexcept { return m_size; }
    std::uint32_t replacements() const noexcept { return m_replacements; }

private:
    static std::uint64_t hash(std::uint64_t value) noexcept {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return value;
    }

    std::array<Entry, Capacity> m_entries{};
    std::size_t m_size = 0;
    std::uint32_t m_replacements = 0;
};

} // namespace at::performance
