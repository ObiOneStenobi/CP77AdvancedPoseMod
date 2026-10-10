#pragma once

#include <cstdint>

// Runtime configuration read from red4ext/plugins/AdvancedPoseMod/config.ini.
// The file is re-read every time Photo Mode is opened, so values can be tweaked without restarting.
namespace APM::Config
{
struct Values
{
    // Research probe: only runs if the known context mapping stops working (e.g. after a game update).
    bool probeEnabled = true;
    uint32_t probeScanBudget = 4096;  // max number of distinct animation contexts inspected per session
    bool probeDumpRig = false;        // write the rig bone list to probe/rig_*.txt
    bool debugLog = false;            // detailed diagnostics in the log
};

void Load();
Values Get();
} // namespace APM::Config
