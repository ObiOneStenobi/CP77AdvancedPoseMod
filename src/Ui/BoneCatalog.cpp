#include "Ui/BoneCatalog.hpp"

#include "Core/Log.hpp"
#include "Game/MeshBones.hpp"
#include "Pose/Characters.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PhysicsBones.hpp"
#include "Pose/PoseOverrides.hpp"

#include <Windows.h>

#include <RED4ext/CName.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace APM::Ui
{
namespace
{
const std::vector<CatalogEntry>& BodyCatalog()
{
    static const std::vector<CatalogEntry> catalog = {
        {"Torso", "Body (hips)", {"Hips"}, EntryMode::Single, true},
        {"Torso", "Lower back", {"Spine"}},
        {"Torso", "Middle back", {"Spine1"}},
        {"Torso", "Upper back", {"Spine2"}},
        {"Torso", "Chest", {"Spine3"}},
        {"Head", "Neck", {"Neck"}},
        {"Head", "Upper neck", {"Neck1"}},
        {"Head", "Head", {"Head"}},
        {"Head", "Eyes (both)", {"LeftEye", "RightEye"}, EntryMode::Mirror},
        {"Head", "Left eye", {"LeftEye"}},
        {"Head", "Right eye", {"RightEye"}},

        {"Left arm", "Left shoulder", {"LeftShoulder"}},
        {"Left arm", "Left upper arm", {"LeftArm"}},
        {"Left arm", "Left forearm", {"LeftForeArm"}},
        {"Left arm", "Left hand", {"LeftHand"}},
        {"Left hand", "Left thumb", {"LeftHandThumb1", "LeftHandThumb2"}, EntryMode::Curl},
        {"Left hand", "Left index finger", {"LeftHandIndex1", "LeftHandIndex2", "LeftHandIndex3"}, EntryMode::Curl},
        {"Left hand", "Left middle finger", {"LeftHandMiddle1", "LeftHandMiddle2", "LeftHandMiddle3"}, EntryMode::Curl},
        {"Left hand", "Left ring finger", {"LeftHandRing1", "LeftHandRing2", "LeftHandRing3"}, EntryMode::Curl},
        {"Left hand", "Left little finger", {"LeftHandPinky1", "LeftHandPinky2", "LeftHandPinky3"}, EntryMode::Curl},

        {"Right arm", "Right shoulder", {"RightShoulder"}},
        {"Right arm", "Right upper arm", {"RightArm"}},
        {"Right arm", "Right forearm", {"RightForeArm"}},
        {"Right arm", "Right hand", {"RightHand"}},
        {"Right hand", "Right thumb", {"RightHandThumb1", "RightHandThumb2"}, EntryMode::Curl},
        {"Right hand", "Right index finger", {"RightHandIndex1", "RightHandIndex2", "RightHandIndex3"}, EntryMode::Curl},
        {"Right hand", "Right middle finger", {"RightHandMiddle1", "RightHandMiddle2", "RightHandMiddle3"}, EntryMode::Curl},
        {"Right hand", "Right ring finger", {"RightHandRing1", "RightHandRing2", "RightHandRing3"}, EntryMode::Curl},
        {"Right hand", "Right little finger", {"RightHandPinky1", "RightHandPinky2", "RightHandPinky3"}, EntryMode::Curl},

        {"Left leg", "Left thigh", {"LeftUpLeg"}},
        {"Left leg", "Left shin", {"LeftLeg"}},
        {"Left leg", "Left foot", {"LeftFoot"}},
        {"Left leg", "Left toes", {"LeftToeBase"}},
        {"Right leg", "Right thigh", {"RightUpLeg"}},
        {"Right leg", "Right shin", {"RightLeg"}},
        {"Right leg", "Right foot", {"RightFoot"}},
        {"Right leg", "Right toes", {"RightToeBase"}},

        // Outer finger segments, edited from the close-up gizmos (kept last so earlier indices never change).
        {"Left hand", "Left thumb tip", {"LeftHandThumb2"}, EntryMode::FingerSegment},
        {"Left hand", "Left index middle joint", {"LeftHandIndex2"}, EntryMode::FingerSegment},
        {"Left hand", "Left index tip", {"LeftHandIndex3"}, EntryMode::FingerSegment},
        {"Left hand", "Left middle middle joint", {"LeftHandMiddle2"}, EntryMode::FingerSegment},
        {"Left hand", "Left middle tip", {"LeftHandMiddle3"}, EntryMode::FingerSegment},
        {"Left hand", "Left ring middle joint", {"LeftHandRing2"}, EntryMode::FingerSegment},
        {"Left hand", "Left ring tip", {"LeftHandRing3"}, EntryMode::FingerSegment},
        {"Left hand", "Left little middle joint", {"LeftHandPinky2"}, EntryMode::FingerSegment},
        {"Left hand", "Left little tip", {"LeftHandPinky3"}, EntryMode::FingerSegment},
        {"Right hand", "Right thumb tip", {"RightHandThumb2"}, EntryMode::FingerSegment},
        {"Right hand", "Right index middle joint", {"RightHandIndex2"}, EntryMode::FingerSegment},
        {"Right hand", "Right index tip", {"RightHandIndex3"}, EntryMode::FingerSegment},
        {"Right hand", "Right middle middle joint", {"RightHandMiddle2"}, EntryMode::FingerSegment},
        {"Right hand", "Right middle tip", {"RightHandMiddle3"}, EntryMode::FingerSegment},
        {"Right hand", "Right ring middle joint", {"RightHandRing2"}, EntryMode::FingerSegment},
        {"Right hand", "Right ring tip", {"RightHandRing3"}, EntryMode::FingerSegment},
        {"Right hand", "Right little middle joint", {"RightHandPinky2"}, EntryMode::FingerSegment},
        {"Right hand", "Right little tip", {"RightHandPinky3"}, EntryMode::FingerSegment},

        // Palm bones (close-up gizmos) and the points held props hang from (listed in the hand tab).
        {"Left hand", "Left thumb base", {"LeftInHandThumb"}, EntryMode::FingerSegment, false, true, true},
        {"Left hand", "Left palm - index", {"LeftInHandIndex"}, EntryMode::FingerSegment, false, true, true},
        {"Left hand", "Left palm - middle", {"LeftInHandMiddle"}, EntryMode::FingerSegment, false, true, true},
        {"Left hand", "Left palm - ring", {"LeftInHandRing"}, EntryMode::FingerSegment, false, true, true},
        {"Left hand", "Left palm - little", {"LeftInHandPinky"}, EntryMode::FingerSegment, false, true, true},
        {"Left hand", "Left hand prop", {"WeaponLeft"}, EntryMode::Single, true},
        {"Right hand", "Right thumb base", {"RightInHandThumb"}, EntryMode::FingerSegment, false, true, true},
        {"Right hand", "Right palm - index", {"RightInHandIndex"}, EntryMode::FingerSegment, false, true, true},
        {"Right hand", "Right palm - middle", {"RightInHandMiddle"}, EntryMode::FingerSegment, false, true, true},
        {"Right hand", "Right palm - ring", {"RightInHandRing"}, EntryMode::FingerSegment, false, true, true},
        {"Right hand", "Right palm - little", {"RightInHandPinky"}, EntryMode::FingerSegment, false, true, true},
        {"Right hand", "Right hand prop", {"WeaponRight"}, EntryMode::Single, true},
    };
    return catalog;
}

std::vector<CatalogEntry> g_catalog = BodyCatalog();
uint32_t g_sourceRevision = 0;
uint32_t g_revision = 1;
uint64_t g_retryAt = 0; // a visible mesh was not loaded yet: look again then
// Physics bones of the selected character last time (to drop the edits of a skeleton that went away).
std::unordered_set<uint64_t> g_lastBones;
uint32_t g_lastSlot = UINT32_MAX;

// Names live as long as the plugin: the entries keep plain pointers to them.
const char* Intern(const std::string& aText)
{
    static std::unordered_set<std::string> strings;
    return strings.insert(aText).first->c_str();
}

std::string Replace(std::string aText, const std::string& aFrom, const std::string& aTo)
{
    for (size_t at = aText.find(aFrom); at != std::string::npos; at = aText.find(aFrom, at + aTo.size()))
    {
        aText.replace(at, aFrom.size(), aTo);
    }
    return aText;
}

std::string Capitalized(std::string aText)
{
    while (!aText.empty() && aText.front() == ' ')
    {
        aText.erase(aText.begin());
    }
    while (!aText.empty() && aText.back() == ' ')
    {
        aText.pop_back();
    }
    if (!aText.empty())
    {
        aText.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(aText.front())));
    }
    return aText;
}

