#pragma once

#include "Math/Vec3.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// Photo Mode's "Look at camera" (mode 0 off, 1 camera, 2 custom; target 0 torso, 1 head, 2 eyes), for V and for each
// added character. While it is on, the game turns those bones towards the camera and has priority: our edits of the same
// bones are paused (the saved values stay).
namespace APM::Pose::LookAtCamera
{
// Per character (Pose/Characters.hpp): 0 = V, 1-3 = the added characters.
inline std::array<std::atomic<int32_t>, 4> g_mode{};   // V: attribute 15, added characters: 58
inline std::array<std::atomic<int32_t>, 4> g_target{}; // V: attribute 74, added characters: 78
// The added character the Photo Mode NPC page works on (attribute 68, its character selector): attributes 58 / 78
// belong to that character.
inline std::atomic<int32_t> g_selectedNpc{0};
// Photo Mode pose of each character (V: attributes 5 category / 6 pose, added characters: 56 / 57): the generation
// goes up when it changes, so data remembered for one pose is not used on another.
inline std::array<std::array<std::atomic<int32_t>, 2>, 4> g_pose{};
inline std::array<std::atomic<uint32_t>, 4> g_poseGeneration{};

inline void SetPose(size_t aCharacter, size_t aField, int32_t aValue)
{
    if (aCharacter < g_pose.size() && g_pose[aCharacter][aField].exchange(aValue) != aValue)
    {
        ++g_poseGeneration[aCharacter];
    }
}

inline uint32_t PoseGeneration(uint32_t aCharacter)
{
    return aCharacter < g_poseGeneration.size() ? g_poseGeneration[aCharacter].load() : 0;
}

inline void Reset()
{
    for (size_t i = 0; i < g_mode.size(); ++i)
    {
        g_mode[i] = 0;
        g_target[i] = 1;
        g_pose[i][0] = -1;
        g_pose[i][1] = -1;
        ++g_poseGeneration[i];
    }
    g_selectedNpc = 0;
}

inline void OnPhotoModeAttribute(uint32_t aAttribute, float aValue)
{
    const int32_t value = static_cast<int32_t>(aValue + 0.5f);
    if (aAttribute == 68)
    {
        g_selectedNpc = value; // the page then sends 58 / 78 of that character
        return;
    }
    const int32_t npc = g_selectedNpc;
    if ((aAttribute == 58 || aAttribute == 78 || aAttribute == 56 || aAttribute == 57) && (npc < 0 || npc >= 3))
    {
        return; // no added character selected in the NPC page
    }
    const size_t character = static_cast<size_t>(npc) + 1;
    switch (aAttribute)
    {
    case 15: g_mode[0] = value; break;
    case 74: g_target[0] = value; break;
    case 58: g_mode[character] = value; break;
    case 78: g_target[character] = value; break;
    case 5: SetPose(0, 0, value); break;
    case 6: SetPose(0, 1, value); break;
    case 56: SetPose(character, 0, value); break;
    case 57: SetPose(character, 1, value); break;
    default: break;
    }
}

inline bool IsLookAtAttribute(uint32_t aAttribute)
{
    return aAttribute == 15 || aAttribute == 74 || aAttribute == 58 || aAttribute == 78 || aAttribute == 68 ||
           aAttribute == 5 || aAttribute == 6 || aAttribute == 56 || aAttribute == 57;
}

// 0 nothing, 1 eyes, 2 head and eyes, 3 torso, head and eyes.
inline int32_t Reach(uint32_t aCharacter)
{
    if (aCharacter >= g_mode.size())
    {
        return 0;
    }
    const int32_t mode = g_mode[aCharacter];
    if (mode == 0)
    {
        return 0;
    }
    if (mode != 1)
    {
        return 3; // custom: assume it may turn everything
    }
    const int32_t target = g_target[aCharacter];
    return target == 2 ? 1 : (target == 1 ? 2 : 3);
}

// Photo Mode turns the bones towards the camera itself ("camera"); with "custom" it turns them towards a point the
// user places, which the plugin does not know: those bones are then left as the game animates them.
inline bool AimsAtCamera(uint32_t aCharacter)
{
    return aCharacter < g_mode.size() && g_mode[aCharacter] == 1;
}

inline bool IsCustom(uint32_t aCharacter)
{
    return aCharacter < g_mode.size() && g_mode[aCharacter] == 2;
}

// The active camera in a character's model space, sent by the editor every frame (used to turn a full base pose
// towards it).
struct CameraView
{
    Math::Vec3 position{};
    Math::Vec3 forward{};
    Math::Vec3 up{};
    bool valid = false;
};

// Per character: each one's model space is its own.
inline std::mutex g_cameraMutex;
inline std::array<CameraView, 4> g_cameras{};

inline void SetCamera(uint32_t aCharacter, const Math::Vec3& aPosition, const Math::Vec3& aForward, const Math::Vec3& aUp)
{
    if (aCharacter >= g_cameras.size())
    {
        return;
    }
    std::scoped_lock lock(g_cameraMutex);
    g_cameras[aCharacter] = {aPosition, aForward, aUp, Math::Length(aForward) > 1e-4f};
}

inline void ForgetCamera()
{
    std::scoped_lock lock(g_cameraMutex);
    for (auto& camera : g_cameras)
    {
        camera.valid = false;
    }
}

inline CameraView GetCamera(uint32_t aCharacter)
{
    std::scoped_lock lock(g_cameraMutex);
    return aCharacter < g_cameras.size() ? g_cameras[aCharacter] : CameraView{};
}

// A new character took that slot: its look-at state starts as the game's default.
inline void ResetCharacter(uint32_t aCharacter)
{
    if (aCharacter >= g_mode.size())
    {
        return;
    }
    g_mode[aCharacter] = 0;
    g_target[aCharacter] = 1;
    g_pose[aCharacter][0] = -1;
    g_pose[aCharacter][1] = -1;
    ++g_poseGeneration[aCharacter];
    std::scoped_lock lock(g_cameraMutex);
    g_cameras[aCharacter].valid = false;
}
} // namespace APM::Pose::LookAtCamera
