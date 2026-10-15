#include "Pose/FaceRig.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"
#include "Pose/Characters.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;
using APM::Pose::FaceControlCount;

enum class Kind
{
    Move,  // a point of the face and its neighbours (weights fall off with the distance)
    Shift, // the bones themselves move, with what hangs from them
    Turn,  // the bones turn, with what hangs from them
};

enum class Axis
{
    Up,
    Forward,
    Outward, // away from the middle of the face
    Inward,
    Side,    // the rig's +X (V's right)
    TurnDown,  // turn: the front of the bone goes down (jaw opens, upper eyelid closes)
    TurnUp,    // turn: the front of the bone goes up (lower eyelid rises)
};

struct ControlDef
{
    const char* label;
    const char* zone;
    Kind kind;
    Axis axis;
    std::array<const char*, 4> bones; // anchors (Move) or the bones moved / turned
    float radius;                     // centimeters (Move)
    const char* area;                 // Move: which face bones can follow ("mouth", "brows", "cheeks", "nose")
    int32_t twin;
};

// clang-format off
constexpr ControlDef Controls[FaceControlCount] = {
    {"Open", "Jaw", Kind::Turn, Axis::TurnDown, {"jaw_root_JNT"}, 0.f, "", -1},
    {"Forward / back", "Jaw", Kind::Shift, Axis::Forward, {"jaw_root_JNT"}, 0.f, "", -1},
    {"Sideways", "Jaw", Kind::Shift, Axis::Side, {"jaw_root_JNT"}, 0.f, "", -1},
    {"Left corner up / down", "Mouth", Kind::Move, Axis::Up, {"l_J_mug_lip_corner_0_JNT"}, 1.8f, "mouth", 4},
    {"Right corner up / down", "Mouth", Kind::Move, Axis::Up, {"r_J_mug_lip_corner_0_JNT"}, 1.8f, "mouth", 3},
    {"Left corner wide / narrow", "Mouth", Kind::Move, Axis::Outward, {"l_J_mug_lip_corner_0_JNT"}, 1.8f, "mouth", 6},
    {"Right corner wide / narrow", "Mouth", Kind::Move, Axis::Outward, {"r_J_mug_lip_corner_0_JNT"}, 1.8f, "mouth", 5},
    {"Upper lip up / down", "Mouth", Kind::Move, Axis::Up, {"l_J_mug_lip_up_0_JNT", "r_J_mug_lip_up_0_JNT"}, 1.6f, "mouth", -1},
    {"Lower lip up / down", "Mouth", Kind::Move, Axis::Up, {"l_J_mug_lip_dn_0_JNT", "r_J_mug_lip_dn_0_JNT"}, 1.6f, "mouth", -1},
    {"Lips forward", "Mouth", Kind::Move, Axis::Forward, {"l_J_mug_lip_up_0_JNT", "r_J_mug_lip_up_0_JNT", "l_J_mug_lip_dn_0_JNT", "r_J_mug_lip_dn_0_JNT"}, 1.8f, "mouth", -1},
    {"Left inner up / down", "Brows", Kind::Move, Axis::Up, {"l_J_eye_brows_rowA_0_JNT", "l_J_eye_brows_rowB_0_JNT"}, 1.6f, "brows", 11},
    {"Right inner up / down", "Brows", Kind::Move, Axis::Up, {"r_J_eye_brows_rowA_0_JNT", "r_J_eye_brows_rowB_0_JNT"}, 1.6f, "brows", 10},
    {"Left outer up / down", "Brows", Kind::Move, Axis::Up, {"l_J_eye_brows_rowA_3_JNT", "l_J_eye_brows_rowB_3_JNT"}, 1.6f, "brows", 13},
    {"Right outer up / down", "Brows", Kind::Move, Axis::Up, {"r_J_eye_brows_rowA_3_JNT", "r_J_eye_brows_rowB_3_JNT"}, 1.6f, "brows", 12},
    {"Frown", "Brows", Kind::Move, Axis::Inward, {"l_J_eye_brows_rowA_0_JNT", "r_J_eye_brows_rowA_0_JNT", "l_J_eye_brows_rowB_0_JNT", "r_J_eye_brows_rowB_0_JNT"}, 1.8f, "brows", -1},
    {"Left upper close / open", "Eyelids", Kind::Turn, Axis::TurnDown, {"l_J_eye_lid_up_root_1_JNT", "l_J_eye_lid_up_root_2_JNT"}, 0.f, "", 16},
    {"Right upper close / open", "Eyelids", Kind::Turn, Axis::TurnDown, {"r_J_eye_lid_up_root_1_JNT", "r_J_eye_lid_up_root_2_JNT"}, 0.f, "", 15},
    {"Left lower up / down", "Eyelids", Kind::Turn, Axis::TurnUp, {"l_J_eye_lid_dn_root_1_JNT", "l_J_eye_lid_dn_root_2_JNT"}, 0.f, "", 18},
    {"Right lower up / down", "Eyelids", Kind::Turn, Axis::TurnUp, {"r_J_eye_lid_dn_root_1_JNT", "r_J_eye_lid_dn_root_2_JNT"}, 0.f, "", 17},
    {"Left cheek up / down", "Cheeks", Kind::Move, Axis::Up, {"l_J_eye_check_rowA_0_JNT", "l_J_eye_check_rowA_1_JNT"}, 2.2f, "cheeks", 20},
    {"Right cheek up / down", "Cheeks", Kind::Move, Axis::Up, {"r_J_eye_check_rowA_0_JNT", "r_J_eye_check_rowA_1_JNT"}, 2.2f, "cheeks", 19},
    {"Nose wrinkle", "Cheeks", Kind::Move, Axis::Up, {"l_J_mug_nose_nostril_0_JNT", "r_J_mug_nose_nostril_0_JNT"}, 1.0f, "nose", -1},
    {"Chin up / down", "Cheeks", Kind::Move, Axis::Up, {"mid_J_mug_chin_rowA_0_JNT"}, 1.8f, "mouth", -1},
};
// clang-format on

