// Rows of the expression editor (the panel's Face mode), in the order shown in each zone tab: the most used first.
// A row moves one face pose of the game (names as in red4ext/plugins/AdvancedPoseMod/face/*.json). Left / right poses
// are given by their left pose: with L+R on one row moves both, else each side has its own row ("Smile L", "Smile R").
// A row with a negative pose is a -100..100 slider: below 0 it sets the negative pose, above 0 the other one.

public class APM_FaceRowTable extends IScriptable {
    public let zones: array<String>;     // tabs, as the poses' zones in face/*.json
    public let zoneKeys: array<String>;
    public let zoneTexts: array<String>;
    public let poses: array<String>;
    public let negatives: array<String>; // "" for a 0..100 row
    public let keys: array<String>;      // translation of the row label
    public let texts: array<String>;     // English label
}

public func APM_BuildFaceRowTable() -> ref<APM_FaceRowTable> {
    let table = new APM_FaceRowTable();
    let zones: array<String> = ["brows", "eyelids", "nose", "cheeks", "lips", "jaw", "ears", "tongue"];
    let faceZoneKeys: array<String> = ["APM-FaceZone-Brows", "APM-FaceZone-Eyelids", "APM-FaceZone-Nose", "APM-FaceZone-Cheeks", "APM-FaceZone-Lips", "APM-FaceZone-Jaw", "APM-FaceZone-Ears", "APM-FaceZone-Tongue"];
    let faceZoneTexts: array<String> = ["Brows", "Eyelids", "Nose", "Cheeks", "Lips", "Jaw", "Ears", "Tongue"];
    table.zones = zones;
    table.zoneKeys = faceZoneKeys;
    table.zoneTexts = faceZoneTexts;
    let poses: array<String> = [
        "eye_l_brows_raise_in", "eye_l_brows_raise_out", "eye_l_brows_lower", "eye_l_brows_lateral",
        "eye_l_blink", "eye_l_widen", "eye_l_oculi_squint_outer_lower", "eye_l_oculi_squint_outer_upper", "eye_l_oculi_squint_inner",
        "nose_l_snear", "nose_l_breathe_out", "nose_l_compress",
        "cheek_l_puff", "cheek_l_suck",
        "lips_l_corner_up", "lips_l_corner_dn", "lips_l_purse", "lips_l_funnel", "lips_l_corner_stretch", "lips_l_upper_raise",
        "lips_l_lower_raise", "lips_l_stretch", "lips_l_corner_wide", "lips_l_pull", "lips_l_nasolabialDeepener",
        "lips_l_corner_sharp_up", "lips_suck_up", "lips_suck_dn", "lips_puff_up", "lips_puff_dn", "lips_apart_up",
        "lips_apart_dn", "lips_chin_raise", "lips_mid_shift_r", "lips_mid_shift_up",
        "jaw_mid_open", "jaw_mid_shift_r", "jaw_mid_shift_fwd",
        "ear_l_shift_up",
        "tongue_mid_fwd", "tongue_mid_lift", "tongue_mid_tip_r", "tongue_mid_tip_up", "tongue_mid_twist_r", "tongue_mid_base_r",
        "tongue_mid_base_up", "tongue_mid_base_fwd", "tongue_mid_base_front"];
    let negatives: array<String> = [
        "", "", "", "",
        "", "", "", "", "",
        "", "nose_l_breathe_in", "",
        "", "",
        "", "", "", "", "", "",
        "", "", "", "", "",
        "", "", "", "", "", "",
        "", "", "lips_mid_shift_l", "lips_mid_shift_dn",
        "", "jaw_mid_shift_l", "jaw_mid_shift_back",
        "",
        "", "", "tongue_mid_tip_l", "tongue_mid_tip_dn", "tongue_mid_twist_l", "tongue_mid_base_l",
        "tongue_mid_base_dn", "", "tongue_mid_base_back"];
    let faceRowKeys: array<String> = [
        "APM-FacePose-InnerBrowRaise", "APM-FacePose-OuterBrowRaise", "APM-FacePose-BrowLower", "APM-FacePose-BrowSqueeze",
        "APM-FacePose-Blink", "APM-FacePose-Widen", "APM-FacePose-LowerLidSquint", "APM-FacePose-UpperLidSquint", "APM-FacePose-InnerSquint",
        "APM-FacePose-Sneer", "APM-FacePose-NostrilsNarrowFlare", "APM-FacePose-NostrilCompress",
        "APM-FacePose-CheekPuff", "APM-FacePose-CheekSuck",
        "APM-FacePose-Smile", "APM-FacePose-CornersDown", "APM-FacePose-Pucker", "APM-FacePose-Funnel", "APM-FacePose-CornersStretch", "APM-FacePose-UpperLipRaise",
        "APM-FacePose-LowerLipDown", "APM-FacePose-LipsStretch", "APM-FacePose-CornersWide", "APM-FacePose-LipsPull", "APM-FacePose-NasolabialFold",
        "APM-FacePose-CornerTuck", "APM-FacePose-UpperLipIn", "APM-FacePose-LowerLipIn", "APM-FacePose-UpperLipOut", "APM-FacePose-LowerLipOut", "APM-FacePose-UpperLipPart",
        "APM-FacePose-LowerLipPart", "APM-FacePose-ChinRaise", "APM-FacePose-MouthLeftRight", "APM-FacePose-MouthDownUp",
        "APM-FacePose-OpenJaw", "APM-FacePose-JawLeftRight", "APM-FacePose-JawBackForward",
        "APM-FacePose-EarsUp",
        "APM-FacePose-TongueOut", "APM-FacePose-TongueLift", "APM-FacePose-TipLeftRight", "APM-FacePose-TipDownUp", "APM-FacePose-TwistLeftRight", "APM-FacePose-BaseLeftRight",
        "APM-FacePose-BaseDownUp", "APM-FacePose-BaseForward", "APM-FacePose-BaseBackFront"];
    let faceRowTexts: array<String> = [
        "Inner brow raise", "Outer brow raise", "Brow lower", "Brow squeeze",
        "Blink", "Widen", "Lower lid squint", "Upper lid squint", "Inner squint",
        "Sneer", "Nostrils narrow / flare", "Nostril compress",
        "Cheek puff", "Cheek suck",
        "Smile", "Corners down", "Pucker", "Funnel", "Corners stretch", "Upper lip raise",
        "Lower lip down", "Lips stretch", "Corners wide", "Lips pull", "Nasolabial fold",
        "Corner tuck", "Upper lip in", "Lower lip in", "Upper lip out", "Lower lip out", "Upper lip part",
        "Lower lip part", "Chin raise", "Mouth left / right", "Mouth down / up",
        "Open jaw", "Jaw left / right", "Jaw back / forward",
        "Ears up",
        "Tongue out", "Tongue lift", "Tip left / right", "Tip down / up", "Twist left / right", "Base left / right",
        "Base down / up", "Base forward", "Base back / front"];
    table.poses = poses;
    table.negatives = negatives;
    table.keys = faceRowKeys;
    table.texts = faceRowTexts;
    return table;
}
