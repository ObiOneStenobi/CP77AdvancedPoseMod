#include "Scripting/ScriptApi.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Pose/FaceRig.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/PoseProcessor.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/BoneCatalog.hpp"
#include "Ui/PhotoModeUi.hpp"
#include "Ui/TextEntry.hpp"

#include <RED4ext/RED4ext.hpp>

#include <Windows.h>
#include <shellapi.h>
#include <RED4ext/TweakDB.hpp>
#include <RED4ext/Scripting/Natives/gamedataTweakDBRecord.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <algorithm>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

// Global native functions exposed to redscript (and to CET through the RTTI).
// Declarations for redscript live in scripts/AdvancedPoseMod/Natives.reds.
namespace
{
using RED4ext::CName;
using RED4ext::CStackFrame;
using RED4ext::CString;
using RED4ext::IScriptable;

template<typename T>
void Return(void* aOut, const T& aValue)
{
    if (aOut)
    {
        *static_cast<T*>(aOut) = aValue;
    }
}

std::string ToString(CName aName)
{
    const char* text = aName.ToString();
    return text ? std::string(text) : std::string();
}

// Cached list of saved pose names, refreshed by APM_GetPoseCount().
std::vector<std::string> g_poseNames;

// --- Photo Mode / skeleton ---------------------------------------------------------------------------------------

void IsPhotoModeActive(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::PhotoModeTracker::IsActive());
}

void GetBoneCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(APM::Pose::TargetRegistry::GetMainSkeletonBones().size()));
}

void GetBoneName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;

    const auto bones = APM::Pose::TargetRegistry::GetMainSkeletonBones();
    Return(aOut, (index >= 0 && index < static_cast<int32_t>(bones.size())) ? CName(bones[index]) : CName());
}

// --- Bone overrides ----------------------------------------------------------------------------------------------

void SetBoneRotation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    float x = 0.f, y = 0.f, z = 0.f;
    RED4ext::GetParameter(aFrame, &bone);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    aFrame->code++;

    const auto name = ToString(bone);
    if (!name.empty())
    {
        APM::Pose::PoseOverrides::SetRotation(name, x, y, z);
    }
    Return(aOut, !name.empty());
}

void SetBoneTranslation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    float x = 0.f, y = 0.f, z = 0.f;
    RED4ext::GetParameter(aFrame, &bone);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    aFrame->code++;

    const auto name = ToString(bone);
    if (!name.empty())
    {
        APM::Pose::PoseOverrides::SetTranslation(name, x, y, z);
    }
    Return(aOut, !name.empty());
}

// Returns (x, y, z, w): xyz = value, w = 1 if the bone has this override, 0 otherwise.
void GetBoneRotation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;

    std::array<float, 3> value{};
    const bool has = APM::Pose::PoseOverrides::GetRotation(ToString(bone), value);
    Return(aOut, RED4ext::Vector4{value[0], value[1], value[2], has ? 1.f : 0.f});
}

void GetBoneTranslation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;

    std::array<float, 3> value{};
    const bool has = APM::Pose::PoseOverrides::GetTranslation(ToString(bone), value);
    Return(aOut, RED4ext::Vector4{value[0], value[1], value[2], has ? 1.f : 0.f});
}

void ResetBone(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;
    APM::Pose::PoseOverrides::ResetBone(ToString(bone));
}

void ResetAll(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Pose::PoseOverrides::ResetAll();
}

// --- IK chains: "LeftHand", "RightHand", "LeftFoot", "RightFoot" ------------------------------------------------

void SetIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    bool enabled = false;
    float x = 0.f, y = 0.f, z = 0.f, swivel = 0.f;
    RED4ext::GetParameter(aFrame, &chainName);
    RED4ext::GetParameter(aFrame, &enabled);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    RED4ext::GetParameter(aFrame, &swivel);
    aFrame->code++;

    APM::Pose::IkChain chain;
    if (!APM::Pose::ParseIkChain(ToString(chainName), chain))
    {
        Return(aOut, false);
        return;
    }
    APM::Pose::IkChainSettings settings;
    settings.enabled = enabled;
    settings.offset = {x, y, z};
    settings.swivel = swivel;
    APM::Pose::PoseOverrides::SetIkChain(chain, settings);
    Return(aOut, true);
}

// Returns (offsetX, offsetY, offsetZ, swivel). Use APM_IsIkChainEnabled for the switch.
void GetIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    RED4ext::GetParameter(aFrame, &chainName);
    aFrame->code++;

    APM::Pose::IkChain chain;
    APM::Pose::IkChainSettings settings;
    if (APM::Pose::ParseIkChain(ToString(chainName), chain))
    {
        settings = APM::Pose::PoseOverrides::GetIkChain(chain);
    }
    Return(aOut, RED4ext::Vector4{settings.offset[0], settings.offset[1], settings.offset[2], settings.swivel});
}

void IsIkChainEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    RED4ext::GetParameter(aFrame, &chainName);
    aFrame->code++;

    APM::Pose::IkChain chain;
    Return(aOut, APM::Pose::ParseIkChain(ToString(chainName), chain) && APM::Pose::PoseOverrides::GetIkChain(chain).enabled);
}