uint64_t Hash(const char* aName)
{
    return RED4ext::CName(aName).hash;
}

std::string Lower(const char* aText)
{
    std::string text = aText ? aText : "";
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool InArea(const std::string& aName, const char* aArea)
{
    const auto has = [&](const char* aPart) { return aName.find(aPart) != std::string::npos; };
    const std::string area = aArea;
    if (area == "mouth")
    {
        return (has("mug_") || has("nosabial")) && !has("mug_nose");
    }
    if (area == "brows")
    {
        return has("brows") || has("eye_nose");
    }
    if (area == "cheeks")
    {
        return has("check") || has("nosabial");
    }
    if (area == "nose")
    {
        return has("mug_nose");
    }
    return false;
}

// What a rig needs, worked out once from its reference pose.
struct RigData
{
    uintptr_t rig = 0;
    bool valid = false;
    int32_t head = -1;
    std::vector<Vec3> refPositions; // model space
    std::vector<Quat> refRotations; // model space
    std::vector<bool> face;         // part of the expression (below face_root_JNT / jaw_root_JNT)
    struct Weighted
    {
        int32_t bone;
        float weight;
        Vec3 axis; // reference model space
    };
    std::array<std::vector<Weighted>, FaceControlCount> moved; // Move: followers; Shift / Turn: the bones
    std::array<std::vector<int32_t>, FaceControlCount> anchors;
};

std::mutex g_mutex;
RigData g_rig;
std::array<float, FaceControlCount> g_measure{};
bool g_measured = false;
std::vector<std::pair<uint64_t, RED4ext::QsTransform>> g_lastFace; // final face locals, for saving
ULONGLONG g_lastFaceTime = 0;
bool g_lastFaceEdited = false;

Vec3 AxisFor(Axis aAxis, const Vec3& aPosition)
{
    const float side = aPosition.x < 0.f ? -1.f : 1.f;
    switch (aAxis)
    {
    case Axis::Up:
        return {0.f, 0.f, 1.f};
    case Axis::Forward:
        return {0.f, 1.f, 0.f};
    case Axis::Outward:
        return {side, 0.f, 0.f};
    case Axis::Inward:
        return {-side, 0.f, 0.f};
    case Axis::Side:
        return {1.f, 0.f, 0.f};
    case Axis::TurnDown:
        return {-1.f, 0.f, 0.f}; // the rig looks along +Y: turning around -X brings the front down
    case Axis::TurnUp:
        return {1.f, 0.f, 0.f};
    }
    return {};
}

// Must be called with g_mutex held.
const RigData& RigLocked(const APM::Pose::Target& aTarget)
{
    if (g_rig.rig == aTarget.rig && g_rig.valid)
    {
        return g_rig;
    }
    RigData data;
    data.rig = aTarget.rig;
    const size_t count = std::min({aTarget.boneNames.size(), aTarget.parents.size(), aTarget.refRotationsLS.size(),
                                   aTarget.refTranslationsLS.size()});
    data.refPositions.resize(count);
    data.refRotations.resize(count);
    data.face.assign(count, false);
    const auto indexOf = [&](uint64_t aHash) -> int32_t
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (aTarget.boneNames[i] == aHash)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    };
    const int32_t faceRoot = indexOf(Hash("face_root_JNT"));
    const int32_t jawRoot = indexOf(Hash("jaw_root_JNT"));
    data.head = indexOf(Hash("Head"));
    for (size_t i = 0; i < count; ++i)
    {
        const int16_t parent = aTarget.parents[i];
        if (parent >= 0 && static_cast<size_t>(parent) < i)
        {
            data.refPositions[i] = data.refPositions[parent] + APM::Math::Rotate(data.refRotations[parent], aTarget.refTranslationsLS[i]);
            data.refRotations[i] = APM::Math::Normalize(APM::Math::Mul(data.refRotations[parent], aTarget.refRotationsLS[i]));
            data.face[i] = data.face[parent];
        }
        else
        {
            data.refPositions[i] = aTarget.refTranslationsLS[i];
            data.refRotations[i] = aTarget.refRotationsLS[i];
        }
        if (static_cast<int32_t>(i) == faceRoot || static_cast<int32_t>(i) == jawRoot)
        {
            data.face[i] = true;
        }
    }
    if (data.head < 0 || faceRoot < 0)
    {
        g_rig = data;
        return g_rig;
    }
    // Positions relative to the head (the face's own frame in the reference pose).
    const Vec3 head = data.refPositions[data.head];

    std::vector<std::string> names(count);
    for (size_t i = 0; i < count; ++i)
    {
        names[i] = Lower(RED4ext::CName(aTarget.boneNames[i]).ToString());
    }
    for (size_t c = 0; c < FaceControlCount; ++c)
    {
        const auto& def = Controls[c];
        for (const char* bone : def.bones)
        {
            if (!bone)
            {
                continue;
            }
            const int32_t index = indexOf(Hash(bone));
            if (index >= 0)
            {
                data.anchors[c].push_back(index);
            }
        }
        if (def.kind != Kind::Move)
        {
            for (const int32_t index : data.anchors[c])
            {
                data.moved[c].push_back({index, 1.f, AxisFor(def.axis, data.refPositions[index] - head)});
            }
            continue;
        }
        const float radius = def.radius / 100.f;
        for (size_t i = 0; i < count; ++i)
        {
            if (!data.face[i] || !InArea(names[i], def.area))
            {
                continue;
            }
            float weight = 0.f;
            for (const int32_t anchor : data.anchors[c])
            {
                const float d = APM::Math::Length(data.refPositions[i] - data.refPositions[anchor]) / radius;
                if (d < 1.f)
                {
                    const float f = 1.f - d * d;
                    weight = std::max(weight, f * f);
                }
            }
            if (weight > 0.01f)
            {
                data.moved[c].push_back({static_cast<int32_t>(i), weight, AxisFor(def.axis, data.refPositions[i] - head)});
            }
        }
    }
    data.valid = true;
    g_rig = std::move(data);
    APM::Log::Verbose("Face rig ready (%zu bones)", count);
    return g_rig;
}

