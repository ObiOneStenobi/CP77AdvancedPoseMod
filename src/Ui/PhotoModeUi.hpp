#pragma once

#include "Math/Vec3.hpp"

#include <cstdint>
#include <string>

// State behind the "Advanced pose" items added to the Pose page of the Photo Mode menu.
// The redscript side (scripts/AdvancedPoseMod/PhotoModeUi.reds) creates the menu items and forwards
// every value change here; when values must be redrawn (e.g. another body part was selected) it
// polls ConsumeRefresh() and reads them back with GetValue().
namespace APM::Ui::PhotoModeUi
{
// Attribute keys used by the menu items. The game uses keys < 100, PhotoModeEx uses 34xx.
namespace Attr
{
constexpr uint32_t First = 7700;
constexpr uint32_t Enabled = 7700;
constexpr uint32_t BodyPart = 7701;
constexpr uint32_t Bend1 = 7702;
constexpr uint32_t Bend2 = 7703;
constexpr uint32_t Twist = 7704;
constexpr uint32_t MoveX = 7705;
constexpr uint32_t MoveY = 7706;
constexpr uint32_t MoveZ = 7707;
constexpr uint32_t IkLimb = 7710;
constexpr uint32_t IkPin = 7711;
constexpr uint32_t IkX = 7712;
constexpr uint32_t IkY = 7713;
constexpr uint32_t IkZ = 7714;
constexpr uint32_t IkSwivel = 7715;
constexpr uint32_t Slot = 7720;
constexpr uint32_t FaceControl = 7740; // 7740..: face controls (value shown = the expression's + the offset)
// Action items: a selector with two options ("-" / "Apply"); choosing "Apply" runs the action.
constexpr uint32_t ResetPart = 7721;
constexpr uint32_t ResetAll = 7722;
constexpr uint32_t SavePose = 7723;
constexpr uint32_t LoadPose = 7724;
constexpr uint32_t Last = 7799;
} // namespace Attr

bool IsOwnAttribute(uint32_t aAttribute);
void SetValue(uint32_t aAttribute, float aValue);
float GetValue(uint32_t aAttribute);
bool IsAttributeEnabled(uint32_t aAttribute);
bool ConsumeRefresh();
void RequestRefresh();

// Option lists for selector items.
int32_t GetOptionCount(uint32_t aAttribute);
std::string GetOptionLabel(uint32_t aAttribute, int32_t aIndex);
// Group of an option (body parts: "Torso", "Left arm", ...). Empty when the attribute has no groups.
std::string GetOptionGroup(uint32_t aAttribute, int32_t aIndex);

// Gizmo: rotates a body part around a model-space axis (degrees), keeping the UI angles in sync.
bool RotatePart(int32_t aPart, const Math::Vec3& aModelAxis, float aDegrees);
// True while Photo Mode's "Look at camera" turns the part's bones (they cannot be edited then).
bool IsPartLookAtLocked(int32_t aPart);
// Moves a part that allows it (hips, held props) by a model-space vector (meters).
bool MovePart(int32_t aPart, const Math::Vec3& aModelDelta);
bool CanMovePart(int32_t aPart);

// Copies one side of the body onto the other, mirrored. aGroup: a tab such as "Left arm" (copied to "Right arm"),
// or empty for every left / right part. Returns the number of parts copied.
int32_t MirrorParts(bool aFromLeft, const std::string& aGroup);
// Turns a part (an eye) so that its forward direction points at a model-space point.
bool AimPart(int32_t aPart, const Math::Vec3& aModelTarget);

// Hand presets (0 open, 1 relaxed, 2 fist, 3 point, 4 grip): sets the fingers of one hand. Returns false if unknown.
bool ApplyHandPreset(bool aRightHand, int32_t aPreset);
// Mirror mode: edits of a left / right part are copied, mirrored, to the other side.
void SetMirror(bool aMirror);
bool IsMirror();

// Joint limits: rotations stay within natural ranges (on by default).
void SetLimits(bool aLimits);
bool AreLimitsOn();

// Small persistent UI preferences (panel side, opacity, gizmos...), stored in panel.ini next to the plugin.
float GetPreference(const std::string& aKey, float aDefault);
void SetPreference(const std::string& aKey, float aValue);

// Pose slots (0-based) used by the menu and the pose editor panel.
int32_t GetSlotCount();
bool IsSlotSaved(int32_t aSlot);
} // namespace APM::Ui::PhotoModeUi