void SetEnabled(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool enabled = false;
    RED4ext::GetParameter(aFrame, &enabled);
    aFrame->code++;
    APM::Pose::PoseOverrides::SetEnabled(enabled);
}

void IsEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::IsEnabled());
}

// --- Pose files --------------------------------------------------------------------------------------------------

void SavePose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Save(name.c_str()));
}

void LoadPose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Load(name.c_str()));
}

void GetPoseCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    g_poseNames = APM::Pose::PoseOverrides::ListSaved();
    Return(aOut, static_cast<int32_t>(g_poseNames.size()));
}

void GetPoseName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    const bool valid = index >= 0 && index < static_cast<int32_t>(g_poseNames.size());
    Return(aOut, CString(valid ? g_poseNames[index].c_str() : ""));
}

void DeletePose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Delete(name.c_str()));
}

void RenamePose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString from;
    CString to;
    RED4ext::GetParameter(aFrame, &from);
    RED4ext::GetParameter(aFrame, &to);
    aFrame->code++;
    Return(aOut, CString(APM::Pose::PoseOverrides::Rename(from.c_str(), to.c_str()).c_str()));
}

void GetBasePoseName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Pose::PoseOverrides::GetBasePoseName().c_str()));
}

// Item records the prop picker offers: 0 weapons, 1 consumables, 2 other items. Cached on first use.
const std::vector<uint64_t>& PropRecords(int32_t aKind)
{
    static std::array<std::vector<uint64_t>, 3> lists;
    static std::array<bool, 3> loaded{};
    if (aKind < 0 || aKind > 2)
    {
        static const std::vector<uint64_t> empty;
        return empty;
    }
    if (!loaded[aKind])
    {
        constexpr const char* types[] = {"gamedataWeaponItem_Record", "gamedataConsumableItem_Record",
                                         "gamedataItem_Record"};
        auto* type = RED4ext::CRTTISystem::Get()->GetType(types[aKind]);
        auto* tweakDb = RED4ext::TweakDB::Get();
        if (type && tweakDb)
        {
            for (const auto& handle : tweakDb->GetRecordsByType(type))
            {
                if (const auto* record = reinterpret_cast<const RED4ext::game::data::TweakDBRecord*>(handle.GetPtr()))
                {
                    lists[aKind].push_back(record->recordID.value);
                }
            }
            loaded[aKind] = true;
            APM::Log::Verbose("Props: %zu records of %s", lists[aKind].size(), types[aKind]);
        }
    }
    return lists[aKind];
}

void PropRecordCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t kind = 0;
    RED4ext::GetParameter(aFrame, &kind);
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(PropRecords(kind).size()));
}

void PropRecordAt(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t kind = 0;
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &kind);
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    const auto& list = PropRecords(kind);
    Return(aOut, index >= 0 && index < static_cast<int32_t>(list.size()) ? list[index] : uint64_t{0});
}

void RecordFromNumber(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint64_t number = 0;
    RED4ext::GetParameter(aFrame, &number);
    aFrame->code++;
    RED4ext::TweakDBID id;
    id.value = number;
    Return(aOut, id);
}

void SetHandProp(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool right = true;
    uint64_t record = 0;
    RED4ext::GetParameter(aFrame, &right);
    RED4ext::GetParameter(aFrame, &record);
    aFrame->code++;
    APM::Pose::PoseOverrides::SetHandProp(right, record);
}

void GetHandProp(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    bool right = true;
    RED4ext::GetParameter(aFrame, &right);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::GetHandProp(right));
}

void HistoryCheckpoint(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Pose::PoseOverrides::Checkpoint();
}

void HistoryUndo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    const bool done = APM::Pose::PoseOverrides::Undo();
    APM::Ui::PhotoModeUi::RequestRefresh();
    Return(aOut, done);
}

void HistoryRedo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    const bool done = APM::Pose::PoseOverrides::Redo();
    APM::Ui::PhotoModeUi::RequestRefresh();
    Return(aOut, done);
}

void HistoryCanUndo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::CanUndo());
}

void HistoryCanRedo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::CanRedo());
}

void PollHotkey(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::TextEntry::ConsumeHotkey());
}

// Alt held (free trackball rotation while dragging a gizmo).
void IsAltDown(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
}

void ApplyHandPreset(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    bool right = true;
    int32_t preset = 0;
    RED4ext::GetParameter(aFrame, &right);
    RED4ext::GetParameter(aFrame, &preset);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::ApplyHandPreset(right, preset));
}

void MirrorParts(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    bool fromLeft = true;
    CString group;
    RED4ext::GetParameter(aFrame, &fromLeft);
    RED4ext::GetParameter(aFrame, &group);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::MirrorParts(fromLeft, group.c_str()));
}

void SetMirror(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool mirror = false;
    RED4ext::GetParameter(aFrame, &mirror);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetMirror(mirror);
}