struct FacePose
{
    std::vector<Vec3> positions;
    std::vector<Quat> rotations;
};

Quat Rotation(const RED4ext::QsTransform& aBone)
{
    return {aBone.Rotation.i, aBone.Rotation.j, aBone.Rotation.k, aBone.Rotation.r};
}

void SetRotation(RED4ext::QsTransform& aBone, const Quat& aRotation)
{
    const Quat q = APM::Math::Normalize(aRotation);
    aBone.Rotation.i = q.i;
    aBone.Rotation.j = q.j;
    aBone.Rotation.k = q.k;
    aBone.Rotation.r = q.r;
}

void ComputePose(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, FacePose& aOut)
{
    const size_t count = std::min<size_t>(aCount, aTarget.parents.size());
    aOut.positions.resize(count);
    aOut.rotations.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        const Vec3 local{aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
        const int16_t parent = aTarget.parents[i];
        if (parent >= 0 && static_cast<size_t>(parent) < i)
        {
            aOut.positions[i] = aOut.positions[parent] + APM::Math::Rotate(aOut.rotations[parent], local);
            aOut.rotations[i] = APM::Math::Normalize(APM::Math::Mul(aOut.rotations[parent], Rotation(aBones[i])));
        }
        else
        {
            aOut.positions[i] = local;
            aOut.rotations[i] = Rotation(aBones[i]);
        }
    }
}

