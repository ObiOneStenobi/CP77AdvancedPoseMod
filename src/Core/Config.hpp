#pragma once

#include <cstdint>
#include <string>

// Runtime configuration read from red4ext/plugins/AdvancedPoseMod/config.ini.
// The file is read when the game starts and again every time Photo Mode is opened, so values can be tweaked without
// restarting (AllowUnknownGameVersion only counts at game start).
namespace APM::Config
{
struct Values
{
    // Research probe: only runs if the known context mapping stops working (e.g. after a game update).
    bool probeEnabled = true;
    uint32_t probeScanBudget = 4096;  // max number of distinct animation contexts inspected per session
    bool probeDumpRig = false;        // write the rig bone list to probe/rig_*.txt
    // Expression editor through the game's facial solver (animAnimNode_Sermo, read at game start); off = the face poses
    // are added to the bones after it (no correctives).
    bool faceGameSolver = true;
    // Face solver probe (research): log the face_rig float tracks the solver receives and add probeFaceTrackWeight to
    // the track probeFaceTrack.
    bool probeFaceTracks = false;
    std::string probeFaceTrack;
    float probeFaceTrackWeight = 0.f;
    bool debugLog = false;            // detailed diagnostics in the log
    bool allowUnknownGameVersion = false; // install the hooks on a game version the plugin was not built for (risky)
};

void Load();
Values Get();
} // namespace APM::Config