void SetLimits(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool limits = true;
    RED4ext::GetParameter(aFrame, &limits);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetLimits(limits);
}

void AreLimitsOn(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::AreLimitsOn());
}

void IsMirror(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsMirror());
}

void SetNeutralPose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    bool armsDown = false;
    RED4ext::GetParameter(aFrame, &armsDown);
    aFrame->code++;
    const bool done = APM::Pose::PoseOverrides::SetNeutralPose(armsDown);
    APM::Ui::PhotoModeUi::RequestRefresh();
    Return(aOut, done);
}

void GizmoAimPart(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::Vector4 target{};
    RED4ext::GetParameter(aFrame, &part);
    RED4ext::GetParameter(aFrame, &target);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::AimPart(part, {target.X, target.Y, target.Z}));
}

void SetCameraModel(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    RED4ext::Vector4 position{};
    RED4ext::Vector4 forward{};
    RED4ext::Vector4 up{};
    RED4ext::GetParameter(aFrame, &position);
    RED4ext::GetParameter(aFrame, &forward);
    RED4ext::GetParameter(aFrame, &up);
    aFrame->code++;
    APM::Pose::LookAtCamera::SetCamera({position.X, position.Y, position.Z}, {forward.X, forward.Y, forward.Z},
                                       {up.X, up.Y, up.Z});
}

// --- Face ------------------------------------------------------------------------------------------------------
std::vector<std::string> g_expressionNames;

void FaceControlCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(APM::Pose::FaceRig::ControlCount()));
}

void FaceControlLabel(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t control = 0;
    RED4ext::GetParameter(aFrame, &control);
    aFrame->code++;
    Return(aOut, CString(control >= 0 ? APM::Pose::FaceRig::ControlLabel(static_cast<size_t>(control)) : ""));
}

void FaceControlZone(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t control = 0;
    RED4ext::GetParameter(aFrame, &control);
    aFrame->code++;
    Return(aOut, CString(control >= 0 ? APM::Pose::FaceRig::ControlZone(static_cast<size_t>(control)) : ""));
}

void FaceControlIsAngle(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t control = 0;
    RED4ext::GetParameter(aFrame, &control);
    aFrame->code++;
    Return(aOut, control >= 0 && APM::Pose::FaceRig::ControlIsAngle(static_cast<size_t>(control)));
}

void FaceIsEdited(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t control = 0;
    RED4ext::GetParameter(aFrame, &control);
    aFrame->code++;
    Return(aOut, control >= 0 && APM::Pose::PoseOverrides::GetFaceEdit(static_cast<size_t>(control)) != 0.f);
}

void FaceResetControl(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    int32_t control = 0;
    RED4ext::GetParameter(aFrame, &control);
    aFrame->code++;
    if (control < 0)
    {
        return;
    }
    APM::Pose::PoseOverrides::SetFaceEdit(static_cast<size_t>(control), 0.f);
    const int32_t twin = APM::Pose::FaceRig::ControlTwin(static_cast<size_t>(control));
    if (APM::Ui::PhotoModeUi::IsMirror() && twin >= 0)
    {
        APM::Pose::PoseOverrides::SetFaceEdit(static_cast<size_t>(twin), 0.f);
    }
}

void FaceReset(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Pose::PoseOverrides::ResetFace();
}

void FaceUnload(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Pose::PoseOverrides::SetFaceBase(nullptr);
}

void FaceGetLoadedName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Pose::PoseOverrides::GetFaceBaseName().c_str()));
}

void FaceReloadList(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    g_expressionNames = APM::Pose::FaceRig::ListSaved();
}

void FaceListCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(g_expressionNames.size()));
}

void FaceListName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    const bool valid = index >= 0 && index < static_cast<int32_t>(g_expressionNames.size());
    Return(aOut, CString(valid ? g_expressionNames[index].c_str() : ""));
}

void FaceSave(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::FaceRig::Save(name.c_str()));
}

void FaceSaveNew(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Pose::FaceRig::SaveNew().c_str()));
}

void FaceLoad(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::FaceRig::Load(name.c_str()));
}

void FaceDelete(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::FaceRig::Delete(name.c_str()));
}

// False when the game was updated and the hooks could not be installed (the panel says so).
void IsReady(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    const auto& rt = APM::GetRuntime();
    Return(aOut, rt.trackerReady && rt.animationReady);
}

// Opens the folder of saved poses (0) or saved expressions (1) in Explorer.
void OpenFolder(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t kind = 0;
    RED4ext::GetParameter(aFrame, &kind);
    aFrame->code++;
    const auto path = kind == 1 ? APM::Pose::FaceRig::GetDirectory() : APM::Pose::PoseOverrides::GetPoseDirectory();
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    const auto result = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    Return(aOut, result > 32);
}

void SetWheelCapture(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool capture = false;
    RED4ext::GetParameter(aFrame, &capture);
    aFrame->code++;
    APM::Ui::TextEntry::SetWheelCapture(capture);
}

void ConsumeWheel(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::TextEntry::ConsumeWheel());
}

