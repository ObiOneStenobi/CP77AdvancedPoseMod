#pragma once

#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// V's face (the "face_rig" skeleton). The Photo Mode expression is the starting point, like the Photo Mode pose for
// the body: face controls (jaw, mouth, brows, eyelids, cheeks) move it from there. Each control reads where the
// expression has its feature (e.g. how far a mouth corner is raised from the rig's rest face), so its value starts
// there; the player's change is kept as an offset on top, and stays when the expression changes.
// "Move" controls drag a point of the face with its neighbours (weights fall off with the distance); "turn" controls
// rotate a bone (jaw, eyelids) with what hangs from it.
// Saved expressions hold the whole face and replace the Photo Mode expression while loaded.
namespace APM::Pose::FaceRig
{
size_t ControlCount();
const char* ControlLabel(size_t aControl);
const char* ControlZone(size_t aControl); // "Jaw", "Mouth", "Brows", "Eyelids", "Cheeks"
bool ControlIsAngle(size_t aControl);     // degrees (else centimeters)
int32_t ControlTwin(size_t aControl);     // the same control on the other side, or -1

// Where the face shown (before the player's changes) has the control's feature.
float GetMeasure(size_t aControl);

// Face rig, every frame, before any edit (reads the expression).
void Observe(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose);
// Face rig: the loaded expression and the controls.
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose);

// Saved expressions: red4ext/plugins/AdvancedPoseMod/expressions/<name>.json
std::filesystem::path GetDirectory();
std::vector<std::string> ListSaved();
bool Save(const std::string& aName);
std::string SaveNew();
bool Load(const std::string& aName);
bool Delete(const std::string& aName);
} // namespace APM::Pose::FaceRig
