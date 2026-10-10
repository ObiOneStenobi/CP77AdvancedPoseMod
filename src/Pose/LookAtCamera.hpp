#pragma once

#include <atomic>
#include <cstdint>

// Photo Mode's "Look at camera" (attribute 15: 0 off, 1 camera, 2 custom; attribute 74 when on: 0 torso, 1 head,
// 2 eyes). While it is on, the game turns those bones towards the camera and has priority: our edits of the same
// bones are paused (the saved values stay).
namespace APM::Pose::LookAtCamera
{
inline std::atomic<int32_t> g_mode{0};   // attribute 15
inline std::atomic<int32_t> g_target{1}; // attribute 74

inline void OnPhotoModeAttribute(uint32_t aAttribute, float aValue)
{
    if (aAttribute == 15)
    {
        g_mode = static_cast<int32_t>(aValue + 0.5f);
    }
    else if (aAttribute == 74)
    {
        g_target = static_cast<int32_t>(aValue + 0.5f);
    }
}

// 0 nothing, 1 eyes, 2 head and eyes, 3 torso, head and eyes.
inline int32_t Reach()
{
    const int32_t mode = g_mode;
    if (mode == 0)
    {
        return 0;
    }
    if (mode != 1)
    {
        return 3; // custom: assume it may turn everything
    }
    const int32_t target = g_target;
    return target == 2 ? 1 : (target == 1 ? 2 : 3);
}
} // namespace APM::Pose::LookAtCamera