void TextEntryBegin(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString text;
    RED4ext::GetParameter(aFrame, &text);
    aFrame->code++;
    Return(aOut, APM::Ui::TextEntry::Begin(text.c_str()));
}

void TextEntryEnd(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Ui::TextEntry::End();
}

void TextEntryGetState(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(APM::Ui::TextEntry::GetState()));
}

void TextEntryGetText(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Ui::TextEntry::GetText().c_str()));
}

void SaveNewPose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Pose::PoseOverrides::SaveNew().c_str()));
}

void PrefGet(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString key;
    float fallback = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &fallback);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetPreference(key.c_str(), fallback));
}

void PrefSet(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString key;
    float value = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetPreference(key.c_str(), value);
    Return(aOut, true);
}

// Panel values kept for this game session only.
std::unordered_map<std::string, float>& SessionValues()
{
    static std::unordered_map<std::string, float> values;
    return values;
}

void SessionGet(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString key;
    float fallback = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &fallback);
    aFrame->code++;
    const auto it = SessionValues().find(key.c_str());
    Return(aOut, it == SessionValues().end() ? fallback : it->second);
}

void SessionSet(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    CString key;
    float value = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    SessionValues()[key.c_str()] = value;
}

// Writes a line from the scripts to the plugin log (diagnostics).
void LogInfo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString text;
    RED4ext::GetParameter(aFrame, &text);
    aFrame->code++;
    APM::Log::Verbose("Script: %s", text.c_str());
    Return(aOut, true);
}

// --- Gizmos ------------------------------------------------------------------------------------------------------

// Model-space position (relative to V's entity: X right, Y forward, Z up) of the main bone of a body part
// (index in the body part list); w = 1 when valid.
void GizmoGetPartPosition(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    APM::Math::Vec3 position{};
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) &&
        APM::Pose::GizmoState::GetBoneModelPosition(RED4ext::CName(catalog[part].bones.front()).hash, position))
    {
        result = {position.x, position.y, position.z, 1.f};
    }
    Return(aOut, result);
}

// Model-space point where a part's gizmo is drawn: the joint, or halfway to the tip for palm bones; w = 1 when valid.
void GizmoGetPartMarker(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()))
    {
        const auto hash = RED4ext::CName(catalog[part].bones.front()).hash;
        APM::Math::Vec3 joint{};
        APM::Math::Vec3 tip{};
        if (APM::Pose::GizmoState::GetBoneModelPosition(hash, joint))
        {
            if (catalog[part].markerAtMiddle && APM::Pose::GizmoState::GetBoneTipModelPosition(hash, tip))
            {
                joint = (joint + tip) * 0.5f;
            }
            result = {joint.x, joint.y, joint.z, 1.f};
        }
    }
    Return(aOut, result);
}

// Model-space point the main bone of a body part points at (w = 1 when valid); the gizmos rotate parts so that this
// point follows the cursor.
void GizmoGetPartTip(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    APM::Math::Vec3 position{};
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) &&
        APM::Pose::GizmoState::GetBoneTipModelPosition(RED4ext::CName(catalog[part].bones.front()).hash, position))
    {
        result = {position.x, position.y, position.z, 1.f};
    }
    Return(aOut, result);
}

// V's Photo Mode puppet, so scripts can read its world transform. Null when unknown.
void GizmoGetEntity(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;

    RED4ext::Handle<RED4ext::ISerializable> handle;
    const auto entity = APM::Pose::GizmoState::GetEntity();
    if (entity && APM::Pose::TargetRegistry::ContainsPointer(entity))
    {
        // Share the entity's existing reference count instead of creating a new one.
        handle = reinterpret_cast<RED4ext::ISerializable*>(entity)->ref.Lock();
    }
    if (aOut)
    {
        new (aOut) RED4ext::Handle<RED4ext::ISerializable>(handle);
    }
}

// Animated component of V's main skeleton: its GetLocalToWorld() maps bone model space to the world.
void GizmoGetComponent(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;

    RED4ext::Handle<RED4ext::ISerializable> handle;
    const auto component = APM::Pose::GizmoState::GetComponent();
    if (component && APM::Pose::TargetRegistry::ContainsPointer(component))
    {
        handle = reinterpret_cast<RED4ext::ISerializable*>(component)->ref.Lock();
    }
    if (aOut)
    {
        new (aOut) RED4ext::Handle<RED4ext::ISerializable>(handle);
    }
}

// Rotates a body part around a model-space axis (degrees) - used by the gizmos for view-relative rotation.
void GizmoRotatePart(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::Vector4 axis{};
    float degrees = 0.f;
    RED4ext::GetParameter(aFrame, &part);
    RED4ext::GetParameter(aFrame, &axis);
    RED4ext::GetParameter(aFrame, &degrees);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::RotatePart(part, {axis.X, axis.Y, axis.Z}, degrees));
}

// Moves a part that allows it (hips, held props) by a model-space vector (meters).
void GizmoMovePart(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::Vector4 delta{};
    RED4ext::GetParameter(aFrame, &part);
    RED4ext::GetParameter(aFrame, &delta);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::MovePart(part, {delta.X, delta.Y, delta.Z}));
}