// Reference-frame position of a bone of the current pose (the head brought back to where it is in the rig).
Vec3 InReference(const RigData& aRig, const FacePose& aPose, int32_t aBone)
{
    const Quat back = APM::Math::Mul(aRig.refRotations[aRig.head], APM::Math::Conjugate(aPose.rotations[aRig.head]));
    return aRig.refPositions[aRig.head] + APM::Math::Rotate(back, aPose.positions[aBone] - aPose.positions[aRig.head]);
}

float TwistDegrees(const Quat& aRotation, const Vec3& aAxis)
{
    const Vec3 v{aRotation.i, aRotation.j, aRotation.k};
    float angle = 2.f * std::atan2(APM::Math::Dot(v, aAxis), aRotation.r) * 57.29578f;
    if (angle > 180.f)
    {
        angle -= 360.f;
    }
    if (angle < -180.f)
    {
        angle += 360.f;
    }
    return angle;
}

// Must be called with g_mutex held.
void MeasureLocked(const RigData& aRig, const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, const FacePose& aPose)
{
    std::array<float, FaceControlCount> measure{};
    for (size_t c = 0; c < FaceControlCount; ++c)
    {
        const auto& def = Controls[c];
        if (aRig.anchors[c].empty())
        {
            continue;
        }
        if (def.kind == Kind::Turn)
        {
            const int32_t bone = aRig.anchors[c].front();
            const Vec3 axisRef = AxisFor(def.axis, {});
            const Vec3 axisLocal = APM::Math::Rotate(APM::Math::Conjugate(aRig.refRotations[bone]), axisRef);
            const Quat change = APM::Math::Mul(APM::Math::Conjugate(aTarget.refRotationsLS[bone]), Rotation(aBones[bone]));
            measure[c] = TwistDegrees(change, axisLocal);
            continue;
        }
        float sum = 0.f;
        for (const int32_t anchor : aRig.anchors[c])
        {
            const Vec3 offset = InReference(aRig, aPose, anchor) - aRig.refPositions[anchor];
            const Vec3 axis = AxisFor(def.axis, aRig.refPositions[anchor] - aRig.refPositions[aRig.head]);
            sum += APM::Math::Dot(offset, axis);
        }
        measure[c] = sum / static_cast<float>(aRig.anchors[c].size()) * 100.f;
    }
    for (size_t c = 0; c < FaceControlCount; ++c)
    {
        // Smoothed, so that an expression that moves a little does not make the values flicker.
        g_measure[c] = g_measured ? g_measure[c] + (measure[c] - g_measure[c]) * 0.2f : measure[c];
    }
    g_measured = true;
}

void ApplyBase(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const APM::Pose::BasePose& aBase)
{
    std::unordered_map<uint64_t, const APM::Pose::BasePoseBone*> byHash;
    for (const auto& bone : aBase.bones)
    {
        byHash.emplace(bone.boneHash, &bone);
    }
    const uint32_t count = std::min<uint32_t>(aCount, static_cast<uint32_t>(aTarget.boneNames.size()));
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto it = byHash.find(aTarget.boneNames[i]);
        if (it == byHash.end())
        {
            continue;
        }
        SetRotation(aBones[i], it->second->rotation);
        if (it->second->hasTranslation)
        {
            aBones[i].Translation.X = it->second->translation[0];
            aBones[i].Translation.Y = it->second->translation[1];
            aBones[i].Translation.Z = it->second->translation[2];
        }
    }
}

// Must be called with g_mutex held.
void RememberFaceLocked(const RigData& aRig, const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones,
                        uint32_t aCount, bool aEdited)
{
    g_lastFace.clear();
    const uint32_t count = std::min<uint32_t>(aCount, static_cast<uint32_t>(aRig.face.size()));
    for (uint32_t i = 0; i < count; ++i)
    {
        if (aRig.face[i])
        {
            g_lastFace.emplace_back(aTarget.boneNames[i], aBones[i]);
        }
    }
    g_lastFaceTime = GetTickCount64();
    g_lastFaceEdited = aEdited;
}

