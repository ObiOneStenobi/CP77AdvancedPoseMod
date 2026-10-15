#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

// Applies the player's custom pose to one sampled skeleton of a target.
//
// Main skeleton ("root"):  original pose -> FK overrides -> IK chains (pinned to the original pose + offset).
// Other skeletons (shadow, face, hair, ...) inherit the shared bones from the edited main pose, so only bones
//   linked to a main bone (e.g. the face rig's eyeballs) are edited there.
namespace APM::Pose::PoseProcessor
{
void Process(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aBoneCount, const OverrideSnapshot& aPose);

// Latest rotation of the bone an IK chain hangs from (shoulder / hips) relative to the original animation, in model
// space. Chains pinned to the body express their offset in this frame.
Math::Quat GetIkBodyRotation(size_t aChain);

// Where an IK chain was last solved, to convert its settings when the pin switches between world and body.
struct IkFrame
{
    Math::Vec3 originalEnd;      // hand / foot in the original animation (model space)
    Math::Quat originalEndRotation;
    Math::Vec3 originalParent;   // shoulder / hips in the original animation
    Math::Vec3 currentParent;    // shoulder / hips after the edits
    Math::Quat body;             // see GetIkBodyRotation
};
bool GetIkFrame(size_t aChain, IkFrame& aOut);

// Which way the face looks in the reference pose of a main skeleton (model space, level): from the head towards the
// eyes. Rigs differ in how their bones are oriented, so this is taken from the geometry, not from a fixed axis.
Math::Vec3 ReferenceFaceForward(const Target& aTarget);

// A new character took that slot (Pose/Characters.hpp): its cached IK frames and look-at data are dropped.
void ResetCharacter(uint32_t aCharacter);
} // namespace APM::Pose::PoseProcessor
