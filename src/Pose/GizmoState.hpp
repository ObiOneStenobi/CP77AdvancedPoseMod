#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>

// Final pose of V's main skeleton, kept for the on-screen gizmos (joint markers, drag handles).
// Written from animation worker threads after the custom pose is applied, read from scripts on the game thread.
namespace APM::Pose::GizmoState
{
// Called for every sampled skeleton; only V's main skeleton (Photo Mode slot 0, "root") is kept.
void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount);
void Clear();

// World transform of V's entity (position in meters, orientation). False if unknown.
bool GetEntityTransform(Math::Vec3& aPosition, Math::Quat& aOrientation);

// World position of a bone of the main skeleton. False if the pose or the bone is unknown.
bool GetBoneWorldPosition(uint64_t aBoneHash, Math::Vec3& aOut);

// Model-space position of a bone of the main skeleton (relative to V's entity). False if unknown.
bool GetBoneModelPosition(uint64_t aBoneHash, Math::Vec3& aOut);

// V's entity (ent::Entity*), 0 if unknown.
uintptr_t GetEntity();

// Animated component of V's main skeleton (ent::AnimatedComponent*); bone model space is relative to it. 0 if unknown.
uintptr_t GetComponent();

// World-space vector -> V's model space (X right, Y forward, Z up). Returns the input if V is unknown.
Math::Vec3 WorldToModelDirection(const Math::Vec3& aWorld);
} // namespace APM::Pose::GizmoState
