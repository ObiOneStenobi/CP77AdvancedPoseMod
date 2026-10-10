#include "Ui/PhotoModeUi.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Math/Vec3.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/JointLimits.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/PoseProcessor.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/BoneCatalog.hpp"

#include <RED4ext/CName.hpp>
#include <string_view>

#include <Windows.h>

#include <algorithm>
#include <string_view>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using namespace APM;
using namespace APM::Ui::PhotoModeUi;

constexpr int32_t SlotCount = 10;
constexpr const char* IkLimbLabels[] = {"Left hand", "Right hand", "Left foot", "Right foot"};
constexpr const char* IkPinLabels[] = {"Off", "World", "Body"};

// Mirror mode: every edit of a left / right part is copied, mirrored, to the same part on the other side.
std::atomic<bool> g_mirror{false};
// Last focus distance set for the eyes (cm), used when their lines of sight do not cross.
float g_eyeFocus = 150.f;
void MirrorCurrentPart();
void MirrorCurrentChain();
constexpr const char* ActionLabels[] = {"-", "Apply"};

// Distances are shown in centimeters: the game's sliders only update live with whole-number steps.
constexpr float CentimetersPerMeter = 100.f;

// Main-thread state (natives are called from redscript on the game thread).
int32_t g_bodyPart = 0;
int32_t g_ikLimb = 0;
int32_t g_slot = 0; // 0-based slot index
std::atomic<bool> g_refresh{true};

const Ui::CatalogEntry& CurrentEntry()
{
    const auto& catalog = Ui::GetBoneCatalog();
    return catalog[std::clamp<int32_t>(g_bodyPart, 0, static_cast<int32_t>(catalog.size()) - 1)];
}

Pose::IkChain CurrentChain()
{
    return static_cast<Pose::IkChain>(std::clamp<int32_t>(g_ikLimb, 0, Pose::IkChainCount - 1));
}

std::string SlotName(int32_t aSlot)
{
    char name[16]{};
    std::snprintf(name, sizeof(name), "slot_%02d", aSlot);
    return name;
}

bool SlotExists(int32_t aSlot)
{
    std::error_code ec;
    return std::filesystem::exists(Pose::PoseOverrides::GetPoseDirectory() / (SlotName(aSlot) + ".json"), ec);
}

bool Same(float a, float b)
{
    return std::abs(a - b) < 1e-4f;
}

std::array<float, 3> CurrentAngles()
{
    std::array<float, 3> angles{};
    Pose::PoseOverrides::GetUiAngles(CurrentEntry().bones.front(), angles);
    return angles;
}

std::array<float, 3> CurrentTranslation()
{
    std::array<float, 3> translation{};
    Pose::PoseOverrides::GetTranslation(CurrentEntry().bones.front(), translation);
    return translation;
}

// aSwapBends: fingers point sideways like the arms, but for them "bend forward / back" must be the curl
// (rotation around the forward axis) and "bend sideways" the spread (rotation around the vertical axis).
Math::Quat AnglesToRotation(const char* aBone, const std::array<float, 3>& aAngles, bool aSwapBends)
{
    Math::Vec3 bend1;
    Math::Vec3 bend2;
    Math::Vec3 twist;
    if (!Pose::TargetRegistry::GetBoneAxes(RED4ext::CName(aBone).hash, bend1, bend2, twist))
    {
        // Main skeleton not available yet: fall back to the bone's raw local axes.
        return Math::FromEulerXYZDegrees(aAngles[0], aAngles[1], aAngles[2]);
    }
    if (aSwapBends)
    {
        std::swap(bend1, bend2);
    }
    const auto q1 = Math::FromAxisAngle(bend1, Math::DegToRad(aAngles[0]));
    const auto q2 = Math::FromAxisAngle(bend2, Math::DegToRad(aAngles[1]));
    const auto q3 = Math::FromAxisAngle(twist, Math::DegToRad(aAngles[2]));
    return Math::Normalize(Math::Mul(Math::Mul(q1, q2), q3));
}

bool RotationToAngles(const char* aBone, const Math::Quat& q, bool aSwapBends, std::array<float, 3>& aOut);

constexpr const char* IkChainEnds[] = {"LeftHand", "RightHand", "LeftFoot", "RightFoot"};

bool IsPinnedChainEnd(std::string_view aBone)
{
    for (size_t c = 0; c < 4; ++c)
    {
        if (aBone == IkChainEnds[c] && Pose::PoseOverrides::GetIkChain(static_cast<Pose::IkChain>(c)).enabled)
        {
            return true;
        }
    }
    return false;
}

// The edit (anim * edit = final local rotation) kept within the bone's natural range (Pose/JointLimits).
bool g_noLimits = false; // hand presets: their rotations are natural already, store them as they are

Math::Quat LimitEdit(const char* aBone, const Math::Quat& aEdit, bool aSwapBends)
{
    if (g_noLimits || !Pose::JointLimits::HasRange(aBone) || IsPinnedChainEnd(aBone))
    {
        return aEdit;
    }
    Math::Quat animated{};
    Math::Vec3 unused{};
    if (!Pose::GizmoState::GetAnimatedLocal(RED4ext::CName(aBone).hash, animated, unused))
    {
        return aEdit;
    }
    const Math::Quat wanted = Math::Mul(animated, aEdit);
    const Math::Quat limited = Pose::JointLimits::LimitLocal(aBone, wanted, animated, aSwapBends);
    const float dot = limited.i * wanted.i + limited.j * wanted.j + limited.k * wanted.k + limited.r * wanted.r;
    if (std::abs(dot) >= 0.999999f)
    {
        return aEdit;
    }
    return Math::Normalize(Math::Mul(Math::Conjugate(animated), limited));
}

// Stores an edit given as UI angles, within the joint limits (the stored angles follow the limited rotation).
void StoreEdit(const char* aBone, std::array<float, 3> aAngles, bool aSwapBends)
{
    const Math::Quat wanted = AnglesToRotation(aBone, aAngles, aSwapBends);
    const Math::Quat limited = LimitEdit(aBone, wanted, aSwapBends);
    const float dot = limited.i * wanted.i + limited.j * wanted.j + limited.k * wanted.k + limited.r * wanted.r;
    if (std::abs(dot) < 0.999999f)
    {
        std::array<float, 3> angles{};
        if (RotationToAngles(aBone, limited, aSwapBends, angles))
        {
            for (auto& angle : angles)
            {
                angle = std::clamp(angle, -180.f, 180.f);
            }
            aAngles = angles;
        }
    }
    Pose::PoseOverrides::SetRotationQuat(aBone, limited, aAngles);
}