std::string SanitizeName(const std::string& aName)
{
    std::string out;
    for (const char ch : aName)
    {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '_' || ch == '-' || ch == ' ';
        out.push_back(ok ? ch : '_');
        if (out.size() >= 64)
        {
            break;
        }
    }
    while (!out.empty() && out.back() == ' ')
    {
        out.pop_back();
    }
    return out;
}

} // namespace

namespace APM::Pose::FaceRig
{
size_t ControlCount()
{
    return FaceControlCount;
}

const char* ControlLabel(size_t aControl)
{
    return aControl < FaceControlCount ? Controls[aControl].label : "";
}

const char* ControlZone(size_t aControl)
{
    return aControl < FaceControlCount ? Controls[aControl].zone : "";
}

bool ControlIsAngle(size_t aControl)
{
    return aControl < FaceControlCount && Controls[aControl].kind == Kind::Turn;
}

int32_t ControlTwin(size_t aControl)
{
    return aControl < FaceControlCount ? Controls[aControl].twin : -1;
}

float GetMeasure(size_t aControl)
{
    std::scoped_lock lock(g_mutex);
    return aControl < FaceControlCount ? g_measure[aControl] : 0.f;
}

void Observe(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose)
{
    thread_local std::vector<RED4ext::QsTransform> face;
    thread_local FacePose pose;
    std::scoped_lock lock(g_mutex);
    const auto& rig = RigLocked(aTarget);
    if (!rig.valid)
    {
        return;
    }
    face.assign(aBones, aBones + aCount);
    if (aPose.faceBase && PoseOverrides::IsEnabled())
    {
        ApplyBase(aTarget, face.data(), aCount, *aPose.faceBase);
    }
    ComputePose(aTarget, face.data(), aCount, pose);
    if (pose.positions.size() < rig.refPositions.size())
    {
        return;
    }
    MeasureLocked(rig, aTarget, face.data(), pose);
    // Without edits this is also the final face (saving needs it).
    if (GetTickCount64() - g_lastFaceTime > 500 || !g_lastFaceEdited)
    {
        RememberFaceLocked(rig, aTarget, face.data(), aCount, false);
    }
}

void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose)
{
    const bool edited = AnyFaceEdit(aPose.faceEdits);
    if (!aPose.faceBase && !edited)
    {
        return;
    }
    thread_local FacePose pose;
    std::scoped_lock lock(g_mutex);
    const auto& rig = RigLocked(aTarget);
    if (!rig.valid)
    {
        return;
    }
    if (aPose.faceBase)
    {
        ApplyBase(aTarget, aBones, aCount, *aPose.faceBase);
    }
    if (edited)
    {
        ComputePose(aTarget, aBones, aCount, pose);
        if (pose.positions.size() < rig.refPositions.size())
        {
            return;
        }
        // Reference face frame -> current model space (the head turns the whole face).
        Quat toCurrent = APM::Math::Mul(pose.rotations[rig.head], APM::Math::Conjugate(rig.refRotations[rig.head]));

        // Turns first (jaw, eyelids): what hangs from them follows.
        bool turned = false;
        for (size_t c = 0; c < FaceControlCount; ++c)
        {
            if (Controls[c].kind != Kind::Turn || aPose.faceEdits[c] == 0.f)
            {
                continue;
            }
            for (const auto& entry : rig.moved[c])
            {
                const Vec3 axisModel = APM::Math::Rotate(toCurrent, entry.axis);
                const Vec3 axisLocal = APM::Math::Rotate(APM::Math::Conjugate(pose.rotations[entry.bone]), axisModel);
                const Quat turn = APM::Math::FromAxisAngle(axisLocal, APM::Math::DegToRad(aPose.faceEdits[c]));
                SetRotation(aBones[entry.bone], APM::Math::Mul(Rotation(aBones[entry.bone]), turn));
                turned = true;
            }
        }
        if (turned)
        {
            ComputePose(aTarget, aBones, aCount, pose);
            toCurrent = APM::Math::Mul(pose.rotations[rig.head], APM::Math::Conjugate(rig.refRotations[rig.head]));
        }

        // Moves and shifts: model-space offsets per bone, then into each bone's parent space (minus what the parent
        // already moves, so a child is not moved twice).
        thread_local std::vector<Vec3> offsets;
        thread_local std::vector<bool> hasOffset;
        offsets.assign(pose.positions.size(), Vec3{});
        hasOffset.assign(pose.positions.size(), false);
        for (size_t c = 0; c < FaceControlCount; ++c)
        {
            if (Controls[c].kind == Kind::Turn || aPose.faceEdits[c] == 0.f)
            {
                continue;
            }
            const float meters = aPose.faceEdits[c] / 100.f;
            for (const auto& entry : rig.moved[c])
            {
                offsets[entry.bone] = offsets[entry.bone] + APM::Math::Rotate(toCurrent, entry.axis * (meters * entry.weight));
                hasOffset[entry.bone] = true;
            }
        }
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            if (!hasOffset[i])
            {
                continue;
            }
            const int16_t parent = aTarget.parents[i];
            if (parent < 0)
            {
                continue;
            }
            Vec3 own = offsets[i];
            if (hasOffset[parent])
            {
                own = own - offsets[parent]; // the parent's move already carries this bone along
            }
            const Vec3 local = APM::Math::Rotate(APM::Math::Conjugate(pose.rotations[parent]), own);
            aBones[i].Translation.X += local.x;
            aBones[i].Translation.Y += local.y;
            aBones[i].Translation.Z += local.z;
        }
    }
    if (aTarget.photoModeSlot == APM::Pose::Characters::Selected())
    {
        RememberFaceLocked(rig, aTarget, aBones, aCount, true); // "save expression" takes the selected character's face
    }
}

