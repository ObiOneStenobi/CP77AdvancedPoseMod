#pragma once

#include "Math/Quat.hpp"

#include <cstdint>
#include <string_view>

// Natural joint ranges for V's main skeleton. Ranges are measured from the reference (T) pose on the bone's own axes
// (see TargetRegistry::GetBoneAxes): forward = bend forward / back (fingers: curl), side = sideways or raise / lower,
// twist = along the bone. They grow to include where the animation already has the joint, so editing never makes
// it jump.
namespace APM::Pose::JointLimits
{
void SetEnabled(bool aEnabled);
bool IsEnabled();

// True when the bone has a range (and limits are on).
bool HasRange(std::string_view aBone);

// A final local rotation kept within the bone's range. aAnimatedLocal is where the animation has the joint.
// aSwapBends: the bone's bends are swapped (finger axes, as the panel uses them).
Math::Quat LimitLocal(std::string_view aBone, const Math::Quat& aLocal, const Math::Quat& aAnimatedLocal, bool aSwapBends);
} // namespace APM::Pose::JointLimits