void GizmoCanMovePart(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::CanMovePart(part));
}

// IK chain driven by a body part (0..3), or -1.
void GizmoGetPartIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    int32_t chain = -1;
    const auto& catalog = APM::Ui::GetBoneCatalog();
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) && catalog[part].bones.size() == 1)
    {
        const std::string bone = catalog[part].bones.front();
        constexpr const char* ends[] = {"LeftHand", "RightHand", "LeftFoot", "RightFoot"};
        for (int32_t i = 0; i < 4; ++i)
        {
            if (bone == ends[i])
            {
                chain = i;
            }
        }
    }
    Return(aOut, chain);
}

// Bones of the IK chains, in APM::Pose::IkChain order: upper, middle, end.
constexpr const char* IkChainBones[4][3] = {
    {"LeftArm", "LeftForeArm", "LeftHand"},
    {"RightArm", "RightForeArm", "RightHand"},
    {"LeftUpLeg", "LeftLeg", "LeftFoot"},
    {"RightUpLeg", "RightLeg", "RightFoot"},
};

// IK chain whose upper or middle bone (upper arm / forearm, thigh / shin) is a body part, or -1.
void GizmoGetPartSwivelChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    int32_t chain = -1;
    const auto& catalog = APM::Ui::GetBoneCatalog();
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) && catalog[part].bones.size() == 1)
    {
        const std::string bone = catalog[part].bones.front();
        for (int32_t i = 0; i < 4; ++i)
        {
            if (bone == IkChainBones[i][0] || bone == IkChainBones[i][1])
            {
                chain = i;
            }
        }
    }
    Return(aOut, chain);
}

// True when the custom pose is on and the IK chain pins its hand / foot.
void GizmoIsIkActive(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t chain = 0;
    RED4ext::GetParameter(aFrame, &chain);
    aFrame->code++;

    bool active = false;
    if (chain >= 0 && chain < static_cast<int32_t>(APM::Pose::IkChainCount))
    {
        active = APM::Pose::PoseOverrides::IsEnabled() &&
                 APM::Pose::PoseOverrides::GetIkChain(static_cast<APM::Pose::IkChain>(chain)).enabled;
    }
    Return(aOut, active);
}

// Model-space position of a joint of an IK chain (0 shoulder / hip, 1 elbow / knee, 2 hand / foot); w = 1 when valid.
void GizmoGetChainJoint(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t chain = 0;
    int32_t joint = 0;
    RED4ext::GetParameter(aFrame, &chain);
    RED4ext::GetParameter(aFrame, &joint);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    APM::Math::Vec3 position{};
    if (chain >= 0 && chain < 4 && joint >= 0 && joint < 3 &&
        APM::Pose::GizmoState::GetBoneModelPosition(RED4ext::CName(IkChainBones[chain][joint]).hash, position))
    {
        result = {position.x, position.y, position.z, 1.f};
    }
    Return(aOut, result);
}

// True for body parts that only the close-up gizmos edit (outer finger segments).
void GizmoIsPartLocked(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsPartLookAtLocked(part));
}

void GizmoIsPartDetail(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    const auto& catalog = APM::Ui::GetBoneCatalog();
    Return(aOut, part >= 0 && part < static_cast<int32_t>(catalog.size()) &&
                     (catalog[part].mode == APM::Ui::EntryMode::FingerSegment || catalog[part].detail));
}

// Model-space direction the main bone of a body part looks along (the bone axis that pointed forward in the
// reference pose, e.g. an eye's gaze); w = 1 when valid.
void GizmoGetPartForward(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()))
    {
        const auto hash = RED4ext::CName(catalog[part].bones.front()).hash;
        APM::Math::Vec3 localForward{};
        APM::Math::Quat rotation{};
        if (APM::Pose::TargetRegistry::GetBoneReferenceForward(hash, localForward) &&
            APM::Pose::GizmoState::GetBoneModelRotation(hash, rotation))
        {
            const auto forward = APM::Math::Rotate(rotation, localForward);
            result = {forward.x, forward.y, forward.z, 1.f};
        }
    }
    Return(aOut, result);
}

// Model-space position of the closest ancestor bone that has a gizmo of its own (thigh -> hips); w = 1 when valid.
void GizmoGetPartParent(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    static const std::vector<uint64_t> gizmoBones = []
    {
        std::vector<uint64_t> bones;
        for (const auto& entry : APM::Ui::GetBoneCatalog())
        {
            if (entry.bones.size() == 1 || entry.mode == APM::Ui::EntryMode::Curl)
            {
                bones.push_back(RED4ext::CName(entry.bones.front()).hash);
            }
        }
        return bones;
    }();

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    APM::Math::Vec3 position{};
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) &&
        APM::Pose::GizmoState::GetAncestorModelPosition(RED4ext::CName(catalog[part].bones.front()).hash, gizmoBones,
                                                        position))
    {
        result = {position.x, position.y, position.z, 1.f};
    }
    Return(aOut, result);
}

