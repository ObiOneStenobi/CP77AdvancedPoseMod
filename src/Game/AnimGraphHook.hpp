#pragma once

#include <cstdint>

// Hooks the final output node of animation graphs (animAnimNode_Output::Sample).
// After the original graph has produced the local-space pose, the pose override layer
// gets a chance to modify the bones of registered targets.
namespace APM::AnimGraphHook
{
bool Install();
void Uninstall();

// Called on the game thread when Photo Mode opens (after the config has been reloaded).
void OnPhotoModeActivated();

// Diagnostics ([Debug] Log=1): what every skeleton of a character (0 = V, 1-3 = added characters) received from the
// animation since Photo Mode opened (samples, bone counts, whether they could be posed).
void LogCharacterSkeletons(uint32_t aCharacter);

// For the research probes (PhysicsProbe): vtable of an animation node class (0 if unknown), the address of a node
// class's Sample, how many graph outputs this thread is inside right now, and the animated component an animation
// context belongs to (0 if unknown).
uintptr_t NodeVtable(const char* aClass);
uintptr_t NodeSample(uintptr_t aVtable);
int OutputDepth();
uintptr_t ComponentFromContext(uintptr_t aContext);
} // namespace APM::AnimGraphHook
