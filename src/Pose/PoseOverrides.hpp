#pragma once

#include "Math/Quat.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Per-bone overrides applied on top of the animated pose ("additive" mode):
//   final rotation    = animated rotation * override rotation   (bone local space)
//   final translation = animated translation + override offset  (parent space, meters)
//
// Written from the game thread (scripts / UI), read from animation worker threads through immutable snapshots.
namespace APM::Pose
{
struct BoneOverride
{
    uint64_t boneHash = 0;
    std::string boneName;

    bool hasRotation = false;
    std::array<float, 3> rotationEuler{}; // degrees, applied Z * Y * X around local axes
    Math::Quat rotation{};
    bool hasUiAngles = false;             // set when edited from the Photo Mode UI
    std::array<float, 3> uiAngles{};      // bend1, bend2, twist (degrees) as shown in the UI

    bool hasTranslation = false;
    std::array<float, 3> translation{}; // meters
};

// Two-bone IK chains of the main skeleton.
enum class IkChain : uint32_t
{
    LeftHand = 0,
    RightHand,
    LeftFoot,
    RightFoot,
    Count
};

constexpr size_t IkChainCount = static_cast<size_t>(IkChain::Count);

// When a chain is enabled its end bone (hand/foot) is pinned where the original animation put it,
// plus `offset` (model space, meters: X right, Y forward, Z up). Bones edited above the chain
// (hips, spine...) therefore no longer drag the hand/foot along.
// `swivel` rotates the elbow/knee around the shoulder->hand (hip->foot) axis, in degrees.
struct IkChainSettings
{
    bool enabled = false;
    std::array<float, 3> offset{};
    float swivel = 0.f;
};

struct OverrideSnapshot
{
    std::vector<BoneOverride> bones;
    std::array<IkChainSettings, IkChainCount> ik{};

    bool IsEmpty() const
    {
        if (!bones.empty())
        {
            return false;
        }
        for (const auto& chain : ik)
        {
            if (chain.enabled)
            {
                return false;
            }
        }
        return true;
    }
};

// "LeftHand", "RightHand", "LeftFoot", "RightFoot" -> chain. Returns false for unknown names.
bool ParseIkChain(const std::string& aName, IkChain& aOut);
const char* GetIkChainName(IkChain aChain);

namespace PoseOverrides
{
// Master switch. Editing a bone or loading a pose enables it; leaving Photo Mode disables it
// (the edited pose stays in memory so the player can turn it back on).
void SetEnabled(bool aEnabled);
bool IsEnabled();

// Snapshot used by the animation hook. Never null.
std::shared_ptr<const OverrideSnapshot> GetSnapshot();

void SetRotation(const std::string& aBone, float aX, float aY, float aZ);
void SetTranslation(const std::string& aBone, float aX, float aY, float aZ);
// Rotation given directly as a quaternion, remembering the UI angles that produced it.
void SetRotationQuat(const std::string& aBone, const Math::Quat& aRotation, const std::array<float, 3>& aUiAngles);
bool GetUiAngles(const std::string& aBone, std::array<float, 3>& aOut);
bool GetRotation(const std::string& aBone, std::array<float, 3>& aOut);
bool GetTranslation(const std::string& aBone, std::array<float, 3>& aOut);
void SetIkChain(IkChain aChain, const IkChainSettings& aSettings);
IkChainSettings GetIkChain(IkChain aChain);
void ResetBone(const std::string& aBone);
void ResetAll();
size_t Count();

// Pose files live in red4ext/plugins/AdvancedPoseMod/poses/<name>.json
std::filesystem::path GetPoseDirectory();
bool Save(const std::string& aName);
bool Load(const std::string& aName);
std::vector<std::string> ListSaved();
} // namespace PoseOverrides
} // namespace APM::Pose