std::string SkeletonLabel(const std::string& aName)
{
    if (aName == "hair_dangle")
    {
        return "Hair";
    }
    if (aName == "breasts")
    {
        return "Breasts";
    }
    return aName;
}

// "dyng_back_chain_03_01" -> "Back chain 03"; "l_chest_front_A_bot_out_JNT" -> "Left A bottom".
std::string BoneLabel(std::string aName, bool aDropLastNumber)
{
    if (aName.rfind("dyng_", 0) == 0)
    {
        aName = aName.substr(5);
    }
    if (aName.size() > 4 && aName.substr(aName.size() - 4) == "_JNT")
    {
        aName.resize(aName.size() - 4);
    }
    aName = Replace(aName, "_out", "");
    aName = Replace(aName, "chest_front_", "");
    if (aName.rfind("l_", 0) == 0)
    {
        aName = "Left " + aName.substr(2);
    }
    else if (aName.rfind("r_", 0) == 0)
    {
        aName = "Right " + aName.substr(2);
    }
    if (aDropLastNumber)
    {
        const size_t underscore = aName.find_last_of('_');
        if (underscore != std::string::npos && underscore + 1 < aName.size() &&
            std::all_of(aName.begin() + underscore + 1, aName.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        {
            aName.resize(underscore);
        }
    }
    aName = Replace(aName, "_", " ");
    aName = Replace(aName, " bot", " bottom");
    return Capitalized(aName);
}

std::string NameOf(uint64_t aHash)
{
    const char* text = RED4ext::CName(aHash).ToString();
    return text && std::string_view(text) != "None" ? text : "";
}
} // namespace

const std::vector<CatalogEntry>& GetBoneCatalog()
{
    return g_catalog;
}

size_t GetBodyPartCount()
{
    return BodyCatalog().size();
}

uint32_t RefreshPhysicsEntries()
{
    const uint32_t source = Pose::GizmoState::GetPhysicsRevision();
    const uint64_t now = GetTickCount64();
    if (source == g_sourceRevision && (g_retryAt == 0 || now < g_retryAt))
    {
        return g_revision;
    }
    g_sourceRevision = source;
    g_retryAt = 0;
    const auto skeletons = Pose::GizmoState::GetPhysicsSkeletons();

    // Only the bones the visible meshes use (when they can all be read).
    std::unordered_set<uint64_t> used;
    bool complete = false;
    const bool read = !skeletons.empty() && MeshBones::Collect(skeletons.front().entity, used, complete);
    // A mesh still loading: look again in a second, for at most 10 s (then the bones read so far are used).
    static uint64_t s_waitSince = 0;
    if (read && !complete)
    {
        s_waitSince = s_waitSince ? s_waitSince : now;
        if (now - s_waitSince < 10000)
        {
            g_retryAt = now + 1000;
        }
    }
    else
    {
        s_waitSince = 0;
    }
    const bool filter = read && (complete || g_retryAt == 0) && !used.empty();

    // A skeleton that went away (other outfit or hair): its edits go too, the physics takes it back.
    std::unordered_set<uint64_t> bones;
    for (const auto& skeleton : skeletons)
    {
        for (const auto& bone : skeleton.bones)
        {
            if (bone.chainRoot >= 0 || bone.group >= 0 || Pose::PhysicsBones::GroupOfVirtual(bone.hash) >= 0)
            {
                bones.insert(bone.hash);
            }
        }
    }
    const uint32_t slot = Pose::Characters::Selected();
    if (slot == g_lastSlot && !skeletons.empty())
    {
        std::vector<uint64_t> gone;
        for (const uint64_t hash : g_lastBones)
        {
            if (!bones.contains(hash))
            {
                gone.push_back(hash);
            }
        }
        if (!gone.empty())
        {
            Pose::PoseOverrides::RemoveBones(gone);
        }
    }
    if (!skeletons.empty())
    {
        g_lastBones = std::move(bones);
        g_lastSlot = slot;
    }

    std::vector<CatalogEntry> catalog = BodyCatalog();
    std::unordered_set<std::string> labels;
    for (const auto& skeleton : skeletons)
    {
        std::string skeletonLabel = SkeletonLabel(skeleton.name);
        for (int n = 2; labels.contains(skeletonLabel); ++n)
        {
            skeletonLabel = SkeletonLabel(skeleton.name) + " " + std::to_string(n);
        }
        labels.insert(skeletonLabel);
        const int32_t category = Pose::PhysicsBones::CategoryOf(skeleton.name);
        size_t ownCount = 0;
        size_t usedCount = 0;
        for (const auto& bone : skeleton.bones)
        {
            if (bone.chainRoot >= 0)
            {
                ++ownCount;
                usedCount += used.contains(bone.hash) ? 1 : 0;
            }
        }
        APM::Log::Verbose("Physics bones: %s (%s): %zu own bones, %zu used by the visible meshes%s", skeleton.name.c_str(),
                          skeletonLabel.c_str(), ownCount, usedCount, filter ? "" : " (meshes not all read: all shown)");
        const char* group = Intern("~" + skeletonLabel);
        const char* skeletonText = Intern(skeletonLabel);
        // Groups (a breast) first.
        for (const auto& bone : skeleton.bones)
        {
            const int32_t g = Pose::PhysicsBones::GroupOfVirtual(bone.hash);
            if (g < 0)
            {
                continue;
            }
            const auto& rule = Pose::PhysicsBones::Groups[g];
            CatalogEntry entry{group, rule.label, {rule.name}, EntryMode::Group, true};
            entry.skeleton = skeletonText;
            entry.category = category;
            entry.chain = rule.label;
            entry.chainRoot = rule.name;
            catalog.push_back(std::move(entry));
        }
        // Then every chain, its bones from the top down.
        for (size_t root = 0; root < skeleton.bones.size(); ++root)
        {
            if (skeleton.bones[root].chainRoot != static_cast<int16_t>(root))
            {
                continue;
            }
            const std::string rootName = NameOf(skeleton.bones[root].hash);
            if (rootName.empty())
            {
                continue;
            }
            std::vector<size_t> members;
            for (size_t i = root; i < skeleton.bones.size(); ++i)
            {
                if (skeleton.bones[i].chainRoot == static_cast<int16_t>(root) && (!filter || used.contains(skeleton.bones[i].hash)))
                {
                    members.push_back(i);
                }
            }
            if (members.empty())
            {
                continue;
            }
            const bool single = members.size() == 1;
            const char* chainText = Intern(BoneLabel(rootName, !single));
            const char* rootText = Intern(rootName);
            for (size_t m = 0; m < members.size(); ++m)
            {
                const std::string name = NameOf(skeleton.bones[members[m]].hash);
                if (name.empty())
                {
                    continue;
                }
                const std::string label = single ? chainText : std::string(chainText) + " " + std::to_string(m + 1);
                // Breast bones can be moved a little; hair and clothing bones turn (and bend their chain when dragged).
                const bool movable = category == 1;
                CatalogEntry entry{group, Intern(label), {Intern(name)}, EntryMode::Single, movable};
                entry.skeleton = skeletonText;
                entry.chain = chainText;
                entry.chainRoot = rootText;
                entry.chainIndex = static_cast<int32_t>(m + 1);
                entry.ikMove = members[m] != root;
                entry.category = category;
                entry.support = skeleton.bones[members[m]].group >= 0 && !skeleton.bones[members[m]].pivot;
                catalog.push_back(std::move(entry));
            }
        }
    }
    // Unchanged (a look again after a mesh loaded): same revision, so the scripts keep their markers.
    const auto same = [](const CatalogEntry& a, const CatalogEntry& b)
    { return std::string_view(a.label) == b.label && a.bones == b.bones && a.group == b.group; };
    if (catalog.size() == g_catalog.size() && std::equal(catalog.begin(), catalog.end(), g_catalog.begin(), same))
    {
        return g_revision;
    }
    g_catalog = std::move(catalog);
    ++g_revision;
    return g_revision;
}
} // namespace APM::Ui
