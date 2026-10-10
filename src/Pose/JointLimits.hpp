#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"

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
// Reference (T pose) local rotation of a bone and the sign that makes its bend1 curl towards the palm / go forward.
bool GetReferenceFrame(std::string_view aBone, bool aSwapBends, Math::Quat& aReferenceLocal, float& aForwardSign);

// Thumb bones (thumb base and segments): reference local rotation and the local axis that closes the thumb across the
// palm, towards the little finger (the thumb sits below the palm in the reference pose, so the finger curl axis would
// push it further away).
bool GetThumbCurl(std::string_view aBone, Math::Quat& aReferenceLocal, Math::Vec3& aLocalAxis);

Math::Quat LimitLocal(std::string_view aBone, const Math::Quat& aLocal, const Math::Quat& aAnimatedLocal, bool aSwapBends);
} // namespace APM::Pose::JointLimits