// The pin of the current chain switches between world and body: convert its offset and the hand / foot rotation so
// that nothing moves on screen.
void KeepPinnedEndInPlace(Pose::IkChainSettings& aSettings)
{
    const auto chain = CurrentChain();
    Pose::PoseProcessor::IkFrame frame;
    if (!Pose::PoseProcessor::GetIkFrame(static_cast<size_t>(chain), frame))
    {
        return;
    }
    const Math::Vec3 offset{aSettings.offset[0], aSettings.offset[1], aSettings.offset[2]};
    Math::Vec3 converted;
    Math::Quat worldChange; // applied to the hand / foot's world rotation by the switch, to undo
    if (aSettings.followBody)
    {
        // World -> body: same target, expressed relative to the shoulder / hips.
        const Math::Vec3 target = frame.originalEnd + offset;
        converted = Math::Rotate(Math::Conjugate(frame.body), target - frame.currentParent) - frame.originalEnd +
                    frame.originalParent;
        worldChange = Math::Conjugate(frame.body);
    }
    else
    {
        const Math::Vec3 target =
            frame.currentParent + Math::Rotate(frame.body, frame.originalEnd + offset - frame.originalParent);
        converted = target - frame.originalEnd;
        worldChange = frame.body;
    }
    aSettings.offset = {converted.x, converted.y, converted.z};

    // Hand / foot edit O: world rotation = (body) * original * O. Keep it: O' = original^-1 * change * original * O.
    const char* bone = IkChainEnds[static_cast<size_t>(chain)];
    std::array<float, 3> angles{};
    Pose::PoseOverrides::GetUiAngles(bone, angles);
    const Math::Quat edit = AnglesToRotation(bone, angles, false);
    const Math::Quat updated = Math::Normalize(Math::Mul(
        Math::Mul(Math::Mul(Math::Conjugate(frame.originalEndRotation), worldChange), frame.originalEndRotation), edit));
    std::array<float, 3> updatedAngles{};
    if (RotationToAngles(bone, updated, false, updatedAngles))
    {
        for (auto& angle : updatedAngles)
        {
            angle = std::clamp(angle, -180.f, 180.f);
        }
        Pose::PoseOverrides::SetRotationQuat(bone, AnglesToRotation(bone, updatedAngles, false), updatedAngles);
    }
}

constexpr const char* IkChainParents[] = {"LeftShoulder", "RightShoulder", "Hips", "Hips"};

// The pin of the current chain is turned on: pin the hand / foot where it is shown now (after the edits above it),
// instead of where the animation had it.
void PinAtCurrentPlace(Pose::IkChainSettings& aSettings)
{
    const auto chain = static_cast<size_t>(CurrentChain());
    const char* end = IkChainEnds[chain];
    const uint64_t endHash = RED4ext::CName(end).hash;
    const uint64_t parentHash = RED4ext::CName(IkChainParents[chain]).hash;
    Math::Vec3 currentEnd{};
    Math::Quat currentEndRotation{};
    Math::Vec3 originalEnd{};
    Math::Quat originalEndRotation{};
    Math::Vec3 currentParent{};
    Math::Quat currentParentRotation{};
    Math::Vec3 originalParent{};
    Math::Quat originalParentRotation{};
    if (!Pose::GizmoState::GetBoneModelPosition(endHash, currentEnd) ||
        !Pose::GizmoState::GetBoneModelRotation(endHash, currentEndRotation) ||
        !Pose::GizmoState::GetAnimatedModel(endHash, originalEnd, originalEndRotation) ||
        !Pose::GizmoState::GetBoneModelPosition(parentHash, currentParent) ||
        !Pose::GizmoState::GetBoneModelRotation(parentHash, currentParentRotation) ||
        !Pose::GizmoState::GetAnimatedModel(parentHash, originalParent, originalParentRotation))
    {
        return;
    }
    // Same frame the solver uses (see PoseProcessor: target and hand rotation, world or body).
    const Math::Quat body = Math::Normalize(Math::Mul(currentParentRotation, Math::Conjugate(originalParentRotation)));
    Math::Vec3 offset;
    Math::Quat handWorld = currentEndRotation;
    if (aSettings.followBody)
    {
        offset = Math::Rotate(Math::Conjugate(body), currentEnd - currentParent) - originalEnd + originalParent;
        handWorld = Math::Mul(Math::Conjugate(body), currentEndRotation);
    }
    else
    {
        offset = currentEnd - originalEnd;
    }
    aSettings.offset = {offset.x, offset.y, offset.z};
    aSettings.swivel = 0.f; // the solver keeps the elbow / knee where the pose has it

    // Hand / foot edit O: pinned world rotation = (body) * original * O.
    const Math::Quat edit = Math::Normalize(Math::Mul(Math::Conjugate(originalEndRotation), handWorld));
    std::array<float, 3> angles{};
    if (RotationToAngles(end, edit, false, angles))
    {
        for (auto& angle : angles)
        {
            angle = std::clamp(angle, -180.f, 180.f);
        }
        Pose::PoseOverrides::SetRotationQuat(end, AnglesToRotation(end, angles, false), angles);
    }
}

// The pin of the current chain is turned off: the arm / leg keeps the pose the IK gave it, as plain rotations
// (Reset part puts it back).
void BakeChainPose()
{
    constexpr const char* chainBones[4][3] = {{"LeftArm", "LeftForeArm", "LeftHand"},
                                              {"RightArm", "RightForeArm", "RightHand"},
                                              {"LeftUpLeg", "LeftLeg", "LeftFoot"},
                                              {"RightUpLeg", "RightLeg", "RightFoot"}};
    for (const char* bone : chainBones[static_cast<size_t>(CurrentChain())])
    {
        const uint64_t hash = RED4ext::CName(bone).hash;
        Math::Quat finalLocal{};
        Math::Quat animated{};
        Math::Vec3 unused{};
        if (!Pose::GizmoState::GetFinalLocal(hash, finalLocal, unused) ||
            !Pose::GizmoState::GetAnimatedLocal(hash, animated, unused))
        {
            continue;
        }
        const Math::Quat edit = Math::Normalize(Math::Mul(Math::Conjugate(animated), finalLocal));
        std::array<float, 3> angles{};
        if (RotationToAngles(bone, edit, false, angles))
        {
            Pose::PoseOverrides::SetRotationQuat(bone, edit, angles);
        }
    }
}

bool UsesFingerAxes(const Ui::CatalogEntry& aEntry)
{
    return aEntry.mode == Ui::EntryMode::Curl || aEntry.mode == Ui::EntryMode::FingerSegment;
}

void ApplyAngles(const std::array<float, 3>& aAngles)
{
    const auto& entry = CurrentEntry();
    const float curlChange = aAngles[0] - CurrentAngles()[0];
    for (size_t i = 0; i < entry.bones.size(); ++i)
    {
        std::array<float, 3> angles = aAngles;
        if (entry.mode == Ui::EntryMode::Curl && i > 0)
        {
            // Only the curl reaches the outer finger segments, as a change: their own edits (close-up gizmos) stay.
            std::array<float, 3> segment{};
            Pose::PoseOverrides::GetUiAngles(entry.bones[i], segment);
            angles = segment;
            angles[0] = std::clamp(segment[0] + curlChange, -180.f, 180.f);
        }
        const bool isFinger = UsesFingerAxes(entry);
        StoreEdit(entry.bones[i], angles, isFinger);
    }
}

// Euler decomposition q = Rx(a) * Ry(b) * Rz(c) for a right-handed orthonormal basis (x, y, z); degrees.
std::array<float, 3> DecomposeXYZ(const Math::Quat& q, const Math::Vec3& x, const Math::Vec3& y, const Math::Vec3& z)
{
    const Math::Vec3 rx = Math::Rotate(q, x);
    const Math::Vec3 ry = Math::Rotate(q, y);
    const Math::Vec3 rz = Math::Rotate(q, z);
    // m[i][j] = axis_i . (q * axis_j)
    const float m00 = Math::Dot(x, rx);
    const float m01 = Math::Dot(x, ry);
    const float m02 = Math::Dot(x, rz);
    const float m12 = Math::Dot(y, rz);
    const float m22 = Math::Dot(z, rz);
    constexpr float RadToDeg = 57.29577951308232f;
    const float b = std::asin(std::clamp(m02, -1.f, 1.f));
    const float a = std::atan2(-m12, m22);
    const float c = std::atan2(-m01, m00);
    return {a * RadToDeg, b * RadToDeg, c * RadToDeg};
}

