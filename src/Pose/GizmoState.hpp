#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>

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

// V's entity (ent::Entity*), 0 if unknown.
uintptr_t GetEntity();

// Animated component of V's main skeleton (ent::AnimatedComponent*); bone model space is relative to it. 0 if unknown.
uintptr_t GetComponent();
} // namespace APM::Pose::GizmoState
