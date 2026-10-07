#pragma once

#include <cstdint>
#include <string>

// Runtime configuration read from red4ext/plugins/AdvancedPoseMod/config.ini.
// The file is re-read every time Photo Mode is opened, so values can be tweaked without restarting.
namespace APM::Config
{
struct Values
{
    // Research probe (phase 0): discovers how the animation context maps to an entity.
    bool probeEnabled = true;
    uint32_t probeScanBudget = 4096;  // max number of distinct animation contexts inspected per session
    bool probeDumpRig = true;         // write the rig bone list to probe/rig_*.txt

    // Proof of concept: rotate a single bone of V while Photo Mode is active.
    bool testEnabled = true;
    std::string testBone = "Head";
    float testRotX = 0.f; // degrees, bone local axes
    float testRotY = 0.f;
    float testRotZ = 45.f;
};

void Load();
Values Get();
} // namespace APM::Config