// UI angles (bend1, bend2, twist) that produce the local rotation q, inverse of AnglesToRotation.
bool RotationToAngles(const char* aBone, const Math::Quat& q, bool aSwapBends, std::array<float, 3>& aOut)
{
    Math::Vec3 bend1;
    Math::Vec3 bend2;
    Math::Vec3 twist;
    if (!Pose::TargetRegistry::GetBoneAxes(RED4ext::CName(aBone).hash, bend1, bend2, twist))
    {
        return false;
    }
    if (aSwapBends)
    {
        std::swap(bend1, bend2);
    }
    // The decomposition needs a right-handed basis: when (bend1, bend2, twist) is left-handed (fingers, left limbs)
    // use the negated twist axis and negate the twist angle back.
    if (Math::Dot(Math::Cross(bend1, bend2), twist) >= 0.f)
    {
        aOut = DecomposeXYZ(q, bend1, bend2, twist);
        return true;
    }
    const auto angles = DecomposeXYZ(q, bend1, bend2, twist * -1.f);
    aOut = {angles[0], angles[1], -angles[2]};
    return true;
}

// Eyes: no twist, and they turn at most this far from where the animation points them (like the game's own look-at).
constexpr float EyeMaxDegrees = 30.f;

bool IsEyeEntry(const APM::Ui::CatalogEntry& aEntry)
{
    const std::string label = aEntry.label;
    return label == "Eyes (both)" || label == "Left eye" || label == "Right eye";
}

// "Eyes (both)": the third row is the focus distance (how far the eyes look; nearer = more converged).
bool IsBothEyes()
{
    return std::string(CurrentEntry().label) == "Eyes (both)";
}

constexpr float EyeFocusMin = 10.f;   // cm
constexpr float EyeFocusMax = 1000.f; // cm

// Point between the eyes, the direction they look and how far their lines of sight meet (cm).
bool GetEyeFocus(Math::Vec3& aMiddle, Math::Vec3& aDirection, float& aDistance)
{
    const uint64_t leftHash = RED4ext::CName("LeftEye").hash;
    const uint64_t rightHash = RED4ext::CName("RightEye").hash;
    Math::Vec3 left{};
    Math::Vec3 right{};
    Math::Quat leftRotation{};
    Math::Quat rightRotation{};
    Math::Vec3 leftForward{};
    Math::Vec3 rightForward{};
    if (!Pose::GizmoState::GetBoneModelPosition(leftHash, left) || !Pose::GizmoState::GetBoneModelPosition(rightHash, right) ||
        !Pose::GizmoState::GetBoneModelRotation(leftHash, leftRotation) ||
        !Pose::GizmoState::GetBoneModelRotation(rightHash, rightRotation) ||
        !Pose::TargetRegistry::GetBoneReferenceForward(leftHash, leftForward) ||
        !Pose::TargetRegistry::GetBoneReferenceForward(rightHash, rightForward))
    {
        return false;
    }
    const Math::Vec3 gl = Math::Normalize(Math::Rotate(leftRotation, leftForward));
    const Math::Vec3 gr = Math::Normalize(Math::Rotate(rightRotation, rightForward));
    aMiddle = (left + right) * 0.5f;
    aDirection = Math::Normalize(gl + gr);
    aDistance = g_eyeFocus;
    const Math::Vec3 w = left - right;
    const float b = Math::Dot(gl, gr);
    const float d = Math::Dot(gl, w);
    const float e = Math::Dot(gr, w);
    const float denom = 1.f - b * b;
    if (denom > 1e-6f)
    {
        const float sl = (b * e - d) / denom;
        const float sr = (e - b * d) / denom;
        if (sl > 0.05f && sr > 0.05f)
        {
            const Math::Vec3 meet = (left + gl * sl + right + gr * sr) * 0.5f;
            aDistance = std::clamp(Math::Length(meet - aMiddle) * CentimetersPerMeter, EyeFocusMin, EyeFocusMax);
        }
    }
    return true;
}