// Model-space movement of an IK pin -> change of its offset (the same, unless the chain is pinned to the body).
void GizmoIkModelToOffset(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t chain = 0;
    RED4ext::Vector4 model{};
    RED4ext::GetParameter(aFrame, &chain);
    RED4ext::GetParameter(aFrame, &model);
    aFrame->code++;

    APM::Math::Vec3 result{model.X, model.Y, model.Z};
    if (chain >= 0 && chain < static_cast<int32_t>(APM::Pose::IkChainCount) &&
        APM::Pose::PoseOverrides::GetIkChain(static_cast<APM::Pose::IkChain>(chain)).followBody)
    {
        const auto body = APM::Pose::PoseProcessor::GetIkBodyRotation(static_cast<size_t>(chain));
        result = APM::Math::Rotate(APM::Math::Conjugate(body), result);
    }
    Return(aOut, RED4ext::Vector4{result.x, result.y, result.z, 0.f});
}

// Moves the IK target of a chain by a model-space vector (meters, X right, Y forward, Z up) and pins the chain.
void GizmoMoveIk(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t chain = 0;
    RED4ext::Vector4 delta{};
    RED4ext::GetParameter(aFrame, &chain);
    RED4ext::GetParameter(aFrame, &delta);
    aFrame->code++;

    if (chain >= 0 && chain < static_cast<int32_t>(APM::Pose::IkChainCount))
    {
        const APM::Math::Vec3 model{delta.X, delta.Y, delta.Z};
        auto settings = APM::Pose::PoseOverrides::GetIkChain(static_cast<APM::Pose::IkChain>(chain));
        settings.enabled = true;
        settings.offset[0] = std::clamp(settings.offset[0] + model.x, -0.8f, 0.8f);
        settings.offset[1] = std::clamp(settings.offset[1] + model.y, -0.8f, 0.8f);
        settings.offset[2] = std::clamp(settings.offset[2] + model.z, -0.8f, 0.8f);
        APM::Pose::PoseOverrides::SetIkChain(static_cast<APM::Pose::IkChain>(chain), settings);
        APM::Ui::PhotoModeUi::RequestRefresh();
    }
    Return(aOut, true);
}

// --- Photo Mode menu (see scripts/AdvancedPoseMod/PhotoModeUi.reds) ----------------------------------------------

void UiIsOwnAttribute(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsOwnAttribute(attribute));
}

void UiSetValue(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    uint32_t attribute = 0;
    float value = 0.f;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetValue(attribute, value);
}

void UiGetValue(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetValue(attribute));
}

void UiIsAttributeEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsAttributeEnabled(attribute));
}

void UiConsumeRefresh(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::ConsumeRefresh());
}

void UiRequestRefresh(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Ui::PhotoModeUi::RequestRefresh();
}

void UiGetOptionCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetOptionCount(attribute));
}

void UiGetOptionLabel(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    Return(aOut, CString(APM::Ui::PhotoModeUi::GetOptionLabel(attribute, index).c_str()));
}

void UiGetOptionGroup(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    Return(aOut, CString(APM::Ui::PhotoModeUi::GetOptionGroup(attribute, index).c_str()));
}

void UiGetSlotCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetSlotCount());
}

void UiIsSlotSaved(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t slot = 0;
    RED4ext::GetParameter(aFrame, &slot);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsSlotSaved(slot));
}

// --- Registration ------------------------------------------------------------------------------------------------

struct Param
{
    const char* type;
    const char* name;
};

template<typename TFunc>
void Register(const char* aName, TFunc aFunc, const char* aReturnType, std::initializer_list<Param> aParams)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* func = RED4ext::CGlobalFunction::Create(aName, aName, aFunc);
    RED4ext::CBaseFunction::Flags flags{};
    flags.isNative = true;
    flags.isStatic = true;
    func->flags = flags;
    for (const auto& param : aParams)
    {
        func->AddParam(param.type, param.name);
    }
    if (aReturnType)
    {
        func->SetReturnType(aReturnType);
    }
    rtti->RegisterFunction(func);
}
} // namespace

