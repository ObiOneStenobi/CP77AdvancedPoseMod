#pragma once

#include "Math/Quat.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <array>
#include <cstdint>

// The game moves the eyelids with the eyes (looking down lowers the upper lid), by how much depending on the facial
// expression. Our eye edits come after the game's animation, so the eyelids stayed where the game put them for its
// own gaze. This adds to the face rig's eyelid bones the difference between the eyelids the game would show for our
// gaze and for its own, with the share of the current expression (measured in game, see EyelidFollowData.inc).
namespace APM::Pose::EyelidFollow
{
// Photo Mode facial expression (attribute 28); expressions added by other mods follow like the neutral one.
void SetExpression(int32_t aExpression);
void ResetSession();

// Eye bones of the face rig as the game animated them (before our edits): [left, right]. False if the rig has none.
bool ReadEyes(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount,
              std::array<Math::Quat, 2>& aOut);

// After our edits of the face rig: moves the eyelids for the eyes' change since ReadEyes.
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
           const std::array<Math::Quat, 2>& aGameEyes);
} // namespace APM::Pose::EyelidFollow
