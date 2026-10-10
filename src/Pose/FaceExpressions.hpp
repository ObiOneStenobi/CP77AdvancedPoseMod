#pragma once

#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Expression mixer for V's face rig. The game's facial expressions (Photo Mode attribute 28) are recorded once:
// while the player goes through them in Photo Mode, the face rig's pose of each one is averaged over a moment and
// saved (red4ext/plugins/AdvancedPoseMod/expressions.json). A region of the face (brows, eyes, mouth, jaw) can then
// take any recorded expression at a chosen strength, on top of the face the game shows: the change from the
// neutral expression is added, so the Photo Mode expression should be "Neutral" for an exact result.
namespace APM::Pose::FaceExpressions
{
const char* GetRegionName(size_t aRegion);

// Photo Mode's expression choices (labels and the values the game sends), from the menu.
void SetChoices(const std::vector<std::string>& aNames, const std::vector<int32_t>& aValues);
size_t GetChoiceCount();
std::string GetChoiceName(size_t aIndex);
int32_t GetChoiceValue(size_t aIndex);
std::string GetNameOf(int32_t aValue);

void OnPhotoModeAttribute(uint32_t aAttribute, float aValue);

bool IsRecorded(int32_t aValue);
size_t RecordedCount();
bool IsNeutralRecorded();
// Forgets every recording (they are made again while going through the expressions).
void ForgetRecordings();

// Face rig, every frame, before any edit: records the expression being shown when needed.
void Observe(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount);
// Face rig: applies the mix.
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const FaceMix& aMix);
} // namespace APM::Pose::FaceExpressions