namespace APM::ScriptApi
{
void Register()
{
    ::Register("APM_IsPhotoModeActive", &IsPhotoModeActive, "Bool", {});
    ::Register("APM_GetBoneCount", &GetBoneCount, "Int32", {});
    ::Register("APM_GetBoneName", &GetBoneName, "CName", {{"Int32", "index"}});

    ::Register("APM_SetBoneRotation", &SetBoneRotation, "Bool",
               {{"CName", "bone"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}});
    ::Register("APM_SetBoneTranslation", &SetBoneTranslation, "Bool",
               {{"CName", "bone"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}});
    ::Register("APM_GetBoneRotation", &GetBoneRotation, "Vector4", {{"CName", "bone"}});
    ::Register("APM_GetBoneTranslation", &GetBoneTranslation, "Vector4", {{"CName", "bone"}});
    ::Register("APM_ResetBone", &ResetBone, nullptr, {{"CName", "bone"}});
    ::Register("APM_ResetAll", &ResetAll, nullptr, {});

    ::Register("APM_SetIkChain", &SetIkChain, "Bool",
               {{"CName", "chain"}, {"Bool", "enabled"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}, {"Float", "swivel"}});
    ::Register("APM_GetIkChain", &GetIkChain, "Vector4", {{"CName", "chain"}});
    ::Register("APM_IsIkChainEnabled", &IsIkChainEnabled, "Bool", {{"CName", "chain"}});

    ::Register("APM_SetEnabled", &SetEnabled, nullptr, {{"Bool", "enabled"}});
    ::Register("APM_IsEnabled", &IsEnabled, "Bool", {});

    ::Register("APM_SavePose", &SavePose, "Bool", {{"String", "name"}});
    ::Register("APM_LoadPose", &LoadPose, "Bool", {{"String", "name"}});
    ::Register("APM_GetPoseCount", &GetPoseCount, "Int32", {});
    ::Register("APM_GetPoseName", &GetPoseName, "String", {{"Int32", "index"}});
    ::Register("APM_DeletePose", &DeletePose, "Bool", {{"String", "name"}});
    ::Register("APM_SaveNewPose", &SaveNewPose, "String", {});
    ::Register("APM_RenamePose", &RenamePose, "String", {{"String", "oldName"}, {"String", "newName"}});
    ::Register("APM_GetBasePoseName", &GetBasePoseName, "String", {});
    ::Register("APM_PropRecordCount", &PropRecordCount, "Int32", {{"Int32", "kind"}});
    ::Register("APM_PropRecordAt", &PropRecordAt, "Uint64", {{"Int32", "kind"}, {"Int32", "index"}});
    ::Register("APM_RecordFromNumber", &RecordFromNumber, "TweakDBID", {{"Uint64", "number"}});
    ::Register("APM_SetHandProp", &SetHandProp, nullptr, {{"Bool", "rightHand"}, {"Uint64", "record"}});
    ::Register("APM_GetHandProp", &GetHandProp, "Uint64", {{"Bool", "rightHand"}});
    ::Register("APM_HistoryCheckpoint", &HistoryCheckpoint, nullptr, {});
    ::Register("APM_Undo", &HistoryUndo, "Bool", {});
    ::Register("APM_Redo", &HistoryRedo, "Bool", {});
    ::Register("APM_CanUndo", &HistoryCanUndo, "Bool", {});
    ::Register("APM_CanRedo", &HistoryCanRedo, "Bool", {});
    ::Register("APM_PollHotkey", &PollHotkey, "Int32", {});
    ::Register("APM_SessionGet", &SessionGet, "Float", {{"String", "key"}, {"Float", "fallback"}});
    ::Register("APM_SessionSet", &SessionSet, nullptr, {{"String", "key"}, {"Float", "value"}});
    ::Register("APM_IsAltDown", &IsAltDown, "Bool", {});
    ::Register("APM_MirrorParts", &MirrorParts, "Int32", {{"Bool", "fromLeft"}, {"String", "group"}});
    ::Register("APM_ApplyHandPreset", &ApplyHandPreset, "Bool", {{"Bool", "rightHand"}, {"Int32", "preset"}});
    ::Register("APM_SetMirror", &SetMirror, nullptr, {{"Bool", "mirror"}});
    ::Register("APM_IsMirror", &IsMirror, "Bool", {});
    ::Register("APM_SetLimits", &SetLimits, nullptr, {{"Bool", "limits"}});
    ::Register("APM_AreLimitsOn", &AreLimitsOn, "Bool", {});
    ::Register("APM_SetNeutralPose", &SetNeutralPose, "Bool", {{"Bool", "armsDown"}});
    ::Register("APM_GizmoAimPart", &GizmoAimPart, "Bool", {{"Int32", "part"}, {"Vector4", "modelTarget"}});
    ::Register("APM_SetCameraModel", &SetCameraModel, nullptr,
               {{"Vector4", "position"}, {"Vector4", "forward"}, {"Vector4", "up"}});
    ::Register("APM_FaceControlCount", &FaceControlCount, "Int32", {});
    ::Register("APM_FaceControlLabel", &FaceControlLabel, "String", {{"Int32", "control"}});
    ::Register("APM_FaceControlZone", &FaceControlZone, "String", {{"Int32", "control"}});
    ::Register("APM_FaceControlIsAngle", &FaceControlIsAngle, "Bool", {{"Int32", "control"}});
    ::Register("APM_FaceIsEdited", &FaceIsEdited, "Bool", {{"Int32", "control"}});
    ::Register("APM_FaceResetControl", &FaceResetControl, nullptr, {{"Int32", "control"}});
    ::Register("APM_FaceReset", &FaceReset, nullptr, {});
    ::Register("APM_FaceUnload", &FaceUnload, nullptr, {});
    ::Register("APM_FaceGetLoadedName", &FaceGetLoadedName, "String", {});
    ::Register("APM_FaceReloadList", &FaceReloadList, nullptr, {});
    ::Register("APM_FaceListCount", &FaceListCount, "Int32", {});
    ::Register("APM_FaceListName", &FaceListName, "String", {{"Int32", "index"}});
    ::Register("APM_FaceSave", &FaceSave, "Bool", {{"String", "name"}});
    ::Register("APM_FaceSaveNew", &FaceSaveNew, "String", {});
    ::Register("APM_FaceLoad", &FaceLoad, "Bool", {{"String", "name"}});
    ::Register("APM_FaceDelete", &FaceDelete, "Bool", {{"String", "name"}});
    ::Register("APM_IsReady", &IsReady, "Bool", {});
    ::Register("APM_OpenFolder", &OpenFolder, "Bool", {{"Int32", "kind"}});
    ::Register("APM_SetWheelCapture", &SetWheelCapture, nullptr, {{"Bool", "capture"}});
    ::Register("APM_ConsumeWheel", &ConsumeWheel, "Int32", {});
    ::Register("APM_TextEntryBegin", &TextEntryBegin, "Bool", {{"String", "text"}});
    ::Register("APM_TextEntryEnd", &TextEntryEnd, nullptr, {});
    ::Register("APM_TextEntryGetState", &TextEntryGetState, "Int32", {});
    ::Register("APM_TextEntryGetText", &TextEntryGetText, "String", {});

    ::Register("APM_LogInfo", &LogInfo, "Bool", {{"String", "text"}});
    ::Register("APM_PrefGet", &PrefGet, "Float", {{"String", "key"}, {"Float", "fallback"}});
    ::Register("APM_PrefSet", &PrefSet, "Bool", {{"String", "key"}, {"Float", "value"}});

    ::Register("APM_GizmoGetPartPosition", &GizmoGetPartPosition, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartMarker", &GizmoGetPartMarker, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartTip", &GizmoGetPartTip, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartIkChain", &GizmoGetPartIkChain, "Int32", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartSwivelChain", &GizmoGetPartSwivelChain, "Int32", {{"Int32", "part"}});
    ::Register("APM_GizmoIsIkActive", &GizmoIsIkActive, "Bool", {{"Int32", "chain"}});
    ::Register("APM_GizmoGetChainJoint", &GizmoGetChainJoint, "Vector4", {{"Int32", "chain"}, {"Int32", "joint"}});
    ::Register("APM_GizmoIsPartLocked", &GizmoIsPartLocked, "Bool", {{"Int32", "part"}});
    ::Register("APM_GizmoIsPartDetail", &GizmoIsPartDetail, "Bool", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartForward", &GizmoGetPartForward, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartParent", &GizmoGetPartParent, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoIkModelToOffset", &GizmoIkModelToOffset, "Vector4", {{"Int32", "chain"}, {"Vector4", "modelDelta"}});
    ::Register("APM_GizmoMovePart", &GizmoMovePart, "Bool", {{"Int32", "part"}, {"Vector4", "modelDelta"}});
    ::Register("APM_GizmoCanMovePart", &GizmoCanMovePart, "Bool", {{"Int32", "part"}});
    ::Register("APM_GizmoMoveIk", &GizmoMoveIk, "Bool", {{"Int32", "chain"}, {"Vector4", "modelDelta"}});
    ::Register("APM_GizmoGetEntity", &GizmoGetEntity, "handle:entEntity", {});
    ::Register("APM_GizmoRotatePart", &GizmoRotatePart, "Bool",
               {{"Int32", "part"}, {"Vector4", "modelAxis"}, {"Float", "degrees"}});
    ::Register("APM_GizmoGetComponent", &GizmoGetComponent, "handle:entIPlacedComponent", {});

    ::Register("APM_UiIsOwnAttribute", &UiIsOwnAttribute, "Bool", {{"Uint32", "attribute"}});
    ::Register("APM_UiSetValue", &UiSetValue, nullptr, {{"Uint32", "attribute"}, {"Float", "value"}});
    ::Register("APM_UiGetValue", &UiGetValue, "Float", {{"Uint32", "attribute"}});
    ::Register("APM_UiIsAttributeEnabled", &UiIsAttributeEnabled, "Bool", {{"Uint32", "attribute"}});
    ::Register("APM_UiConsumeRefresh", &UiConsumeRefresh, "Bool", {});
    ::Register("APM_UiRequestRefresh", &UiRequestRefresh, nullptr, {});
    ::Register("APM_UiGetOptionCount", &UiGetOptionCount, "Int32", {{"Uint32", "attribute"}});
    ::Register("APM_UiGetOptionLabel", &UiGetOptionLabel, "String", {{"Uint32", "attribute"}, {"Int32", "index"}});
    ::Register("APM_UiGetOptionGroup", &UiGetOptionGroup, "String", {{"Uint32", "attribute"}, {"Int32", "index"}});
    ::Register("APM_UiGetSlotCount", &UiGetSlotCount, "Int32", {});
    ::Register("APM_UiIsSlotSaved", &UiIsSlotSaved, "Bool", {{"Int32", "slot"}});

    Log::Info("Script API registered");
}
} // namespace APM::ScriptApi
