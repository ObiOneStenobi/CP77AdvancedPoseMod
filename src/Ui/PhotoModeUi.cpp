#include "Ui/PhotoModeUi.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Math/Vec3.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/BoneCatalog.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <utility>

namespace
{
using namespace APM;
using namespace APM::Ui::PhotoModeUi;

constexpr int32_t SlotCount = 10;
constexpr const char* IkLimbLabels[] = {"Left hand", "Right hand", "Left foot", "Right foot"};
constexpr const char* OnOffLabels[] = {"Off", "On"};
constexpr const char* IkPinLabels[] = {"Off", "World", "Body"};
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
        Pose::PoseOverrides::SetRotationQuat(entry.bones[i], AnglesToRotation(entry.bones[i], angles, isFinger),
                                             angles);
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

void ResetCurrentPart()
{
    for (const auto* bone : CurrentEntry().bones)
    {
        Pose::PoseOverrides::ResetBone(bone);
    }
}

void RunAction(uint32_t aAttribute)
{
    switch (aAttribute)
    {
    case Attr::ResetPart:
        ResetCurrentPart();
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
        return CurrentAngles()[aAttribute - Attr::Bend1];
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
        auto angles = CurrentAngles();
        auto& angle = angles[aAttribute - Attr::Bend1];
        if (!Same(angle, aValue))
        {
            angle = aValue;
            ApplyAngles(angles);
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
    RequestRefresh();
    return true;
}
} // namespace APM::Ui::PhotoModeUi