std::filesystem::path GetDirectory()
{
    return GetRuntime().pluginDir / L"expressions";
}

std::vector<std::string> ListSaved()
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(GetDirectory(), ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Save(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    if (name.empty())
    {
        return false;
    }
    nlohmann::json root;
    root["format"] = "AdvancedPoseMod.Expression";
    root["version"] = 1;
    root["name"] = name;
    auto& bones = root["bones"] = nlohmann::json::object();
    {
        std::scoped_lock lock(g_mutex);
        if (g_lastFace.empty())
        {
            Log::Warn("SaveExpression: the face is not available");
            return false;
        }
        for (const auto& [hash, bone] : g_lastFace)
        {
            const char* boneName = RED4ext::CName(hash).ToString();
            if (!boneName)
            {
                continue;
            }
            bones[boneName] = {{"r", {bone.Rotation.i, bone.Rotation.j, bone.Rotation.k, bone.Rotation.r}},
                               {"t", {bone.Translation.X, bone.Translation.Y, bone.Translation.Z}}};
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(GetDirectory(), ec);
    const auto path = GetDirectory() / (name + ".json");
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        Log::Error("SaveExpression: cannot write %s", path.string().c_str());
        return false;
    }
    out << root.dump(1);
    Log::Info("Expression saved: %s (%zu bones)", name.c_str(), bones.size());
    return true;
}

std::string SaveNew()
{
    for (int i = 1; i < 1000; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "Expression %03d", i);
        std::error_code ec;
        if (!std::filesystem::exists(GetDirectory() / (std::string(name) + ".json"), ec))
        {
            return Save(name) ? std::string(name) : std::string();
        }
    }
    return {};
}

bool Load(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    std::ifstream in(GetDirectory() / (name + ".json"));
    if (!in)
    {
        return false;
    }
    auto base = std::make_shared<BasePose>();
    base->name = name;
    try
    {
        const auto root = nlohmann::json::parse(in);
        for (const auto& [boneName, entry] : root.at("bones").items())
        {
            BasePoseBone bone;
            bone.boneHash = RED4ext::CName(boneName.c_str()).hash;
            const auto r = entry.at("r").get<std::array<float, 4>>();
            const auto t = entry.at("t").get<std::array<float, 3>>();
            bone.rotation = Math::Normalize(Math::Quat{r[0], r[1], r[2], r[3]});
            bone.translation = t;
            bone.hasTranslation = true;
            base->bones.push_back(bone);
        }
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadExpression: %s: %s", name.c_str(), e.what());
        return false;
    }
    PoseOverrides::SetFaceBase(std::move(base));
    Log::Info("Expression loaded: %s", name.c_str());
    return true;
}

bool Delete(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    std::error_code ec;
    const bool removed = std::filesystem::remove(GetDirectory() / (name + ".json"), ec);
    if (removed && PoseOverrides::GetFaceBaseName() == name)
    {
        PoseOverrides::SetFaceBase(nullptr);
    }
    return removed;
}
} // namespace APM::Pose::FaceRig
