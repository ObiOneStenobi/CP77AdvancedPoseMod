#pragma once

#include <atomic>
#include <cstdint>

// The characters that can be posed in Photo Mode: 0 = V, 1-3 = the characters added in the scene (the three Photo
// Mode slots, PhotoModeSystem +0x1D0 / +0x1D8 / +0x1E0). Every character has its own pose (PoseOverrides); the panel,
// the gizmos and the script functions work on the selected one.
namespace APM::Pose::Characters
{
constexpr uint32_t Count = 4;

inline std::atomic<uint32_t> g_selected{0};

inline uint32_t Selected()
{
    return g_selected.load(std::memory_order_relaxed);
}

inline void Select(uint32_t aCharacter)
{
    g_selected = aCharacter < Count ? aCharacter : 0;
}
} // namespace APM::Pose::Characters