int32_t CatalogIndexOf(std::string_view aLabel)
{
    const auto& catalog = Ui::GetBoneCatalog();
    for (size_t i = 0; i < catalog.size(); ++i)
    {
        if (aLabel == catalog[i].label)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Both eyes look at the point that far along their current direction.
void SetEyeFocus(float aCentimeters)
{
    Math::Vec3 middle{};
    Math::Vec3 direction{};
    float current = 0.f;
    if (!GetEyeFocus(middle, direction, current))
    {
        return;
    }
    g_eyeFocus = std::clamp(aCentimeters, EyeFocusMin, EyeFocusMax);
    const Math::Vec3 target = middle + direction * (g_eyeFocus / CentimetersPerMeter);
    const int32_t selected = g_bodyPart;
    const int32_t leftEye = CatalogIndexOf("Left eye");
    const int32_t rightEye = CatalogIndexOf("Right eye");
    if (leftEye >= 0 && rightEye >= 0)
    {
        APM::Ui::PhotoModeUi::AimPart(leftEye, target);
        APM::Ui::PhotoModeUi::AimPart(rightEye, target);
    }
    g_bodyPart = selected; // AimPart selects the eye it turns
}

void ResetCurrentPart()
{
    // IK chains: upper bone, middle bone, end bone.
    constexpr const char* chainBones[4][3] = {{"LeftArm", "LeftForeArm", "LeftHand"},
                                              {"RightArm", "RightForeArm", "RightHand"},
                                              {"LeftUpLeg", "LeftLeg", "LeftFoot"},
                                              {"RightUpLeg", "RightLeg", "RightFoot"}};
    for (const auto* bone : CurrentEntry().bones)
    {
        Pose::PoseOverrides::ResetBone(bone);
        // A pinned limb is placed by its pin: the hand / foot goes back to where the pose puts it (pin kept),
        // the upper / middle bones lose the elbow / knee turn.
        for (size_t c = 0; c < 4; ++c)
        {
            const auto chain = static_cast<Pose::IkChain>(c);
            auto settings = Pose::PoseOverrides::GetIkChain(chain);
            if (!settings.enabled)
            {
                continue;
            }
            if (std::string_view(bone) == chainBones[c][2])
            {
                settings.offset = {};
                settings.swivel = 0.f;
                Pose::PoseOverrides::SetIkChain(chain, settings);
            }
            else if (std::string_view(bone) == chainBones[c][0] || std::string_view(bone) == chainBones[c][1])
            {
                settings.swivel = 0.f;
                Pose::PoseOverrides::SetIkChain(chain, settings);
            }
        }
    }
}

void RunAction(uint32_t aAttribute)
{
    switch (aAttribute)
    {
    case Attr::ResetPart:
        ResetCurrentPart();
        MirrorCurrentPart();
        break;
    case Attr::ResetAll:
        Pose::PoseOverrides::ResetAll();
        break;
    case Attr::SavePose:
        Pose::PoseOverrides::Save(SlotName(g_slot + 1));
        break;
    case Attr::LoadPose:
        Pose::PoseOverrides::Load(SlotName(g_slot + 1));
        break;
    default:
        return;
    }
    RequestRefresh(); // action selectors go back to "-", values and slot labels are redrawn
}
} // namespace

namespace APM::Ui::PhotoModeUi
{
bool IsOwnAttribute(uint32_t aAttribute)
{
    return aAttribute >= Attr::First && aAttribute <= Attr::Last;
}

void RequestRefresh()
{
    g_refresh = true;
}

bool ConsumeRefresh()
{
    return g_refresh.exchange(false);
}

float GetValue(uint32_t aAttribute)
{
    switch (aAttribute)
    {
    case Attr::Enabled:
        return Pose::PoseOverrides::IsEnabled() ? 1.f : 0.f;
    case Attr::BodyPart:
        return static_cast<float>(g_bodyPart);
    case Attr::Bend1:
    case Attr::Bend2:
    case Attr::Twist:
    {
        if (aAttribute == Attr::Twist && IsEyeEntry(CurrentEntry()) && !IsBothEyes())
        {
            return 0.f; // single eyes have no twist
        }
        if (aAttribute == Attr::Twist && IsBothEyes())
        {
            Math::Vec3 middle{};
            Math::Vec3 direction{};
            float distance = g_eyeFocus;
            GetEyeFocus(middle, direction, distance);
            return distance;
        }
        return CurrentAngles()[aAttribute - Attr::Bend1];
    }
    case Attr::MoveX:
    case Attr::MoveY:
    case Attr::MoveZ:
        return CurrentEntry().allowsTranslation
                   ? CurrentTranslation()[aAttribute - Attr::MoveX] * CentimetersPerMeter
                   : 0.f;
    case Attr::IkLimb:
        return static_cast<float>(g_ikLimb);
    case Attr::IkPin:
    {
        const auto chain = Pose::PoseOverrides::GetIkChain(CurrentChain());
        return chain.enabled ? (chain.followBody ? 2.f : 1.f) : 0.f;
    }
    case Attr::IkX:
    case Attr::IkY:
    case Attr::IkZ:
        return Pose::PoseOverrides::GetIkChain(CurrentChain()).offset[aAttribute - Attr::IkX] * CentimetersPerMeter;
    case Attr::IkSwivel:
        return Pose::PoseOverrides::GetIkChain(CurrentChain()).swivel;
    case Attr::Slot:
        return static_cast<float>(g_slot);
    default:
        return 0.f; // action selectors always rest on "-"
    }
}

void SetValue(uint32_t aAttribute, float aValue)
{
    // Redrawing the menu feeds the current values back here: ignore anything that does not change.
    switch (aAttribute)
    {
    case Attr::Enabled:
    {
        const bool enabled = aValue > 0.5f;
        if (enabled != Pose::PoseOverrides::IsEnabled())
        {
            Pose::PoseOverrides::SetEnabled(enabled);
        }
        break;
    }
    case Attr::BodyPart:
    {
        const auto index = static_cast<int32_t>(std::lround(aValue));
        if (index != g_bodyPart)
        {
            g_bodyPart = std::clamp<int32_t>(index, 0, static_cast<int32_t>(GetBoneCatalog().size()) - 1);
            RequestRefresh();
        }
        break;
    }
    case Attr::Bend1:
    case Attr::Bend2:
    case Attr::Twist:
    {
        if (aAttribute == Attr::Twist && IsBothEyes())
        {
            SetEyeFocus(aValue);
            RequestRefresh();
            break;
        }
        auto angles = CurrentAngles();
        auto& angle = angles[aAttribute - Attr::Bend1];
        float value = aValue;
        if (IsEyeEntry(CurrentEntry()))
        {
            value = aAttribute == Attr::Twist ? 0.f : std::clamp(value, -EyeMaxDegrees, EyeMaxDegrees);
        }
        if (!Same(angle, value))
        {
            angle = value;
            ApplyAngles(angles);
            MirrorCurrentPart();
        }
        break;
    }
    case Attr::MoveX:
    case Attr::MoveY:
    case Attr::MoveZ:
    {
        if (!CurrentEntry().allowsTranslation)
        {
            break;
        }
        auto translation = CurrentTranslation();
        auto& component = translation[aAttribute - Attr::MoveX];
        const float meters = aValue / CentimetersPerMeter;
        if (!Same(component, meters))
        {
            component = meters;
            Pose::PoseOverrides::SetTranslation(CurrentEntry().bones.front(), translation[0], translation[1],
                                                translation[2]);
            MirrorCurrentPart();
        }
        break;
    }
    case Attr::IkLimb:
    {
        const auto index = std::clamp<int32_t>(static_cast<int32_t>(std::lround(aValue)), 0, Pose::IkChainCount - 1);
        if (index != g_ikLimb)
        {
            g_ikLimb = index;
            RequestRefresh();
        }
        break;
    }
    case Attr::IkPin:
    case Attr::IkX:
    case Attr::IkY:
    case Attr::IkZ:
    case Attr::IkSwivel:
    {
        auto settings = Pose::PoseOverrides::GetIkChain(CurrentChain());
        auto updated = settings;
        if (aAttribute == Attr::IkPin)
        {
            updated.enabled = aValue > 0.5f;
            updated.followBody = aValue > 1.5f;
            if (settings.enabled && updated.enabled && settings.followBody != updated.followBody)
            {
                KeepPinnedEndInPlace(updated);
            }
            else if (!settings.enabled && updated.enabled)
            {
                PinAtCurrentPlace(updated);
            }
            else if (settings.enabled && !updated.enabled)
            {
                BakeChainPose();
            }
        }
        else if (aAttribute == Attr::IkSwivel)
        {
            updated.swivel = aValue;
        }
        else
        {
            updated.offset[aAttribute - Attr::IkX] = aValue / CentimetersPerMeter;
        }

        const bool changed = updated.enabled != settings.enabled || updated.followBody != settings.followBody ||
                             !Same(updated.swivel, settings.swivel) ||
                             !Same(updated.offset[0], settings.offset[0]) ||
                             !Same(updated.offset[1], settings.offset[1]) ||
                             !Same(updated.offset[2], settings.offset[2]);
        if (changed)
        {
            Pose::PoseOverrides::SetIkChain(CurrentChain(), updated);
            MirrorCurrentChain();
            if (aAttribute == Attr::IkPin)
            {
                RequestRefresh(); // offset sliders become (un)available
            }
        }
        break;
    }
    case Attr::Slot:
    {
        g_slot = std::clamp<int32_t>(static_cast<int32_t>(std::lround(aValue)), 0, SlotCount - 1);
        break;
    }
    case Attr::ResetPart:
    case Attr::ResetAll:
    case Attr::SavePose:
    case Attr::LoadPose:
        if (aValue > 0.5f)
        {
            RunAction(aAttribute);
        }
        break;
    default:
        break;
    }
}

bool IsAttributeEnabled(uint32_t aAttribute)
{
    switch (aAttribute)
    {
    case Attr::MoveX:
    case Attr::MoveY:
    case Attr::MoveZ:
        return CurrentEntry().allowsTranslation;
    case Attr::IkX:
    case Attr::IkY:
    case Attr::IkZ:
    case Attr::IkSwivel:
        return Pose::PoseOverrides::GetIkChain(CurrentChain()).enabled;
    case Attr::Twist:
        return !IsEyeEntry(CurrentEntry()) || IsBothEyes(); // both eyes: focus distance
    default:
        return true;
    }
}

int32_t GetOptionCount(uint32_t aAttribute)
{
    switch (aAttribute)
    {
    case Attr::Enabled:
    case Attr::IkPin:
        return static_cast<int32_t>(std::size(IkPinLabels));
    case Attr::BodyPart:
        return static_cast<int32_t>(GetBoneCatalog().size());
    case Attr::IkLimb:
        return static_cast<int32_t>(std::size(IkLimbLabels));
    case Attr::Slot:
        return SlotCount;
    case Attr::ResetPart:
    case Attr::ResetAll:
    case Attr::SavePose:
    case Attr::LoadPose:
        return static_cast<int32_t>(std::size(ActionLabels));
    default:
        return 0;
    }
}

std::string GetOptionLabel(uint32_t aAttribute, int32_t aIndex)
{
    if (aIndex < 0 || aIndex >= GetOptionCount(aAttribute))
    {
        return {};
    }
    switch (aAttribute)
    {
    case Attr::Enabled:
    case Attr::IkPin:
        return IkPinLabels[aIndex];
    case Attr::BodyPart:
        return GetBoneCatalog()[aIndex].label;
    case Attr::IkLimb:
        return IkLimbLabels[aIndex];
    case Attr::Slot:
    {
        const int32_t slot = aIndex + 1;
        return "Slot " + std::to_string(slot) + (SlotExists(slot) ? " (saved)" : " (empty)");
    }
    case Attr::ResetPart:
    case Attr::ResetAll:
    case Attr::SavePose:
    case Attr::LoadPose:
        return ActionLabels[aIndex];
    default:
        return {};
    }
}

std::string GetOptionGroup(uint32_t aAttribute, int32_t aIndex)
{
    if (aAttribute != Attr::BodyPart || aIndex < 0 || aIndex >= GetOptionCount(aAttribute))
    {
        return {};
    }
    return GetBoneCatalog()[aIndex].group;
}

int32_t GetSlotCount()
{
    return SlotCount;
}

bool IsSlotSaved(int32_t aSlot)
{
    return aSlot >= 0 && aSlot < SlotCount && SlotExists(aSlot + 1);
}

float GetPreference(const std::string& aKey, float aDefault)
{
    const auto file = (GetRuntime().pluginDir / L"panel.ini").wstring();
    const std::wstring key(aKey.begin(), aKey.end());
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(L"Panel", key.c_str(), L"", buffer, static_cast<DWORD>(std::size(buffer)), file.c_str());
    if (buffer[0] == L'\0')
    {
        return aDefault;
    }
    return static_cast<float>(_wtof(buffer));
}

void SetPreference(const std::string& aKey, float aValue)
{
    const auto file = (GetRuntime().pluginDir / L"panel.ini").wstring();
    const std::wstring key(aKey.begin(), aKey.end());
    const std::wstring value = std::to_wstring(aValue);
    WritePrivateProfileStringW(L"Panel", key.c_str(), value.c_str(), file.c_str());
}

bool RotatePart(int32_t aPart, const Math::Vec3& aModelAxis, float aDegrees)
{
    const auto& catalog = GetBoneCatalog();
    if (aPart < 0 || aPart >= static_cast<int32_t>(catalog.size()) || Math::Length(aModelAxis) < 1e-4f)
    {
        return false;
    }
    g_bodyPart = aPart;
    const auto& entry = CurrentEntry();
    const char* bone = entry.bones.front();
    const bool isFinger = UsesFingerAxes(entry);

    // Current bone rotation in model space (with the current edit) and the current edit itself.
    Math::Quat boneModel{};
    if (!Pose::GizmoState::GetBoneModelRotation(RED4ext::CName(bone).hash, boneModel))
    {
        return false;
    }
    const auto angles = CurrentAngles();
    const Math::Quat offset = AnglesToRotation(bone, angles, isFinger);

    // Rotate around a model-space axis: new offset = offset * (B^-1 * R * B).
    const Math::Quat r = Math::FromAxisAngle(Math::Normalize(aModelAxis), Math::DegToRad(aDegrees));
    const Math::Quat local = Math::Mul(Math::Mul(Math::Conjugate(boneModel), r), boneModel);
    const Math::Quat updated = Math::Normalize(Math::Mul(offset, local));

    std::array<float, 3> result{};
    if (!RotationToAngles(bone, updated, isFinger, result))
    {
        return false;
    }
    for (auto& angle : result)
    {
        angle = std::clamp(angle, -180.f, 180.f);
    }
    ApplyAngles(result);
    MirrorCurrentPart();
    RequestRefresh();
    return true;
}

bool CanMovePart(int32_t aPart)
{
    const auto& catalog = GetBoneCatalog();
    return aPart >= 0 && aPart < static_cast<int32_t>(catalog.size()) && catalog[aPart].allowsTranslation;
}

bool MovePart(int32_t aPart, const Math::Vec3& aModelDelta)
{
    if (!CanMovePart(aPart))
    {
        return false;
    }
    g_bodyPart = aPart;
    const char* bone = CurrentEntry().bones.front();
    // The offset is in the parent bone's space.
    Math::Quat parent{};
    if (!Pose::GizmoState::GetParentModelRotation(RED4ext::CName(bone).hash, parent))
    {
        return false;
    }
    const Math::Vec3 local = Math::Rotate(Math::Conjugate(parent), aModelDelta);
    auto translation = CurrentTranslation();
    translation[0] = std::clamp(translation[0] + local.x, -10.f, 10.f);
    translation[1] = std::clamp(translation[1] + local.y, -10.f, 10.f);
    translation[2] = std::clamp(translation[2] + local.z, -10.f, 10.f);
    Pose::PoseOverrides::SetTranslation(bone, translation[0], translation[1], translation[2]);
    MirrorCurrentPart();
    RequestRefresh();
    return true;
}

namespace
{
// Mirror of a model-space rotation across the body's left / right plane (X = 0).
Math::Quat MirrorRotation(const Math::Quat& q)
{
    return {q.i, -q.j, -q.k, q.r};
}

// Writes a bone's edit so that its local rotation becomes aWanted (keeping the face rig link and UI angles right).
bool SetBoneLocal(const char* aBone, const Math::Quat& aWanted, bool aFingerAxes)
{
    Math::Quat animated{};
    Math::Vec3 unused{};
    if (!Pose::GizmoState::GetAnimatedLocal(RED4ext::CName(aBone).hash, animated, unused))
    {
        return false;
    }
    const Math::Quat edit = Math::Normalize(Math::Mul(Math::Conjugate(animated), aWanted));
    std::array<float, 3> angles{};
    if (!RotationToAngles(aBone, edit, aFingerAxes, angles))
    {
        return false;
    }
    for (auto& angle : angles)
    {
        angle = std::clamp(angle, -180.f, 180.f);
    }
    StoreEdit(aBone, angles, aFingerAxes);
    return true;
}
} // namespace

int32_t MirrorParts(bool aFromLeft, const std::string& aGroup)
{
    Pose::TargetRegistry::ReferencePose reference;
    if (!Pose::TargetRegistry::GetReferencePose(reference))
    {
        return 0;
    }
    const auto refIndex = [&](const char* aBone) -> int32_t
    {
        const auto it = std::find(reference.boneNames.begin(), reference.boneNames.end(), RED4ext::CName(aBone).hash);
        return it == reference.boneNames.end() ? -1 : static_cast<int32_t>(std::distance(reference.boneNames.begin(), it));
    };

    const std::string from = aFromLeft ? "Left " : "Right ";
    const std::string to = aFromLeft ? "Right " : "Left ";
    const auto& catalog = GetBoneCatalog();
    int32_t copied = 0;
    std::array<bool, Pose::IkChainCount> touchedChains{};
    constexpr const char* chainBones[Pose::IkChainCount][3] = {{"LeftArm", "LeftForeArm", "LeftHand"},
                                                               {"RightArm", "RightForeArm", "RightHand"},
                                                               {"LeftUpLeg", "LeftLeg", "LeftFoot"},
                                                               {"RightUpLeg", "RightLeg", "RightFoot"}};

    Pose::PoseOverrides::Checkpoint();
    for (const auto& source : catalog)
    {
        const std::string label = source.label;
        if (label.rfind(from, 0) != 0 || (!aGroup.empty() && aGroup != source.group))
        {
            continue;
        }
        if (aGroup.empty() && std::string(source.group) == "Head")
        {
            continue; // mirrored eyes would look apart: the gaze is set with its own gizmo
        }
        const std::string targetLabel = to + label.substr(from.size());
        const auto target = std::find_if(catalog.begin(), catalog.end(),
                                         [&](const CatalogEntry& e) { return targetLabel == e.label; });
        if (target == catalog.end() || target->bones.size() != source.bones.size())
        {
            continue;
        }
        const bool fingerAxes = UsesFingerAxes(*target);
        bool any = false;
        for (size_t b = 0; b < source.bones.size(); ++b)
        {
            const int32_t is = refIndex(source.bones[b]);
            const int32_t it = refIndex(target->bones[b]);
            Math::Quat current{};
            Math::Vec3 translation{};
            if (is < 0 || it < 0 ||
                !Pose::GizmoState::GetFinalLocal(RED4ext::CName(source.bones[b]).hash, current, translation))
            {
                continue;
            }
            // Change from the reference pose, seen in model space at the reference pose, mirrored, then expressed
            // for the other side's bone (the reference pose is symmetric).
            const Math::Quat change = Math::Mul(Math::Conjugate(reference.localRotations[is]), current);
            const Math::Quat changeModel = Math::Mul(Math::Mul(reference.modelRotations[is], change),
                                                     Math::Conjugate(reference.modelRotations[is]));
            const Math::Quat mirrored = MirrorRotation(changeModel);
            const Math::Quat targetChange = Math::Mul(Math::Mul(Math::Conjugate(reference.modelRotations[it]), mirrored),
                                                      reference.modelRotations[it]);
            const Math::Quat wanted = Math::Normalize(Math::Mul(reference.localRotations[it], targetChange));
            any = SetBoneLocal(target->bones[b], wanted, fingerAxes) || any;

            for (size_t c = 0; c < Pose::IkChainCount; ++c)
            {
                for (const char* bone : chainBones[c])
                {
                    touchedChains[c] = touchedChains[c] || std::string(bone) == target->bones[b];
                }
            }
        }
        // Held props: mirror the position offset too (it is in the hand's space).
        if (source.allowsTranslation && target->allowsTranslation)
        {
            std::array<float, 3> offset{};
            Pose::PoseOverrides::GetTranslation(source.bones.front(), offset);
            const int32_t is = refIndex(source.bones.front());
            const int32_t it = refIndex(target->bones.front());
            if (is >= 0 && it >= 0 && reference.parents[is] >= 0 && reference.parents[it] >= 0)
            {
                const Math::Vec3 model =
                    Math::Rotate(reference.modelRotations[reference.parents[is]], Math::Vec3{offset[0], offset[1], offset[2]});
                const Math::Vec3 local =
                    Math::Rotate(Math::Conjugate(reference.modelRotations[reference.parents[it]]), Math::Vec3{-model.x, model.y, model.z});
                Pose::PoseOverrides::SetTranslation(target->bones.front(), local.x, local.y, local.z);
            }
        }
        copied += any ? 1 : 0;
    }
    // The copied side is now posed by rotations (IK results included): its pin would fight them.
    for (size_t c = 0; c < Pose::IkChainCount; ++c)
    {
        if (touchedChains[c])
        {
            auto settings = Pose::PoseOverrides::GetIkChain(static_cast<Pose::IkChain>(c));
            if (settings.enabled)
            {
                settings.enabled = false;
                Pose::PoseOverrides::SetIkChain(static_cast<Pose::IkChain>(c), settings);
            }
        }
    }
    RequestRefresh();
    return copied;
}

bool AimPart(int32_t aPart, const Math::Vec3& aModelTarget)
{
    const auto& catalog = GetBoneCatalog();
    if (aPart < 0 || aPart >= static_cast<int32_t>(catalog.size()))
    {
        return false;
    }
    const char* bone = catalog[aPart].bones.front();
    const uint64_t hash = RED4ext::CName(bone).hash;
    Math::Vec3 position{};
    Math::Quat model{};
    Math::Quat parent{};
    Math::Vec3 forwardLocal{};
    if (!Pose::GizmoState::GetBoneModelPosition(hash, position) || !Pose::GizmoState::GetBoneModelRotation(hash, model) ||
        !Pose::GizmoState::GetParentModelRotation(hash, parent) ||
        !Pose::TargetRegistry::GetBoneReferenceForward(hash, forwardLocal))
    {
        return false;
    }
    Math::Vec3 wanted = Math::Normalize(aModelTarget - position);
    // Eyes: keep the direction within a cone around where the animation (under the current head) points them.
    Math::Quat animatedLocal{};
    Math::Vec3 animatedTranslation{};
    if (IsEyeEntry(catalog[aPart]) && Pose::GizmoState::GetAnimatedLocal(hash, animatedLocal, animatedTranslation))
    {
        const Math::Vec3 neutral = Math::Normalize(Math::Rotate(Math::Mul(parent, animatedLocal), forwardLocal));
        const float maxAngle = EyeMaxDegrees * 3.14159265f / 180.f;
        const Math::Vec3 side = Math::Cross(neutral, wanted);
        if (Math::AngleBetween(neutral, wanted) > maxAngle && Math::Length(side) > 1e-6f)
        {
            wanted = Math::Normalize(Math::Rotate(Math::FromAxisAngle(side, maxAngle), neutral));
        }
    }
    const Math::Vec3 current = Math::Normalize(Math::Rotate(model, forwardLocal));
    Math::Quat turned = model;
    const Math::Vec3 axis = Math::Cross(current, wanted);
    if (Math::Length(axis) > 1e-6f)
    {
        turned = Math::Mul(Math::FromAxisAngle(axis, Math::AngleBetween(current, wanted)), model);
    }
    // Absolute: computed from the animation and the parent, so repeated calls do not add up.
    g_bodyPart = aPart;
    const bool done = SetBoneLocal(bone, Math::Normalize(Math::Mul(Math::Conjugate(parent), turned)), false);
    RequestRefresh();
    return done;
}
} // namespace APM::Ui::PhotoModeUi

namespace
{
using namespace APM;
using APM::Ui::CatalogEntry;
using APM::Ui::GetBoneCatalog;

// Counterpart of a left / right entry ("Left forearm" <-> "Right forearm"), or nullptr.
const CatalogEntry* OtherSide(const CatalogEntry& aEntry)
{
    const std::string label = aEntry.label;
    std::string other;
    if (label.rfind("Left ", 0) == 0)
    {
        other = "Right " + label.substr(5);
    }
    else if (label.rfind("Right ", 0) == 0)
    {
        other = "Left " + label.substr(6);
    }
    if (other.empty())
    {
        return nullptr;
    }
    for (const auto& entry : GetBoneCatalog())
    {
        if (other == entry.label)
        {
            return entry.bones.size() == aEntry.bones.size() ? &entry : nullptr;
        }
    }
    return nullptr;
}

Math::Quat MirrorModel(const Math::Quat& q)
{
    return {q.i, -q.j, -q.k, q.r};
}

// Writes the edit that gives a bone the local rotation aWanted (on top of its animation).
void SetLocalRotation(const char* aBone, const Math::Quat& aAnimated, const Math::Quat& aWanted, bool aFingerAxes)
{
    const Math::Quat edit = Math::Normalize(Math::Mul(Math::Conjugate(aAnimated), aWanted));
    std::array<float, 3> angles{};
    if (RotationToAngles(aBone, edit, aFingerAxes, angles))
    {
        for (auto& angle : angles)
        {
            angle = std::clamp(angle, -180.f, 180.f);
        }
        StoreEdit(aBone, angles, aFingerAxes);
    }
}

constexpr const char* MirrorChainBones[4][4] = {{"LeftArm", "LeftForeArm", "LeftHand", "LeftShoulder"},
                                                {"RightArm", "RightForeArm", "RightHand", "RightShoulder"},
                                                {"LeftUpLeg", "LeftLeg", "LeftFoot", "Hips"},
                                                {"RightUpLeg", "RightLeg", "RightFoot", "Hips"}};

// IK chain that ends at a bone, or -1.
int32_t ChainOfEnd(const std::string& aBone)
{
    for (int32_t c = 0; c < 4; ++c)
    {
        if (aBone == MirrorChainBones[c][2])
        {
            return c;
        }
    }
    return -1;
}

// Pinned hand / foot of the source chain mirrored onto the other chain: same target and orientation, mirrored
// across the body's left / right plane.
void MirrorChain(int32_t aSource, const Pose::TargetRegistry::ReferencePose& aReference)
{
    const int32_t target = aSource ^ 1;
    const auto source = Pose::PoseOverrides::GetIkChain(static_cast<Pose::IkChain>(aSource));
    auto settings = Pose::PoseOverrides::GetIkChain(static_cast<Pose::IkChain>(target));
    if (!source.enabled)
    {
        if (settings.enabled)
        {
            settings.enabled = false;
            Pose::PoseOverrides::SetIkChain(static_cast<Pose::IkChain>(target), settings);
        }
        return;
    }
    const auto hash = [](const char* aName) { return RED4ext::CName(aName).hash; };
    const auto refIndex = [&](const char* aBone) -> int32_t
    {
        const auto it = std::find(aReference.boneNames.begin(), aReference.boneNames.end(), hash(aBone));
        return it == aReference.boneNames.end() ? -1 : static_cast<int32_t>(std::distance(aReference.boneNames.begin(), it));
    };
    // Frames of both limbs: end bone and the bone the limb hangs from, in the animation and now.
    struct Frame
    {
        Math::Vec3 end;
        Math::Quat endRotation;
        Math::Vec3 parent;
        Math::Quat parentRotation;
        Math::Vec3 currentParent;
        Math::Quat currentParentRotation;
    };
    const auto frame = [&](int32_t aChain, Frame& aOut)
    {
        const char* end = MirrorChainBones[aChain][2];
        const char* parent = MirrorChainBones[aChain][3];
        return Pose::GizmoState::GetAnimatedModel(hash(end), aOut.end, aOut.endRotation) &&
               Pose::GizmoState::GetAnimatedModel(hash(parent), aOut.parent, aOut.parentRotation) &&
               Pose::GizmoState::GetBoneModelPosition(hash(parent), aOut.currentParent) &&
               Pose::GizmoState::GetBoneModelRotation(hash(parent), aOut.currentParentRotation);
    };
    Frame from;
    Frame to;
    const int32_t refFrom = refIndex(MirrorChainBones[aSource][2]);
    const int32_t refTo = refIndex(MirrorChainBones[target][2]);
    if (!frame(aSource, from) || !frame(target, to) || refFrom < 0 || refTo < 0)
    {
        return;
    }
    const Math::Vec3 offset{source.offset[0], source.offset[1], source.offset[2]};
    const Math::Quat bodyFrom =
        Math::Normalize(Math::Mul(from.currentParentRotation, Math::Conjugate(from.parentRotation)));
    const Math::Quat bodyTo = Math::Normalize(Math::Mul(to.currentParentRotation, Math::Conjugate(to.parentRotation)));

    // Where the source pins its hand / foot, and how it turns it (model space).
    std::array<float, 3> angles{};
    Pose::PoseOverrides::GetUiAngles(MirrorChainBones[aSource][2], angles);
    const Math::Quat sourceEdit = AnglesToRotation(MirrorChainBones[aSource][2], angles, false);
    Math::Vec3 pin = from.end + offset;
    Math::Quat pinRotation = Math::Mul(from.endRotation, sourceEdit);
    if (source.followBody)
    {
        pin = from.currentParent + Math::Rotate(bodyFrom, from.end + offset - from.parent);
        pinRotation = Math::Mul(bodyFrom, pinRotation);
    }

    // Mirrored for the other limb (the reference pose is symmetric: its frames tell how the two sides differ).
    const Math::Vec3 mirroredPin{-pin.x, pin.y, pin.z};
    const Math::Quat correction =
        Math::Mul(Math::Conjugate(MirrorModel(aReference.modelRotations[refFrom])), aReference.modelRotations[refTo]);
    const Math::Quat mirroredRotation = Math::Normalize(Math::Mul(MirrorModel(pinRotation), correction));

    settings.enabled = true;
    settings.followBody = source.followBody;
    settings.swivel = -source.swivel;
    Math::Vec3 targetOffset = mirroredPin - to.end;
    Math::Quat targetWorld = mirroredRotation;
    if (source.followBody)
    {
        targetOffset = Math::Rotate(Math::Conjugate(bodyTo), mirroredPin - to.currentParent) - to.end + to.parent;
        targetWorld = Math::Mul(Math::Conjugate(bodyTo), mirroredRotation);
    }
    settings.offset = {targetOffset.x, targetOffset.y, targetOffset.z};
    Pose::PoseOverrides::SetIkChain(static_cast<Pose::IkChain>(target), settings);
    // Pinned rotation = (body) * animation * edit.
    Pose::GizmoState::GetAnimatedModel(hash(MirrorChainBones[target][2]), to.end, to.endRotation);
    const Math::Quat targetEdit = Math::Normalize(Math::Mul(Math::Conjugate(to.endRotation), targetWorld));
    std::array<float, 3> targetAngles{};
    if (RotationToAngles(MirrorChainBones[target][2], targetEdit, false, targetAngles))
    {
        for (auto& angle : targetAngles)
        {
            angle = std::clamp(angle, -180.f, 180.f);
        }
        Pose::PoseOverrides::SetRotationQuat(MirrorChainBones[target][2],
                                             AnglesToRotation(MirrorChainBones[target][2], targetAngles, false), targetAngles);
    }
}

// Mirror mode: the whole tab of the part just edited ("Left arm", "Right hand", ...) is copied to the other side as a
// mirror image - the other limb jumps to the same pose and then follows every change.
void MirrorGroup(const std::string& aGroup)
{
    Pose::TargetRegistry::ReferencePose reference;
    if (!g_mirror || aGroup.empty() || aGroup == "Head" || !Pose::TargetRegistry::GetReferencePose(reference))
    {
        return;
    }
    const auto refIndex = [&](const char* aBone) -> int32_t
    {
        const auto it = std::find(reference.boneNames.begin(), reference.boneNames.end(), RED4ext::CName(aBone).hash);
        return it == reference.boneNames.end() ? -1 : static_cast<int32_t>(std::distance(reference.boneNames.begin(), it));
    };
    for (const auto& source : GetBoneCatalog())
    {
        if (aGroup != source.group)
        {
            continue;
        }
        const auto* target = OtherSide(source);
        if (!target)
        {
            continue;
        }
        const bool fingerAxes = UsesFingerAxes(*target);
        for (size_t b = 0; b < source.bones.size(); ++b)
        {
            const int32_t chain = ChainOfEnd(source.bones[b]);
            if (chain >= 0 && Pose::PoseOverrides::GetIkChain(static_cast<Pose::IkChain>(chain)).enabled)
            {
                continue; // its hand / foot is placed by the pin, mirrored below
            }
            const int32_t is = refIndex(source.bones[b]);
            const int32_t it = refIndex(target->bones[b]);
            Math::Quat animatedSource{};
            Math::Quat animatedTarget{};
            Math::Vec3 unused{};
            if (is < 0 || it < 0 ||
                !Pose::GizmoState::GetAnimatedLocal(RED4ext::CName(source.bones[b]).hash, animatedSource, unused) ||
                !Pose::GizmoState::GetAnimatedLocal(RED4ext::CName(target->bones[b]).hash, animatedTarget, unused))
            {
                continue;
            }
            // The source bone's local rotation now (animation + edit), as a change from the symmetric reference pose
            // seen in model space, mirrored, and given to the other bone.
            std::array<float, 3> angles{};
            Pose::PoseOverrides::GetUiAngles(source.bones[b], angles);
            const Math::Quat local = Math::Mul(animatedSource, AnglesToRotation(source.bones[b], angles, UsesFingerAxes(source)));
            const Math::Quat change = Math::Mul(Math::Conjugate(reference.localRotations[is]), local);
            const Math::Quat changeModel =
                Math::Mul(Math::Mul(reference.modelRotations[is], change), Math::Conjugate(reference.modelRotations[is]));
            const Math::Quat targetChange = Math::Mul(
                Math::Mul(Math::Conjugate(reference.modelRotations[it]), MirrorModel(changeModel)), reference.modelRotations[it]);
            SetLocalRotation(target->bones[b], animatedTarget,
                             Math::Normalize(Math::Mul(reference.localRotations[it], targetChange)), fingerAxes);
        }
        if (source.allowsTranslation && target->allowsTranslation)
        {
            std::array<float, 3> offset{};
            Pose::PoseOverrides::GetTranslation(source.bones.front(), offset);
            const int32_t is = refIndex(source.bones.front());
            const int32_t it = refIndex(target->bones.front());
            if (is >= 0 && it >= 0 && reference.parents[is] >= 0 && reference.parents[it] >= 0)
            {
                const Math::Vec3 model =
                    Math::Rotate(reference.modelRotations[reference.parents[is]], Math::Vec3{offset[0], offset[1], offset[2]});
                const Math::Vec3 local = Math::Rotate(Math::Conjugate(reference.modelRotations[reference.parents[it]]),
                                                      Math::Vec3{-model.x, model.y, model.z});
                Pose::PoseOverrides::SetTranslation(target->bones.front(), local.x, local.y, local.z);
            }
        }
    }
    // Limbs: mirror the pin (or drop the other side's pin when this side has none).
    for (int32_t c = 0; c < 4; ++c)
    {
        const bool sourceSide = std::string(MirrorChainBones[c][2]).rfind(aGroup.substr(0, aGroup.find(' ')), 0) == 0;
        const bool limbGroup = aGroup.find("arm") != std::string::npos ? c < 2 : (aGroup.find("leg") != std::string::npos && c >= 2);
        if (sourceSide && limbGroup)
        {
            MirrorChain(c, reference);
        }
    }
}

void MirrorCurrentPart()
{
    MirrorGroup(CurrentEntry().group);
}

void MirrorCurrentChain()
{
    // The pinned limb's tab ("Left arm" for the left hand, ...).
    constexpr const char* groups[] = {"Left arm", "Right arm", "Left leg", "Right leg"};
    MirrorGroup(groups[static_cast<size_t>(CurrentChain())]);
}
} // namespace

namespace APM::Ui::PhotoModeUi
{
void SetMirror(bool aMirror)
{
    g_mirror = aMirror; // nothing happens until a left / right part is edited
}

bool IsMirror()
{
    return g_mirror;
}

void SetLimits(bool aLimits)
{
    Pose::JointLimits::SetEnabled(aLimits);
}

bool AreLimitsOn()
{
    return Pose::JointLimits::IsEnabled();
}

bool ApplyHandPreset(bool aRightHand, int32_t aPreset)
{
    // Finger curl of each segment (degrees towards the palm, from the straight reference fingers) and the thumb's
    // local rotations from the reference pose (base, first segment, tip; the same on both hands, the rig is mirrored).
    // Worked out on V's rig: the fist and point thumbs lie across the middle segments of the fingers, the grip makes
    // a round hole around a ~34 mm handle with the thumb closing it.
    struct Preset
    {
        std::array<std::array<float, 3>, 4> fingers; // index, middle, ring, little
        std::array<Math::Quat, 3> thumb;             // i, j, k, r
    };
    static const std::array<Preset, 5> Presets = {{
        // Open: straight fingers
        {{{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}},
         {{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f, 1.f}}}},
        // Relaxed: a little more curl towards the little finger
        {{{{15.f, 20.f, 10.f}, {20.f, 25.f, 12.f}, {25.f, 30.f, 15.f}, {30.f, 35.f, 18.f}}},
         {{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, -0.13053f, 0.99144f}, {0.f, 0.f, -0.13053f, 0.99144f}}}},
        // Fist: the thumb across the middle segments of the index and middle fingers
        {{{{85.f, 95.f, 60.f}, {85.f, 95.f, 60.f}, {85.f, 95.f, 60.f}, {85.f, 95.f, 60.f}}},
         {{{0.16598f, -0.17244f, -0.05488f, 0.96938f},
           {0.f, 0.03699f, -0.47433f, 0.87957f},
           {0.f, -0.13479f, -0.43234f, 0.89158f}}}},
        // Point: index straight, the thumb a little further in, over the middle finger
        {{{{0.f, 0.f, 0.f}, {85.f, 95.f, 60.f}, {85.f, 95.f, 60.f}, {85.f, 95.f, 60.f}}},
         {{{0.00355f, -0.05227f, -0.18870f, 0.98064f},
           {0.f, 0.00319f, -0.44396f, 0.89604f},
           {0.f, 0.03417f, -0.40269f, 0.91470f}}}},
        // Grip: round around a handle, the thumb closing the ring
        {{{{26.2f, 72.3f, 51.5f}, {35.9f, 88.5f, 57.7f}, {24.5f, 74.8f, 55.5f}, {11.2f, 48.6f, 45.5f}}},
         {{{-0.03066f, 0.13140f, -0.03410f, 0.99027f},
           {0.f, -0.20083f, -0.31434f, 0.92782f},
           {0.f, 0.07786f, -0.63811f, 0.76600f}}}},
    }};
    if (aPreset < 0 || aPreset >= static_cast<int32_t>(Presets.size()))
    {
        return false;
    }
    const auto& preset = Presets[static_cast<size_t>(aPreset)];
    const std::string side = aRightHand ? "Right" : "Left";
    g_noLimits = true;
    bool any = false;

    const std::array<std::string, 3> thumbBones = {side + "InHandThumb", side + "HandThumb1", side + "HandThumb2"};
    for (size_t s = 0; s < thumbBones.size(); ++s)
    {
        Math::Quat reference{};
        float sign = 1.f;
        if (!Pose::JointLimits::GetReferenceFrame(thumbBones[s], true, reference, sign))
        {
            continue;
        }
        const Math::Quat wanted = Math::Normalize(Math::Mul(reference, preset.thumb[s]));
        any = SetBoneLocal(thumbBones[s].c_str(), wanted, true) || any;
    }

    static constexpr const char* Fingers[] = {"Index", "Middle", "Ring", "Pinky"};
    for (size_t f = 0; f < std::size(Fingers); ++f)
    {
        for (int32_t s = 0; s < 3; ++s)
        {
            const std::string bone = side + "Hand" + Fingers[f] + std::to_string(s + 1);
            Math::Quat reference{};
            float sign = 1.f;
            if (!Pose::JointLimits::GetReferenceFrame(bone, true, reference, sign))
            {
                continue;
            }
            const std::array<float, 3> angles = {preset.fingers[f][static_cast<size_t>(s)] * sign, 0.f, 0.f};
            const Math::Quat wanted = Math::Normalize(Math::Mul(reference, AnglesToRotation(bone.c_str(), angles, true)));
            any = SetBoneLocal(bone.c_str(), wanted, true) || any;
        }
    }
    MirrorGroup(aRightHand ? "Right hand" : "Left hand");
    g_noLimits = false;
    RequestRefresh();
    return any;
}
} // namespace APM::Ui::PhotoModeUi
