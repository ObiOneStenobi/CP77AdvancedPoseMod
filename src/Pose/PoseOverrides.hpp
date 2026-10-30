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

// Face (Pose/FacePoses): the value (0-1) of each face pose the user set, by pose index (FaceUnset: the pose keeps the
// Photo Mode expression's value). With the game's facial solver a set value replaces that pose's track; on the bone
// fallback it is added to the expression. Kept apart from the body: saved in their own library, untouched by body poses.
constexpr size_t MaxFacePoses = 128;
constexpr float FaceUnset = -1.f;
using FaceWeights = std::array<float, MaxFacePoses>;

inline FaceWeights UnsetFaceWeights()
{
    FaceWeights weights;
    weights.fill(FaceUnset);
    return weights;
}

inline bool AnyFaceWeight(const FaceWeights& aWeights)
{
    for (const float value : aWeights)
    {
        if (value >= 0.f)
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
    FaceWeights faceWeights = UnsetFaceWeights();

    bool IsEmpty() const
    {
        if (!bones.empty() || base || AnyFaceWeight(faceWeights))
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
// Master switch of the selected character. Editing a bone or loading a pose enables it; leaving Photo Mode
// disables it. Every function below works on the selected character (Pose/Characters.hpp).
void SetEnabled(bool aEnabled);
bool IsEnabled();

// The selected character's snapshot (see Pose/Characters.hpp). Never null.
std::shared_ptr<const OverrideSnapshot> GetSnapshot();
// Per character (0 = V, 1-3 = added characters), used by the animation hook. Never null.
std::shared_ptr<const OverrideSnapshot> GetSnapshot(uint32_t aCharacter);
bool IsEnabled(uint32_t aCharacter);
void SetEnabled(uint32_t aCharacter, bool aEnabled);
// Edits, a loaded pose or items in hand.
bool HasEdits(uint32_t aCharacter);
// Changes the selected character (between two edits, never during one).
void SelectCharacter(uint32_t aCharacter);
// Forgets one character's pose (another character took that Photo Mode slot).
void ClearCharacter(uint32_t aCharacter);

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
// Clears the pose edits, IK, base pose and items in hand, and the face weights unless aKeepFace (the Body reset).
void ResetAll(bool aKeepFace = false);
// Starts a new Photo Mode session: clears the pose, face, held items and undo history, and disables the custom pose.
void ClearSession();
size_t Count();

// Pose files live in red4ext/plugins/AdvancedPoseMod/poses/<name>.json
std::filesystem::path GetPoseDirectory();
// Saves the whole visible pose (see BasePose) when V's skeleton is available, else only the edits.
bool Save(const std::string& aName);
// Name of the full pose currently replacing the Photo Mode pose, empty if none.
std::string GetBasePoseName();
// Replaces the Photo Mode pose with a neutral one ("T" = reference pose, "A" = arms lowered), clearing the edits.
bool SetNeutralPose(bool aArmsDown);

// Face pose values (see FaceWeights); part of undo / redo. A negative value unsets the pose.
void SetFaceWeight(size_t aPose, float aWeight);
// FaceUnset when the pose keeps the Photo Mode value.
float GetFaceWeight(size_t aPose);
FaceWeights GetFaceWeights();
// Replaces every value (a loaded expression, a zone reset) and the name of the expression loaded ("" for none).
void SetFaceWeights(const FaceWeights& aWeights, const std::string& aName);
std::string GetFaceName();
// Values and loaded expression name cleared.
void ResetFace();
// The same for one character whose Photo Mode expression changed. The undo step gives back the whole face it showed
// (aShown: the previous expression's values, for the poses it had not set). False if it had no face edits.
bool ResetFaceOf(uint32_t aCharacter, const FaceWeights& aShown);

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
std::string SaveNew(const std::string& aFolder);
// Renames a saved pose. Returns the new name (made unique if another pose already has it), or "" on failure.
std::string Rename(const std::string& aOldName, const std::string& aNewName);
std::vector<std::string> ListSaved();
} // namespace PoseOverrides
} // namespace APM::Pose
