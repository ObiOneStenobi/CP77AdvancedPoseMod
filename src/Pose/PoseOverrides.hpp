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
// With `followBody` the pin moves with the bone the limb hangs from (shoulder / hips) instead: the hand/foot keeps
// its place relative to the body, and `offset` is expressed in that bone's current orientation.
struct IkChainSettings
{
    bool enabled = false;
    bool followBody = false;
    std::array<float, 3> offset{};
    float swivel = 0.f;
};

// A whole saved pose ("full" pose files): local rotations of V's body bones (plus the hips' position) that replace
// the animated ones before the edits are applied, so the pose no longer depends on the Photo Mode pose underneath.
struct BasePoseBone
{
    uint64_t boneHash = 0;
    Math::Quat rotation{};
    bool hasTranslation = false;
    std::array<float, 3> translation{};
};

struct BasePose
{
    std::string name;
    std::vector<BasePoseBone> bones;
};

// Expression mixer (Pose/FaceExpressions): per face region, a recorded Photo Mode expression (the value the game
// uses for it, -1 = none) and its strength (0..1).
constexpr size_t FaceRegionCount = 4; // brows, eyes, mouth, jaw
struct FaceRegionMix
{
    int32_t expression = -1;
    float weight = 0.f;

    bool IsActive() const { return expression >= 0 && weight > 0.f; }
    bool operator==(const FaceRegionMix&) const = default;
};
using FaceMix = std::array<FaceRegionMix, FaceRegionCount>;

inline bool AnyFaceMix(const FaceMix& aMix)
{
    for (const auto& region : aMix)
    {
        if (region.IsActive())
        {
            return true;
        }
    }
    return false;
}

struct OverrideSnapshot
{
    std::vector<BoneOverride> bones;
    std::array<IkChainSettings, IkChainCount> ik{};
    std::shared_ptr<const BasePose> base;
    FaceMix face{};

    bool IsEmpty() const
    {
        if (!bones.empty() || base || AnyFaceMix(face))
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
// Saves the whole visible pose (see BasePose) when V's skeleton is available, else only the edits.
bool Save(const std::string& aName);
// Name of the full pose currently replacing the Photo Mode pose, empty if none.
std::string GetBasePoseName();
// Replaces the Photo Mode pose with a neutral one ("T" = reference pose, "A" = arms lowered), clearing the edits.
bool SetNeutralPose(bool aArmsDown);

// Expression mixer of a face region (see FaceMix); saved with poses, part of undo / redo.
void SetFaceRegion(size_t aRegion, const FaceRegionMix& aMix);
FaceRegionMix GetFaceRegion(size_t aRegion);

// Items held in the hands (TweakDB record ids as numbers, 0 = none); saved with poses.
void SetHandProp(bool aRightHand, uint64_t aRecord);
uint64_t GetHandProp(bool aRightHand);

// Undo / redo. Checkpoint() records the state before a change (called when an edit starts).
void Checkpoint();
bool Undo();
bool Redo();
bool CanUndo();
bool CanRedo();
bool Load(const std::string& aName);
bool Delete(const std::string& aName);
// Saves under the first free name "Pose 001", "Pose 002", ... and returns it (empty on failure).
std::string SaveNew();
// Renames a saved pose. Returns the new name (made unique if another pose already has it), or "" on failure.
std::string Rename(const std::string& aOldName, const std::string& aNewName);
std::vector<std::string> ListSaved();
} // namespace PoseOverrides
} // namespace APM::Pose
