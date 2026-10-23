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
// Reference (T pose) local rotation of a bone and the sign that makes its bend1 curl towards the palm / go forward.
// Without aCharacter: the selected character (0 = V, 1-3 = added characters).
bool GetReferenceFrame(std::string_view aBone, bool aSwapBends, Math::Quat& aReferenceLocal, float& aForwardSign);
bool GetReferenceFrame(uint32_t aCharacter, std::string_view aBone, bool aSwapBends, Math::Quat& aReferenceLocal,
                       float& aForwardSign);

Math::Quat LimitLocal(std::string_view aBone, const Math::Quat& aLocal, const Math::Quat& aAnimatedLocal, bool aSwapBends);
Math::Quat LimitLocal(uint32_t aCharacter, std::string_view aBone, const Math::Quat& aLocal,
                      const Math::Quat& aAnimatedLocal, bool aSwapBends);

// Drops the joint frames worked out so far (Photo Mode opened / closed).
void ResetCache();
} // namespace APM::Pose::JointLimits
