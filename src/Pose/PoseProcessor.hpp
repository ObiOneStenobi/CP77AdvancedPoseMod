#pragma once

#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

// Applies the player's custom pose to one sampled skeleton of a target.
//
// Main skeleton ("root"):  original pose -> FK overrides -> IK chains (pinned to the original pose + offset).
//   The IK result is published as per-bone local deltas for the same character.
// Other skeletons (deformations, face, ...): FK overrides -> IK deltas of same-named bones,
//   so helper/twist/face bones stay consistent with the main skeleton.
namespace APM::Pose::PoseProcessor
{
void Process(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aBoneCount, const OverrideSnapshot& aPose);
} // namespace APM::Pose::PoseProcessor
