#pragma once

#include <cstdint>
#include <string_view>

// Bones of the physics skeletons (hair, clothes, breasts) that the user can pose, and the groups moved as one piece.
//
// A group is edited through a virtual bone (its name starts with GroupPrefix): its rotation turns the members around
// their middle and its offset moves them, in the space of the bone they hang from (Spine3 for the breasts). The
// members' own edits are applied after it.
namespace APM::Pose::PhysicsBones
{
constexpr std::string_view GroupPrefix = "APM_group:";

struct GroupRule
{
    const char* skeleton;     // component name
    const char* name;         // virtual bone
    const char* memberPrefix; // bones whose name starts with it
    const char* label;        // panel label
};

inline constexpr GroupRule Groups[] = {
    {"breasts", "APM_group:l_chest", "l_chest", "Left breast"},
    {"breasts", "APM_group:r_chest", "r_chest", "Right breast"},
};
constexpr size_t GroupCount = sizeof(Groups) / sizeof(Groups[0]);

inline bool IsGroupBone(std::string_view aName)
{
    return aName.substr(0, GroupPrefix.size()) == GroupPrefix;
}

// Group a bone of a skeleton belongs to, or -1.
inline int32_t GroupOf(std::string_view aSkeleton, std::string_view aBone)
{
    for (size_t i = 0; i < GroupCount; ++i)
    {
        const std::string_view prefix = Groups[i].memberPrefix;
        if (aSkeleton == Groups[i].skeleton && aBone.substr(0, prefix.size()) == prefix)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Own bones that can be posed: in the breasts skeleton only the breast bones (the shoulder helpers follow the arms).
inline bool IsPosable(std::string_view aSkeleton, std::string_view aBone)
{
    if (aSkeleton == "breasts")
    {
        return aBone.find("chest") != std::string_view::npos;
    }
    return true;
}
} // namespace APM::Pose::PhysicsBones
