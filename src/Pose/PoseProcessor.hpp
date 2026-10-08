#pragma once

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
} // namespace APM::Pose::PoseProcessor
