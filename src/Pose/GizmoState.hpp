#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Final pose of V's main skeleton, kept for the on-screen gizmos (joint markers, drag handles).
// Written from animation worker threads after the custom pose is applied, read from scripts on the game thread.
//
// Positions come from the rendered "deformations" skeleton when it has the bone, else from the main one.
namespace APM::Pose::GizmoState
{
// Called for every sampled skeleton; V's "root" and "deformations" skeletons (Photo Mode slot 0) are kept.
// aSource (graph output node) is currently unused.
void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, uintptr_t aSource);
void Clear();

// Model-space position / rotation of a bone of the main skeleton (relative to the animated component).
bool GetBoneModelPosition(uint64_t aBoneHash, Math::Vec3& aOut);
bool GetBoneModelRotation(uint64_t aBoneHash, Math::Quat& aOut);
// Model-space rotation of the bone's parent (identity for the root bone).
bool GetParentModelRotation(uint64_t aBoneHash, Math::Quat& aOut);
// Model-space point the bone points at (its first child, or a short extension for leaf bones).
bool GetBoneTipModelPosition(uint64_t aBoneHash, Math::Vec3& aOut);

// Model-space position of the closest ancestor of a bone that is one of aCandidates (bone name hashes).
bool GetAncestorModelPosition(uint64_t aBoneHash, const std::vector<uint64_t>& aCandidates, Math::Vec3& aOut);

// A bone of the main skeleton's current pose in local space (relative to its parent), for full pose files.
struct CapturedBone
{
    uint64_t hash = 0;
    std::string name;
    Math::Quat rotation{};
    Math::Vec3 translation{};
    bool inBase = false;          // body bone stored as part of the pose (not the root, not the face)
    bool keepTranslation = false; // the hips: their position is part of the pose
};
bool CaptureLocalPose(std::vector<CapturedBone>& aOut);

// Main skeleton's local pose before the edits (animation, or the loaded full pose), published by the pose processor.
void PublishAnimatedPose(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount);
// Local rotation / translation of a bone: as shown now (edits and IK included), and before the edits.
bool GetFinalLocal(uint64_t aBoneHash, Math::Quat& aRotation, Math::Vec3& aTranslation);
bool GetAnimatedLocal(uint64_t aBoneHash, Math::Quat& aRotation, Math::Vec3& aTranslation);

// V's entity (ent::Entity*), 0 if unknown.
uintptr_t GetEntity();

// Animated component of V's main skeleton (ent::AnimatedComponent*); bone model space is relative to it. 0 if unknown.
uintptr_t GetComponent();
} // namespace APM::Pose::GizmoState
