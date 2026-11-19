#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace APM::Pose
{
struct Target;
}

// Research probe for hair and clothing physics ([Probe] Physics=1, see docs/RESEARCH.md "Hair and clothing physics").
// Logs, with the prefix "PhysicsProbe:":
// - every component of each Photo Mode character (class, name, anim graph of the animated ones) and the chains of its
//   secondary skeletons (hair, breasts, clothing dangles): where each chain hangs from the main skeleton, its length;
// - the animation node classes that look like physics, hooked: in which skeleton's graph they run and whether that
//   happens inside the graph output the mod edits (then the mod sees, and can override, the physics result);
// - how much the bones of those skeletons turn every 0.5 s during the first 30 s of a session;
// - with PhysicsTestBone / PhysicsTestAngle: that bone turned every frame, and whether the next frame starts from the
//   turned bone (the physics continues from the mod's pose) or from the physics' own result.
namespace APM::PhysicsProbe
{
// At game start, after the animation output hook (Physics=1 is read once, at game start, for the hooks).
void Install();
void Uninstall();
bool Enabled();

// Photo Mode opened (config reloaded).
void OnPhotoModeActivated();

// A character's targets were built: aTargets[aFirst..] belong to aPuppet. Logged once per puppet and session.
void LogPuppet(uint64_t aPuppet, uint32_t aCharacter, const std::vector<Pose::Target>& aTargets, size_t aFirst);

// Every sample of a skeleton with the rig's bone count, before the mod's edits (main skeleton skipped inside).
void OnSkeletonSampled(const Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount);
} // namespace APM::PhysicsProbe
