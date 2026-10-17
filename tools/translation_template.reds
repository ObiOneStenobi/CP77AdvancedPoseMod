// AdvancedPoseMod - translation template
//
// How to make a translation mod (needs Codeware, like AdvancedPoseMod):
// 1. Copy this file to r6\scripts\AdvancedPoseMod_<language>\Translation.reds (e.g. AdvancedPoseMod_it).
// 2. Rename the module and the two classes (replace "xx" with your language, e.g. "it"): every translation
//    must have its own class names.
// 3. Set the language code in GetPackage (the game's interface language: it-it, fr-fr, de-de, es-es, pl-pl,
//    pt-br, ru-ru, zh-cn, zh-tw, jp-jp, kr-kr, ...).
// 4. Translate the second text of every line. Keep "{0}": the value (a name) goes there. Lines left out show
//    in English. Long texts on buttons shrink to fit; keep them short anyway.
// The game shows the translation when its interface language matches; Photo Mode must be reopened after a
// language change.

module AdvancedPoseModTranslation.xx

import Codeware.Localization.*

public class APM_TranslationProvider_xx extends ModLocalizationProvider {
    public func GetPackage(language: CName) -> ref<ModLocalizationPackage> {
        return Equals(language, n"xx-xx") ? new APM_TranslationPackage_xx() : null;
    }

    public func GetFallback() -> CName {
        return n"";
    }
}

