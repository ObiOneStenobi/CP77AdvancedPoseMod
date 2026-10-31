#pragma once

#include "Math/Vec3.hpp"

#include <array>
#include <atomic>
#include <chrono>
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
// Photo Mode pose of each character (V: attributes 5 category / 6 pose, added characters: 57 pose): the generation
// goes up when it changes, so data remembered for one pose is not used on another.
inline std::array<std::array<std::atomic<int32_t>, 2>, 4> g_pose{};
inline std::array<std::atomic<uint32_t>, 4> g_poseGeneration{};
// Look at camera left out for a character: V when the player itself is posed in a vehicle (the menu option is off
// there, but a mod restoring the last settings can still send it on; the game does not turn the player then).
inline std::array<std::atomic<bool>, 4> g_ignored{};

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

// The pose without the game's own turn (PoseProcessor's RemoveGameLookAt) is remembered while Look at camera is off. A
// Photo Mode pose chosen with it on (or Photo Mode opened with it on) never showed that: Photo Mode's own Look at camera
// is then switched off in the game for a moment (PhotoModeMenuGuard::UpdateGameLookAt, the plugin's state stays as it
// is), the pose is remembered once the game's turn has faded out, and the game's Look at camera goes back on.
// The pose is taken once its bones hold still (the game's turn faded out, the new Photo Mode pose blended in: 1 s was
// not always enough, test 31/10), at least GameLookAtFadeMs after, at most GameLookAtSettleMaxMs.
constexpr uint64_t GameLookAtFadeMs = 1000;      // the game's turn fading out (preset speed 140 degrees/s)
constexpr uint64_t GameLookAtSettleMaxMs = 5000; // taken anyway (a pose that keeps moving)
constexpr uint64_t GameLookAtOffMaxMs = 6000;    // back on anyway (the character's pose may not be processed)
inline std::array<std::atomic<bool>, 4> g_wantsCleanFrame{};  // anim thread: no pose without the game's turn yet
inline std::array<std::atomic<bool>, 4> g_poseSettled{};      // anim thread: the bones hold still
inline std::array<std::atomic<uint64_t>, 4> g_gameOffSince{}; // when the game's turn was switched off (ms, 0 = on)
inline std::array<std::atomic<uint32_t>, 4> g_gameOffGeneration{}; // Photo Mode pose it was switched off for
inline std::array<std::atomic<bool>, 4> g_cleanFrameTaken{};  // anim thread: remembered, the game's turn can go back on

inline uint64_t NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The game's turn is off and has faded out on the current Photo Mode pose: this frame is the pose without it.
inline bool GameLookAtClean(uint32_t aCharacter)
{
    if (aCharacter >= g_gameOffSince.size())
    {
        return false;
    }
    const uint64_t since = g_gameOffSince[aCharacter];
    if (since == 0 || g_gameOffGeneration[aCharacter] != PoseGeneration(aCharacter))
    {
        return false;
    }
    const uint64_t elapsed = NowMs() - since;
    return elapsed >= GameLookAtSettleMaxMs || (elapsed >= GameLookAtFadeMs && g_poseSettled[aCharacter]);
}

// The game's turn is off but still fading out (or the pose still blending in): wait for the clean frame.
inline bool AwaitsCleanFrame(uint32_t aCharacter)
{
    return aCharacter < g_gameOffSince.size() && g_gameOffSince[aCharacter] != 0 && !GameLookAtClean(aCharacter);
}

inline void ResetCleanFrame(uint32_t aCharacter)
{
    g_wantsCleanFrame[aCharacter] = false;
    g_poseSettled[aCharacter] = false;
    g_gameOffSince[aCharacter] = 0;
    g_cleanFrameTaken[aCharacter] = false;
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
    for (uint32_t i = 0; i < g_wantsCleanFrame.size(); ++i)
    {
        ResetCleanFrame(i);
    }
    for (auto& ignored : g_ignored)
    {
        ignored = false;
    }
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
    if ((aAttribute == 58 || aAttribute == 78 || aAttribute == 57) && (npc < 0 || npc >= 3))
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
    case 57: SetPose(character, 1, value); break;
    default: break;
    }
}

inline bool IsLookAtAttribute(uint32_t aAttribute)
{
    return aAttribute == 15 || aAttribute == 74 || aAttribute == 58 || aAttribute == 78 || aAttribute == 68 ||
           aAttribute == 5 || aAttribute == 6 || aAttribute == 57;
}

// 0 nothing, 1 eyes, 2 head and eyes, 3 torso, head and eyes.
inline int32_t Reach(uint32_t aCharacter)
{
    if (aCharacter >= g_mode.size() || g_ignored[aCharacter])
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
    return aCharacter < g_mode.size() && !g_ignored[aCharacter] && g_mode[aCharacter] == 1;
}

inline bool IsCustom(uint32_t aCharacter)
{
    return aCharacter < g_mode.size() && !g_ignored[aCharacter] && g_mode[aCharacter] == 2;
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
    ResetCleanFrame(aCharacter);
    std::scoped_lock lock(g_cameraMutex);
    g_cameras[aCharacter].valid = false;
}
} // namespace APM::Pose::LookAtCamera
