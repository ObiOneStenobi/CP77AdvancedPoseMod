#pragma once

// Hooks the final output node of animation graphs (animAnimNode_Output::Sample).
// After the original graph has produced the local-space pose, the pose override layer
// gets a chance to modify the bones of registered targets.
namespace APM::AnimGraphHook
{
bool Install();
void Uninstall();

// Called on the game thread when Photo Mode opens (after the config has been reloaded).
void OnPhotoModeActivated();
} // namespace APM::AnimGraphHook