public class APM_TranslationPackage_xx extends ModLocalizationPackage {
    protected func DefineTexts() {
        // Panel
        this.Text("APM-Button-PoseEditor", "Pose editor");
        this.Text("APM-Title", "Advanced pose");
        this.Text("APM-Msg-UnsupportedVersion", "Unsupported game version - see the log");
        this.Text("APM-Button-PoseOff", "Pose off");
        this.Text("APM-Tip-PoseOnOff", "Turn your edits on V on / off");
        this.Text("APM-Button-Gizmos", "Gizmos");
        this.Text("APM-Tip-Gizmos", "Show / hide the markers on V");
        this.Text("APM-Tip-Opacity", "Panel opacity");
        this.Text("APM-Tip-Help", "Info: markers and shortcuts");
        this.Text("APM-Tip-HidePanel", "Hide the panel");
        this.Text("APM-Tip-CharacterPrev", "Previous character to pose");
        this.Text("APM-Tip-CharacterNext", "Next character to pose");
        this.Text("APM-Tip-Undo", "Undo the last change of this character (Ctrl+Z)");
        this.Text("APM-Tip-Redo", "Redo the last undone change of this character (Ctrl+Y)");
        this.Text("APM-Tip-Mirror", "Mirror: while on, the limb you edit is copied to the other side and followed");
        this.Text("APM-Tip-LimitsOn", "Joint limits on: rotations stay natural. Click to unlock");
        this.Text("APM-Tip-LimitsOff", "Joint limits off: any rotation is allowed. Click to lock");
        this.Text("APM-Tip-AutoIk", "Auto IK: dragging a hand / foot moves it with IK and pins it (Shift rotates it). Off: dragging rotates it, and IK is used only on limbs pinned in the IK row");
        this.Text("APM-Tip-ResetAll", "Reset everything (click twice) - items in hand stay");
        this.Text("APM-Label-Preset", "Preset");
        this.Text("APM-Preset-Open", "Open");
        this.Text("APM-Preset-Relaxed", "Relaxed");
        this.Text("APM-Preset-Fist", "Fist");
        this.Text("APM-Preset-Point", "Point");
        this.Text("APM-Preset-Grip", "Grip");
        this.Text("APM-Button-Reset", "Reset");
        this.Text("APM-Tip-PartReset", "Undo every edit of this part (an IK pin goes back to the pose's place too)");
        this.Text("APM-Button-EyesBoth", "Both");
        this.Text("APM-Tip-EyesBoth", "Both eyes together: one look marker");
        this.Text("APM-Button-EyeLeft", "Left");
        this.Text("APM-Tip-EyeLeft", "Left eye on its own (one marker per eye)");
        this.Text("APM-Button-EyeRight", "Right");
        this.Text("APM-Tip-EyeRight", "Right eye on its own (one marker per eye)");
        this.Text("APM-Row-BendForwardBack", "Bend forward / back");
        this.Text("APM-Row-BendSideways", "Bend sideways");
        this.Text("APM-Row-Twist", "Twist");
        this.Text("APM-Title-Move", "Move (cm)");
        this.Text("APM-Row-LeftRight", "Left / right");
        this.Text("APM-Row-ForwardBack", "Forward / back");
        this.Text("APM-Row-UpDown", "Up / down");
        this.Text("APM-Title-Ik", "IK");
        this.Text("APM-Tip-IkLimb", "IK limb: the frame lights up while it is pinned");
        this.Text("APM-Button-PinOff", "Off");
        this.Text("APM-Tip-PinOff", "Pin off: the hand / foot follows the arm / leg rotations");
        this.Text("APM-Button-PinWorld", "World");
        this.Text("APM-Tip-PinWorld", "Pin world: the hand / foot stays in its place in the scene, whatever the body does");
        this.Text("APM-Button-PinBody", "Body");
        this.Text("APM-Tip-PinBody", "Pin body: the hand / foot keeps its place relative to the shoulder / hips and moves with them");
        this.Text("APM-Row-RightLeft", "Right / left");
        this.Text("APM-Row-ElbowKnee", "Elbow / knee");
        this.Text("APM-Title-Poses", "Poses");
        this.Text("APM-Button-TPose", "T pose");
        this.Text("APM-Button-APose", "A pose");
        this.Text("APM-Tip-PosesFolder", "Open the folder of the saved poses (to share them; in full screen the game is minimized)");
        this.Text("APM-Tip-PosesShow", "Show the saved poses");
        this.Text("APM-Tip-PosesHide", "Hide the saved poses");
        this.Text("APM-Button-SaveNew", "Save new");
        this.Text("APM-Button-Overwrite", "Overwrite");
        this.Text("APM-Button-Load", "Load");
        this.Text("APM-Button-Rename", "Rename");
        this.Text("APM-Button-Delete", "Delete");
        this.Text("APM-Title-InHand", "In hand");
        this.Text("APM-Label-RightHand", "Right");
        this.Text("APM-Tip-ItemRestore", "Give back the item of the Photo Mode pose");
        this.Text("APM-Tip-ItemClearRight", "Empty the right hand");
        this.Text("APM-Label-LeftHand", "Left");
        this.Text("APM-Tip-ItemClearLeft", "Empty the left hand");
        this.Text("APM-Picker-All", "All");
        this.Text("APM-Picker-Weapons", "Weapons");
        this.Text("APM-Picker-Food", "Food");
        this.Text("APM-Picker-Other", "Other");
        this.Text("APM-Help-Title", "Info");
        this.Text("APM-Help-ShapeRotate", "rotate");
        this.Text("APM-Help-ShapeMove", "move");
        this.Text("APM-Help-ShapeTwist", "twist");
        this.Text("APM-Help-ShapeIk", "IK: moves the hand / foot, the arm / leg follows");
        this.Text("APM-Help-ShapeHips", "hips: V's root, moves the whole body");
        this.Text("APM-Help-KeyAltDrag", "Alt + drag");
        this.Text("APM-Help-RotateFreely", "rotate freely");
        this.Text("APM-Help-KeyCtrlDrag", "Ctrl + drag");
        this.Text("APM-Help-Twist", "twist");
        this.Text("APM-Help-KeyShiftDrag", "Shift + drag");
        this.Text("APM-Help-ShiftDrag", "move (hips, held items) or rotate (hands / feet with IK)");
        this.Text("APM-Help-KeyWheel", "Mouse wheel");
        this.Text("APM-Help-Wheel", "while moving: nearer / farther");
        this.Text("APM-Help-KeyRightClick", "Right-click a marker");
        this.Text("APM-Help-RightClick", "reset that part");
        this.Text("APM-Help-KeyUndoRedo", "Ctrl + Z  /  Ctrl + Y");
        this.Text("APM-Help-UndoRedo", "undo / redo");
        this.Text("APM-Label-Root", "V's root (Photo Mode position)");
        this.Text("APM-Msg-TypingUnavailable", "Typing is not available");
        this.Text("APM-Picker-Count", "{0} items");
        this.Text("APM-Picker-ClickToSearch", "Click to search");
        this.Text("APM-Item-Nothing", "Nothing - click to choose");
        this.Text("APM-Item-FromPose", "{0} (pose)");
        this.Text("APM-Msg-NoModel", "{0} has no model to hold");
        this.Text("APM-Item-ThisItem", "This item");
        this.Text("APM-Msg-RenameFailed", "Could not rename {0}");
        this.Text("APM-Msg-TurnedByLookAt", "{0} is turned by Look at camera");
        this.Text("APM-Button-PoseOn", "Pose on");
        this.Text("APM-PartShort-Eyes", "Eyes");
        this.Text("APM-Row-FocusDistance", "Focus distance");
        this.Text("APM-Tip-ResetAllConfirm", "Click again to reset everything");
        this.Text("APM-Button-ConfirmDelete", "Confirm delete");
        this.Text("APM-Hint-ShiftRotate", "(Shift: rotate, Alt: free, Ctrl: twist)");
        this.Text("APM-Hint-ShiftMoveIk", "(Shift: move with IK, Alt: free, Ctrl: twist)");
        this.Text("APM-Hint-AltFree", "(Alt: free, Ctrl: twist)");
        this.Text("APM-Hint-ShiftMove", "(Shift: move, Alt: free, Ctrl: twist)");
        this.Text("APM-Hint-CtrlTwist", "(Ctrl: twist)");
        this.Text("APM-Hint-RightClickReset", "Right-click: reset");
        this.Text("APM-Mode-MoveIk", "move (IK)");
        this.Text("APM-Mode-Move", "move");
        this.Text("APM-Mode-Swivel", "turn elbow / knee (IK)");
        this.Text("APM-Mode-Twist", "twist");
        this.Text("APM-Mode-Look", "look");
        this.Text("APM-Mode-FreeRotate", "free rotate");
        this.Text("APM-Mode-Rotate", "rotate");
        this.Text("APM-Msg-NeutralUnavailable", "Neutral pose not available");
        this.Text("APM-Msg-ResetAllConfirm", "Click again to reset everything (items in hand stay)");
        this.Text("APM-Msg-PoseSaveFailed", "Could not save the pose");
        this.Text("APM-Msg-PoseLoadFailed", "Could not load {0}");
        this.Text("APM-Label-Character", "Character {0}");
        this.Text("APM-Tip-PresetOpen", "Straight fingers");
        this.Text("APM-Tip-PresetRelaxed", "Slightly curled, at rest");
        this.Text("APM-Tip-PresetFist", "Closed fist");
        this.Text("APM-Tip-PresetPoint", "Index finger pointing");
        this.Text("APM-Tip-PresetGrip", "Fingers around a handle");

        // Body part groups (tabs)
        this.Text("APM-Group-Torso", "Torso");
        this.Text("APM-Group-Head", "Head");
        this.Text("APM-Group-LeftArm", "Left arm");
        this.Text("APM-Group-LeftHand", "Left hand");
        this.Text("APM-Group-RightArm", "Right arm");
        this.Text("APM-Group-RightHand", "Right hand");
        this.Text("APM-Group-LeftLeg", "Left leg");
        this.Text("APM-Group-RightLeg", "Right leg");

        // Body parts (full names)
        this.Text("APM-Part-BodyHips", "Body (hips)");
        this.Text("APM-Part-LowerBack", "Lower back");
        this.Text("APM-Part-MiddleBack", "Middle back");
        this.Text("APM-Part-UpperBack", "Upper back");
        this.Text("APM-Part-Chest", "Chest");
        this.Text("APM-Part-Neck", "Neck");
        this.Text("APM-Part-UpperNeck", "Upper neck");
        this.Text("APM-Part-Head", "Head");
        this.Text("APM-Part-EyesBoth", "Eyes (both)");
        this.Text("APM-Part-LeftEye", "Left eye");
        this.Text("APM-Part-RightEye", "Right eye");
        this.Text("APM-Part-LeftShoulder", "Left shoulder");
        this.Text("APM-Part-LeftUpperArm", "Left upper arm");
        this.Text("APM-Part-LeftForearm", "Left forearm");
        this.Text("APM-Part-LeftHand", "Left hand");
        this.Text("APM-Part-LeftThumb", "Left thumb");
        this.Text("APM-Part-LeftIndexFinger", "Left index finger");
        this.Text("APM-Part-LeftMiddleFinger", "Left middle finger");
        this.Text("APM-Part-LeftRingFinger", "Left ring finger");
        this.Text("APM-Part-LeftLittleFinger", "Left little finger");
        this.Text("APM-Part-RightShoulder", "Right shoulder");
        this.Text("APM-Part-RightUpperArm", "Right upper arm");
        this.Text("APM-Part-RightForearm", "Right forearm");
        this.Text("APM-Part-RightHand", "Right hand");
        this.Text("APM-Part-RightThumb", "Right thumb");
        this.Text("APM-Part-RightIndexFinger", "Right index finger");
        this.Text("APM-Part-RightMiddleFinger", "Right middle finger");
        this.Text("APM-Part-RightRingFinger", "Right ring finger");
        this.Text("APM-Part-RightLittleFinger", "Right little finger");
        this.Text("APM-Part-LeftThigh", "Left thigh");
        this.Text("APM-Part-LeftShin", "Left shin");
        this.Text("APM-Part-LeftFoot", "Left foot");
        this.Text("APM-Part-LeftToes", "Left toes");
        this.Text("APM-Part-RightThigh", "Right thigh");
        this.Text("APM-Part-RightShin", "Right shin");
        this.Text("APM-Part-RightFoot", "Right foot");
        this.Text("APM-Part-RightToes", "Right toes");
        this.Text("APM-Part-LeftThumbTip", "Left thumb tip");
        this.Text("APM-Part-LeftIndexMiddleJoint", "Left index middle joint");
        this.Text("APM-Part-LeftIndexTip", "Left index tip");
        this.Text("APM-Part-LeftMiddleMiddleJoint", "Left middle middle joint");
        this.Text("APM-Part-LeftMiddleTip", "Left middle tip");
        this.Text("APM-Part-LeftRingMiddleJoint", "Left ring middle joint");
        this.Text("APM-Part-LeftRingTip", "Left ring tip");
        this.Text("APM-Part-LeftLittleMiddleJoint", "Left little middle joint");
        this.Text("APM-Part-LeftLittleTip", "Left little tip");
        this.Text("APM-Part-RightThumbTip", "Right thumb tip");
        this.Text("APM-Part-RightIndexMiddleJoint", "Right index middle joint");
        this.Text("APM-Part-RightIndexTip", "Right index tip");
        this.Text("APM-Part-RightMiddleMiddleJoint", "Right middle middle joint");
        this.Text("APM-Part-RightMiddleTip", "Right middle tip");
        this.Text("APM-Part-RightRingMiddleJoint", "Right ring middle joint");
        this.Text("APM-Part-RightRingTip", "Right ring tip");
        this.Text("APM-Part-RightLittleMiddleJoint", "Right little middle joint");
        this.Text("APM-Part-RightLittleTip", "Right little tip");
        this.Text("APM-Part-LeftThumbBase", "Left thumb base");
        this.Text("APM-Part-LeftPalmIndex", "Left palm - index");
        this.Text("APM-Part-LeftPalmMiddle", "Left palm - middle");
        this.Text("APM-Part-LeftPalmRing", "Left palm - ring");
        this.Text("APM-Part-LeftPalmLittle", "Left palm - little");
        this.Text("APM-Part-LeftHandProp", "Left hand prop");
        this.Text("APM-Part-RightThumbBase", "Right thumb base");
        this.Text("APM-Part-RightPalmIndex", "Right palm - index");
        this.Text("APM-Part-RightPalmMiddle", "Right palm - middle");
        this.Text("APM-Part-RightPalmRing", "Right palm - ring");
        this.Text("APM-Part-RightPalmLittle", "Right palm - little");
        this.Text("APM-Part-RightHandProp", "Right hand prop");

        // Body parts (short names on the buttons, the tab says the side)
        this.Text("APM-PartShort-Shoulder", "shoulder");
        this.Text("APM-PartShort-UpperArm", "upper arm");
        this.Text("APM-PartShort-Forearm", "forearm");
        this.Text("APM-PartShort-Hand", "hand");
        this.Text("APM-PartShort-Thumb", "thumb");
        this.Text("APM-PartShort-IndexFinger", "index finger");
        this.Text("APM-PartShort-MiddleFinger", "middle finger");
        this.Text("APM-PartShort-RingFinger", "ring finger");
        this.Text("APM-PartShort-LittleFinger", "little finger");
        this.Text("APM-PartShort-Thigh", "thigh");
        this.Text("APM-PartShort-Shin", "shin");
        this.Text("APM-PartShort-Foot", "foot");
        this.Text("APM-PartShort-Toes", "toes");
        this.Text("APM-PartShort-ThumbTip", "thumb tip");
        this.Text("APM-PartShort-IndexMiddleJoint", "index middle joint");
        this.Text("APM-PartShort-IndexTip", "index tip");
        this.Text("APM-PartShort-MiddleMiddleJoint", "middle middle joint");
        this.Text("APM-PartShort-MiddleTip", "middle tip");
        this.Text("APM-PartShort-RingMiddleJoint", "ring middle joint");
        this.Text("APM-PartShort-RingTip", "ring tip");
        this.Text("APM-PartShort-LittleMiddleJoint", "little middle joint");
        this.Text("APM-PartShort-LittleTip", "little tip");
        this.Text("APM-PartShort-ThumbBase", "thumb base");
        this.Text("APM-PartShort-PalmIndex", "palm - index");
        this.Text("APM-PartShort-PalmMiddle", "palm - middle");
        this.Text("APM-PartShort-PalmRing", "palm - ring");
        this.Text("APM-PartShort-PalmLittle", "palm - little");
        this.Text("APM-PartShort-HandProp", "hand prop");
    }
}
