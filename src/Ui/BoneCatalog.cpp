#include "Ui/BoneCatalog.hpp"

namespace APM::Ui
{
const std::vector<CatalogEntry>& GetBoneCatalog()
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
    };
    return catalog;
}
} // namespace APM::Ui
