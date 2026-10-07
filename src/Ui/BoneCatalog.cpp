#include "Ui/BoneCatalog.hpp"

namespace APM::Ui
{
const std::vector<CatalogEntry>& GetBoneCatalog()
{
    static const std::vector<CatalogEntry> catalog = {
        {"Body (hips)", {"Hips"}, EntryMode::Single, true},
        {"Lower back", {"Spine"}},
        {"Middle back", {"Spine1"}},
        {"Upper back", {"Spine2"}},
        {"Chest", {"Spine3"}},
        {"Neck", {"Neck"}},
        {"Upper neck", {"Neck1"}},
        {"Head", {"Head"}},
        {"Eyes (both)", {"LeftEye", "RightEye"}, EntryMode::Mirror},
        {"Left eye", {"LeftEye"}},
        {"Right eye", {"RightEye"}},

        {"Left shoulder", {"LeftShoulder"}},
        {"Left upper arm", {"LeftArm"}},
        {"Left forearm", {"LeftForeArm"}},
        {"Left hand", {"LeftHand"}},
        {"Left thumb", {"LeftHandThumb1", "LeftHandThumb2"}, EntryMode::Curl},
        {"Left index finger", {"LeftHandIndex1", "LeftHandIndex2", "LeftHandIndex3"}, EntryMode::Curl},
        {"Left middle finger", {"LeftHandMiddle1", "LeftHandMiddle2", "LeftHandMiddle3"}, EntryMode::Curl},
        {"Left ring finger", {"LeftHandRing1", "LeftHandRing2", "LeftHandRing3"}, EntryMode::Curl},
        {"Left little finger", {"LeftHandPinky1", "LeftHandPinky2", "LeftHandPinky3"}, EntryMode::Curl},

        {"Right shoulder", {"RightShoulder"}},
        {"Right upper arm", {"RightArm"}},
        {"Right forearm", {"RightForeArm"}},
        {"Right hand", {"RightHand"}},
        {"Right thumb", {"RightHandThumb1", "RightHandThumb2"}, EntryMode::Curl},
        {"Right index finger", {"RightHandIndex1", "RightHandIndex2", "RightHandIndex3"}, EntryMode::Curl},
        {"Right middle finger", {"RightHandMiddle1", "RightHandMiddle2", "RightHandMiddle3"}, EntryMode::Curl},
        {"Right ring finger", {"RightHandRing1", "RightHandRing2", "RightHandRing3"}, EntryMode::Curl},
        {"Right little finger", {"RightHandPinky1", "RightHandPinky2", "RightHandPinky3"}, EntryMode::Curl},

        {"Left thigh", {"LeftUpLeg"}},
        {"Left shin", {"LeftLeg"}},
        {"Left foot", {"LeftFoot"}},
        {"Left toes", {"LeftToeBase"}},
        {"Right thigh", {"RightUpLeg"}},
        {"Right shin", {"RightLeg"}},
        {"Right foot", {"RightFoot"}},
        {"Right toes", {"RightToeBase"}},
    };
    return catalog;
}
} // namespace APM::Ui
