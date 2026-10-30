// AdvancedPoseMod - pose editor panel and gizmos shown over Photo Mode.
//
// A self-made ink panel (texts translated through Codeware, see Localization.reds): body part tabs, value rows that can be dragged with the mouse or
// stepped with -/+ buttons, IK controls and a pose library. It drives the same plugin state as the Photo Mode menu
// items (APM_UiSetValue / APM_UiGetValue with the 77xx attribute keys), so both stay in sync.
//
// Gizmos: one marker per body part, placed on V's joints. Click a marker to select the part; drag it to rotate the
// part, or, for hands and feet, to move them with IK.
//
// Note: never return a wref built from a local ref - the game crashes when the caller ignores such a result.

module AdvancedPoseMod.Ui

// --- Constants ----------------------------------------------------------------------------------------------------

// Layout is designed in 1920x1080-like units and scaled to the 3840x2160 ink canvas.
public func APM_UiScale() -> Float {
    return 1.5;
}

public func APM_PanelWidth() -> Float {
    return 640.0;
}

public func APM_PanelHeight() -> Float {
    return 1022.0;
}

// Size of the ink canvas that covers the whole screen. The game's interface is laid out on 3840x2160 and keeps that
// size on 16:9 screens; on wider ones (21:9, 32:9) it keeps the height and gets wider, on narrower ones (16:10, 4:3) it
// keeps the width and gets taller.
public func APM_ScreenAspect() -> Float {
    let size = APM_GetViewportSize();
    if size.W < 0.5 || size.X < 1.0 || size.Y < 1.0 {
        return 16.0 / 9.0;
    }
    return size.X / size.Y;
}

public func APM_ScreenWidth() -> Float {
    let aspect = APM_ScreenAspect();
    return aspect >= 16.0 / 9.0 ? 2160.0 * aspect : 3840.0;
}

public func APM_ScreenHeight() -> Float {
    let aspect = APM_ScreenAspect();
    return aspect >= 16.0 / 9.0 ? 2160.0 : 3840.0 / aspect;
}

public func APM_PosesPerPage() -> Int32 {
    return 5;
}

public func APM_Color(r: Float, g: Float, b: Float, a: Float) -> HDRColor {
    let color: HDRColor;
    color.Red = r;
    color.Green = g;
    color.Blue = b;
    color.Alpha = a;
    return color;
}

public func APM_ColorAccent() -> HDRColor {
    return APM_Color(0.369, 0.965, 1.0, 1.0);
}

public func APM_ColorText() -> HDRColor {
    return APM_Color(0.85, 0.9, 0.92, 1.0);
}

public func APM_ColorDimText() -> HDRColor {
    return APM_Color(0.45, 0.5, 0.52, 1.0);
}

public func APM_ColorDark() -> HDRColor {
    return APM_Color(0.04, 0.05, 0.07, 1.0);
}

public func APM_ColorButton() -> HDRColor {
    return APM_Color(0.09, 0.12, 0.15, 1.0);
}

public func APM_ColorButtonHover() -> HDRColor {
    return APM_Color(0.15, 0.27, 0.31, 1.0);
}

public func APM_ColorWarning() -> HDRColor {
    return APM_Color(1.0, 0.38, 0.33, 1.0);
}

public func APM_ColorIk() -> HDRColor {
    return APM_Color(1.0, 0.72, 0.2, 1.0);
}

public func APM_Vector(x: Float, y: Float, z: Float, w: Float) -> Vector4 {
    let v: Vector4;
    v.X = x;
    v.Y = y;
    v.Z = z;
    v.W = w;
    return v;
}

// "Left upper arm" -> "Upper arm" (the tab already says which side)
public func APM_ShortPartLabel(label: String) -> String {
    if StrEndsWith(label, " eye") {
        return label; // "Left eye" / "Right eye" sit side by side in the Head tab
    }
    if StrBeginsWith(label, "Left ") {
        return StrAfterFirst(label, "Left ");
    }
    if StrBeginsWith(label, "Right ") {
        return StrAfterFirst(label, "Right ");
    }
    return label;
}

public func APM_IsHandGroup(group: String) -> Bool {
    return Equals(group, "Left hand") || Equals(group, "Right hand");
}

// --- Button ----------------------------------------------------------------------------------------------------------

public class APM_Button extends IScriptable {
    public let id: String;
    public let root: wref<inkCanvas>;
    public let bg: wref<inkRectangle>;
    public let label: wref<inkText>;
    public let isSelected: Bool;
    public let isEnabled: Bool;
    public let isHovered: Bool;
    public let isMarked: Bool; // secondary highlight
    public let tooltip: String; // shown above the button after a short hover
    public let parent: wref<inkCompoundWidget>; // where the button sits, and its place there (panel units)
    public let x: Float;
    public let y: Float;
    public let w: Float;
    public let h: Float;
    public let icon: array<wref<inkRectangle>>; // drawn icon (instead of a label)
    public let outline: array<wref<inkRectangle>>; // glowing frame (IK limb buttons: that limb is pinned)
    public let dot: wref<inkRectangle>; // small mark in the corner: this part (or a part of this tab) is edited

    public func UpdateLook() {
        if !IsDefined(this.root) {
            return;
        }
        this.root.SetOpacity(this.isEnabled ? 1.0 : 0.35);
        if this.isSelected {
            this.bg.SetTintColor(APM_ColorAccent());
            this.label.SetTintColor(APM_ColorDark());
        } else {
            this.bg.SetTintColor(this.isHovered && this.isEnabled ? APM_ColorButtonHover() : APM_ColorButton());
            this.label.SetTintColor(this.isMarked ? APM_ColorAccent() : APM_ColorText());
        }
        let i = 0;
        while i < ArraySize(this.icon) {
            this.icon[i].SetTintColor(this.isSelected ? APM_ColorDark() : (this.isMarked ? APM_ColorWarning() : APM_ColorText()));
            i += 1;
        }
    }
}

// --- Value row ---------------------------------------------------------------------------------------------------

public class APM_ValueRow extends IScriptable {
    public let attribute: Uint32;
    public let pairAttribute: Uint32; // face rows only: the pose set by negative values (0 = none)
    public let minValue: Float;
    public let maxValue: Float;
    public let step: Float;      // -/+ buttons
    public let dragScale: Float; // value change per unit of mouse movement
    public let unit: String;
    public let label: wref<inkText>;
    public let value: ref<APM_Button>;
    public let buttons: array<ref<APM_Button>>; // dec, inc, zero
}

public func APM_FaceRowsPerPage() -> Int32 { return 13; }

// --- Gizmo marker --------------------------------------------------------------------------------------------------

public class APM_Marker extends IScriptable {
    public let part: Int32;
    public let chain: Int32; // IK chain moved by dragging, or -1
    public let swivelChain: Int32; // IK chain whose elbow / knee this part turns while that chain is pinned, or -1
    public let canMove: Bool;      // Shift+drag moves it (hips, held props)
    public let isProp: Bool;       // point a held prop hangs from
    public let kind: Int32; // APM_MarkerKind*
    public let group: String;
    public let root: wref<inkCanvas>;
    public let dot: wref<inkRectangle>;
    public let halo: wref<inkRectangle>; // pinned hand / foot (IK on)
    public let line: wref<inkRectangle>;  // towards the point the bone points at (the eye look marker: from the left eye)
    public let line2: wref<inkRectangle>; // the eye look marker only: from the right eye
    public let parentLine: wref<inkRectangle>; // from the closest parent part with a gizmo (hips -> thighs)
    public let isHovered: Bool;
    public let screenX: Float; // last position on the ink canvas
    public let screenY: Float;
    public let depth: Float;    // distance from the camera while shown
    public let behindFade: Float; // 0..1: depth behind the nearest marker shown (drawn fainter)
    public let coverFade: Float;  // 0..1: covered on screen by a nearer marker (drawn much fainter)
}

// Classic gizmo (rotation rings / move arrows on the selected part): one hit square of a ring or an arrow.
public class APM_GizmoHandle extends IScriptable {
    public let axis: Int32;    // 0..2 the part's axes (bend forward / back, sideways, twist) or the model's X / Y / Z, 3 = view ring
    public let isArrow: Bool;
    public let segment: Int32; // ring segment, or point along the arrow
    public let root: wref<inkCanvas>;     // hit square (interactive)
    public let line: wref<inkRectangle>;  // visible ring segment / arrow shaft (line layer), null for the other squares
    public let isHovered: Bool;
    public let screenX: Float;
    public let screenY: Float;
    public let tangentX: Float; // screen movement per radian (rings) or per meter (arrows) of a drag
    public let tangentY: Float;
}

public func APM_ClassicOff() -> Int32 { return 0; }
public func APM_ClassicRotate() -> Int32 { return 1; }
public func APM_ClassicMove() -> Int32 { return 2; }
public func APM_ClassicRingSegments() -> Int32 { return 32; }
public func APM_ClassicArrowPoints() -> Int32 { return 6; }

public func APM_ColorAxis(axis: Int32) -> HDRColor {
    if axis == 0 {
        return APM_Color(1.0, 0.36, 0.36, 1.0);
    }
    if axis == 1 {
        return APM_Color(0.45, 1.0, 0.5, 1.0);
    }
    if axis == 2 {
        return APM_Color(0.42, 0.62, 1.0, 1.0);
    }
    return APM_Color(0.9, 0.9, 0.9, 1.0);
}

public func APM_MarkerKindBone() -> Int32 { return 0; }
public func APM_MarkerKindFinger() -> Int32 { return 1; }        // whole finger, at the knuckle
public func APM_MarkerKindFingerDetail() -> Int32 { return 2; }  // outer finger segment, close-up only
public func APM_MarkerKindEyeLook() -> Int32 { return 3; }       // where both eyes look

public func APM_DragRotate() -> Int32 { return 1; }    // the bone's end follows the cursor
public func APM_DragTrackball() -> Int32 { return 2; } // turns like a ball under the cursor (head)
public func APM_DragIkMove() -> Int32 { return 3; }    // the pinned hand / foot follows the cursor
public func APM_DragSwivel() -> Int32 { return 4; }    // the elbow / knee of a pinned limb follows the cursor
public func APM_DragTwist() -> Int32 { return 5; }     // rotation around the bone itself
public func APM_DragEyeLook() -> Int32 { return 6; }   // the eyes look at the cursor
public func APM_DragMove() -> Int32 { return 7; }      // the part (hips, held prop) follows the cursor

// The hips: V's root, moves the whole body.
public func APM_ColorRoot() -> HDRColor {
    return APM_Color(0.78, 0.48, 1.0, 1.0);
}

public func APM_ColorProp() -> HDRColor {
    return APM_Color(0.45, 1.0, 0.55, 1.0);
}

// World position of a body part (w = 1 when valid): the bone's model-space position through the animated
// component's local-to-world matrix (the puppet's entity transform is not where Photo Mode draws it).
public func APM_PartWorldPosition(toWorld: Matrix, part: Int32) -> Vector4 {
    let model = APM_GizmoGetPartPosition(part);
    if model.W < 0.5 {
        return APM_Vector(0.0, 0.0, 0.0, 0.0);
    }
    let world = Vector4.Transform(toWorld, APM_Vector(model.X, model.Y, model.Z, 1.0));
    return APM_Vector(world.X, world.Y, world.Z, 1.0);
}

// World direction -> model space of the animated component (projection on its axes).
public func APM_WorldToModelDirection(toWorld: Matrix, world: Vector4) -> Vector4 {
    let axisX = Vector4.TransformDir(toWorld, APM_Vector(1.0, 0.0, 0.0, 0.0));
    let axisY = Vector4.TransformDir(toWorld, APM_Vector(0.0, 1.0, 0.0, 0.0));
    let axisZ = Vector4.TransformDir(toWorld, APM_Vector(0.0, 0.0, 1.0, 0.0));
    let x = (world.X * axisX.X + world.Y * axisX.Y + world.Z * axisX.Z) / MaxF(axisX.X * axisX.X + axisX.Y * axisX.Y + axisX.Z * axisX.Z, 0.0001);
    let y = (world.X * axisY.X + world.Y * axisY.Y + world.Z * axisY.Z) / MaxF(axisY.X * axisY.X + axisY.Y * axisY.Y + axisY.Z * axisY.Z, 0.0001);
    let z = (world.X * axisZ.X + world.Y * axisZ.Y + world.Z * axisZ.Z) / MaxF(axisZ.X * axisZ.X + axisZ.Y * axisZ.Y + axisZ.Z * axisZ.Z, 0.0001);
    return APM_Vector(x, y, z, 0.0);
}

// World point -> model space of the animated component.
public func APM_WorldToModelPoint(toWorld: Matrix, world: Vector4) -> Vector4 {
    let origin = Vector4.Transform(toWorld, APM_Vector(0.0, 0.0, 0.0, 1.0));
    let model = APM_WorldToModelDirection(toWorld, APM_Vector(world.X - origin.X, world.Y - origin.Y, world.Z - origin.Z, 0.0));
    return APM_Vector(model.X, model.Y, model.Z, 1.0);
}

// --- Panel -------------------------------------------------------------------------------------------------------

public class APM_PoseEditor extends IScriptable {
    private let m_controller: wref<gameuiPhotoModeMenuController>;
    private let m_layer: wref<inkCanvas>;   // full-screen container, parent of everything below
    private let m_gizmoLayer: wref<inkCanvas>;
    private let m_raisedParts: array<Int32>; // markers last raised above the ones they cover, farthest first
    private let m_panel: wref<inkCanvas>;
    private let m_panelBg: wref<inkRectangle>;
    private let m_openButton: ref<APM_Button>;
    private let m_isOpen: Bool;
    private let m_opacityStep: Int32; // 0..3
    private let m_gizmosOn: Bool;
    private let m_panelHovered: Bool;

    private let m_buttons: array<ref<APM_Button>>;
    private let m_rows: array<ref<APM_ValueRow>>;
    private let m_groupNames: array<String>;
    private let m_groupButtons: array<ref<APM_Button>>;
    private let m_partButtons: array<ref<APM_Button>>;
    private let m_partIndices: array<Int32>;
    private let m_poseButtons: array<ref<APM_Button>>;
    private let m_poseLocks: array<wref<inkCanvas>>; // lock on a list entry: built-in, load only
    private let m_poseFolderIcons: array<wref<inkCanvas>>; // folder icon on a list entry that opens a folder
    private let m_poseUpIcons: array<wref<inkCanvas>>;     // arrow on the ".." entry (back to the parent folder)
    private let m_poseNames: array<String>; // entries of the open folder (see BuildEntries)
    private let m_libraryAll: array<String>; // every saved file of the library, "Folder/Name"
    private let m_libraryFolders: array<String>; // open folder per library (index LibraryKind())
    private let m_posePage: Int32;
    private let m_selectedPose: String;
    private let m_handLibrary: Bool; // Body mode: the list shows the hand poses (Hands tab) instead of the body poses
    private let m_libraryTabs: wref<inkCanvas>; // Body / Hands tabs of the list (Body mode)
    private let m_partTitle: wref<inkText>;
    private let m_posesTitle: wref<inkText>;
    private let m_posesPageText: wref<inkText>; // n/N between < and > under the list
    // Panel sections, stacked by Layout()
    private let m_sectionHeader: wref<inkCanvas>;
    private let m_sectionCharacter: wref<inkCanvas>; // character selector row (only while characters are added)
    private let m_sectionTools: wref<inkCanvas>;     // undo, redo, mirror, limits, auto IK, reset all
    private let m_sectionPart: wref<inkCanvas>;
    private let m_sectionRotation: wref<inkCanvas>;
    private let m_sectionMove: wref<inkCanvas>;
    private let m_sectionIk: wref<inkCanvas>;
    private let m_ikRows: wref<inkCanvas>;
    private let m_sectionPoses: wref<inkCanvas>;
    private let m_posesBody: wref<inkCanvas>;
    private let m_sectionItems: wref<inkCanvas>;
    private let m_sectionMode: wref<inkCanvas>;   // Body / Face: the panel shows the body sections or the face section
    private let m_faceMode: Bool;
    private let m_sectionFace: wref<inkCanvas>;   // face poses by zone and saved expressions
    private let m_faceBody: wref<inkCanvas>;
    private let m_faceInfo: wref<inkText>;
    private let m_faceMissing: wref<inkText>;     // shown when the face data could not be read
    private let m_faceZones: array<String>;       // zone tabs ("brows", "eyelids", ..., as in face/*.json)
    private let m_faceZone: Int32;                // tab shown
    private let m_faceLinked: Bool;               // L+R: one row moves the left and right pose
    private let m_faceUnlocked: Bool;             // face rows up to 400% instead of 100%
    private let m_faceRows: array<ref<APM_ValueRow>>; // every face row (both sides of the left / right ones)
    private let m_faceRowZones: array<String>;
    private let m_faceRowSides: array<String>;    // "l", "r" (a side of a left / right pose) or ""
    private let m_faceRowKeys: array<String>;     // label: translation key and English text
    private let m_faceRowTexts: array<String>;
    private let m_faceFooter: array<ref<APM_Button>>; // the page buttons, below the rows
    private let m_faceFooterOffsets: array<Float>;
    private let m_facePageText: wref<inkText>;
    private let m_faceRowPage: Int32;             // page of the zone's rows shown (APM_FaceRowsPerPage rows each)
    private let m_faceLayout: String;             // zone, page and L+R the rows were last placed for
    private let m_faceHeight: Float;
    private let m_posesOpen: Bool;
    private let m_poseListTimer: Float;
    // Item picker (search list next to the panel)
    private let m_picker: wref<inkCanvas>;
    private let m_help: wref<inkCanvas>; // shortcuts window
    private let m_helpOpen: Bool;
    private let m_pickerSearch: wref<inkText>;
    private let m_pickerTyping: Bool; // the search box has the keyboard (after a click on it)
    private let m_pickerPageText: wref<inkText>;
    private let m_pickerButtons: array<ref<APM_Button>>;
    private let m_pickerOpen: Bool;
    private let m_pickerQuery: String;
    private let m_pickerPage: Int32;
    private let m_pickerResults: array<Int32>; // indexes into m_itemIds
    private let m_itemIds: array<TweakDBID>;   // every item the picker offers, sorted by name
    private let m_itemNames: array<String>;
    private let m_itemSearch: array<String>;   // lower case names
    private let m_itemWeapon: array<Bool>;
    private let m_itemCategory: array<Int32>; // 1 weapon, 2 food & drink, 3 other
    private let m_pickerCategory: Int32;      // 0 all
    private let m_itemsBuilt: Bool;
    private let m_recentItems: array<TweakDBID>;
    private let m_noModelItems: array<Uint64>; // items that showed nothing in the hand (this session)
    private let m_checkTimer: Float;           // time left before checking that a placed item shows up
    private let m_checkRight: Bool;
    private let m_checkRecord: Uint64;
    // Tooltip: above the hovered button after a short wait, or a message above the last clicked button
    private let m_tip: ref<inkCanvas>;
    private let m_tipText: wref<inkText>;
    private let m_tipButton: wref<APM_Button>; // hovered button with a tooltip
    private let m_tipTimer: Float;
    private let m_tipShown: Bool;
    private let m_messageTimer: Float;
    private let m_lastButton: wref<APM_Button>;

    private let m_markers: array<ref<APM_Marker>>;
    private let m_classicMode: Int32;          // APM_Classic*: rotation rings / move arrows on the selected part
    private let m_classicModelAxes: Bool;      // the model's X / Y / Z instead of the part's own axes
    private let m_handles: array<ref<APM_GizmoHandle>>;
    private let m_dragHandle: ref<APM_GizmoHandle>;
    private let m_handleAxis: Vector4;         // world axis of the dragged ring / arrow
    private let m_hoverAxis: Int32;            // axis of the hovered ring / arrow (-1 none)
    private let m_arrowHeads: array<wref<inkRectangle>>;
    private let m_lineLayer: wref<inkCanvas>;
    private let m_canvasWidth: Float;
    private let m_canvasHeight: Float;
    private let m_hipsPart: Int32;
    private let m_autoIk: Bool;
    private let m_eyesTogether: Bool; // one look marker for both eyes (else one per eye) // dragging a hand / foot uses IK (else it rotates; Shift swaps)
    private let m_rootDot: wref<inkRectangle>;  // V's root (where Photo Mode placed her): saved hips positions are relative to it
    private let m_rootLine: wref<inkRectangle>;
    private let m_rootLabel: wref<inkText>;
    private let m_rootHit: wref<inkCanvas>;
    private let m_rootHovered: Bool;
    // Hovers that started while the camera was being moved (a mouse button already held) or while Photo Mode's UI is
    // hidden: they take effect only once the buttons are released / the UI is shown, so the camera keeps moving.
    private let m_waitMarker: ref<APM_Marker>;
    private let m_waitButton: ref<APM_Button>;
    private let m_waitPanel: Bool;
    private let m_waitRoot: Bool;
    private let m_ownsHover: Bool; // the camera ignores the mouse because of one of our widgets
    // Texts and tooltips set when the panel is built, translated again when Photo Mode opens (the translations may
    // not be loaded yet when the panel is built). L() queues a text until the widget showing it is created.
    private let m_pendingKeys: array<String>;
    private let m_pendingEnglish: array<String>;
    private let m_pendingShown: array<String>;
    private let m_locTexts: array<wref<inkText>>;
    private let m_locTextKeys: array<String>;
    private let m_locTextEnglish: array<String>;
    private let m_locTips: array<ref<APM_Button>>;
    private let m_locTipKeys: array<String>;
    private let m_locTipEnglish: array<String>;
    private let m_wasActive: Bool;
    // Characters (0 = V, 1-3 = added human characters): the one the panel works on, and the items each one holds.
    private let m_character: Int32;
    private let m_characterName: ref<inkText>;
    private let m_charShownRight: array<Uint64>;
    private let m_charShownLeft: array<Uint64>;
    private let m_charPoseItemRight: array<ItemID>;
    private let m_charPoseItemLeft: array<ItemID>;
    private let m_charHasPoseItemRight: array<Bool>;
    private let m_charHasPoseItemLeft: array<Bool>;
    private let m_charPuppets: array<wref<Entity>>;
    private let m_leftEyePart: Int32;
    private let m_rightEyePart: Int32;
    private let m_dragMarker: ref<APM_Marker>;
    private let m_dragMode: Int32;
    private let m_centerDrag: Bool; // Move arrows on: the selected part's marker (their center) is dragged freely
    private let m_dragSwivel: Float;      // elbow / knee direction (degrees) while dragging it
    private let m_lookTarget: Vector4;    // world point the eyes look at while dragging it
    private let m_eyeLookDistance: Float; // eased distance of the eyes' look marker (0 = not set yet)
    private let m_eyeLookPinned: Bool;    // the marker stays where it was dropped while the eyes keep that direction
    private let m_eyeLookPinForward: Vector4; // the eyes' direction then (model space; w = 0 until measured)
    private let m_shiftDown: Bool;        // modifier keys, from the latest input event
    private let m_controlDown: Bool;
    private let m_hint: wref<inkText>;    // what dragging the hovered marker does
    // IK drag: the hand / foot follows a virtual cursor on a plane facing the camera.
    private let m_dragCursorX: Float;
    private let m_dragCursorY: Float;
    private let m_dragDepth: Float;
    private let m_dragStartWorld: Vector4;
    private let m_dragStartOffset: Vector4; // IK offset in cm when the drag started
    // Real cursor tracking: screen position of the pointer and its scale to the ink canvas.
    private let m_cursorScaleX: Float;
    private let m_cursorScaleY: Float;
    private let m_lastPointer: Vector2;
    private let m_useRealCursor: Bool;

    private let m_dragRow: ref<APM_ValueRow>;
    private let m_dragValue: Float;
    private let m_pressedButtonId: String; // button the left mouse button went down on (a click needs press and release on it)
    private let m_clearPressedButton: Bool; // the button was released: forget the pressed button at the next update
    private let m_confirmResetAll: Bool;
    private let m_rightWasDown: Bool;
    private let m_confirmDelete: Bool;
    private let m_confirmOverwrite: Bool; // Overwrite asks for a second click, as Delete
    // Props: items put in V's hands
    private let m_propRight: Bool;            // the hand the picker works on
    private let m_shownRight: Uint64;         // items actually placed on the puppet
    private let m_shownLeft: Uint64;
    // Item the Photo Mode pose itself put in each hand (hand prop 0 = keep it, 1 = hand emptied on purpose)
    private let m_poseItemRight: ItemID;
    // Items placed on the player itself (Photo Mode in a vehicle, no puppet): the player keeps them after Photo Mode,
    // so its hands are given back as they were when leaving. Index 0 left, 1 right.
    private let m_playerHandTouched: array<Bool>;
    private let m_playerHandHad: array<Bool>;
    private let m_playerHandItem: array<ItemID>;
    private let m_playerGivenItems: array<ItemID>; // given to the player's inventory by the mod
    private let m_poseItemLeft: ItemID;
    private let m_hasPoseItemRight: Bool;
    private let m_hasPoseItemLeft: Bool;
    private let m_poseItemDelay: Float; // wait after placing before reading the slots again
    private let m_renaming: Bool;     // typing a pose name
    private let m_renameFrom: String; // pose being renamed
    private let m_refreshTimer: Float;

    public func Initialize(controller: ref<gameuiPhotoModeMenuController>) {
        this.m_controller = controller;
        let root = controller.GetRootCompoundWidget();
        if !IsDefined(root) {
            return;
        }

        this.m_opacityStep = Cast<Int32>(APM_PrefGet("OpacityStep", 1.0));
        this.m_gizmosOn = APM_PrefGet("Gizmos", 1.0) > 0.5;
        this.m_classicMode = Cast<Int32>(APM_PrefGet("ClassicGizmo", 0.0) + 0.5);
        this.m_classicModelAxes = APM_PrefGet("ClassicGizmoModelAxes", 0.0) > 0.5;
        this.m_hoverAxis = -1;
        this.m_propRight = true;
        this.m_posesOpen = APM_SessionGet("PosesOpen", 0.0) > 0.5; // closed when the game starts
        this.m_faceMode = APM_SessionGet("FaceMode", 0.0) > 0.5;
        this.m_faceZone = Cast<Int32>(APM_SessionGet("FaceZone", 4.0) + 0.5); // lips
        this.m_faceLinked = APM_PrefGet("FaceLinked", 1.0) > 0.5;
        APM_FaceSetLinked(this.m_faceLinked);
        this.m_faceUnlocked = APM_PrefGet("FaceUnlocked", 0.0) > 0.5;
        APM_FaceSetUnlocked(this.m_faceUnlocked);
        this.m_autoIk = APM_SessionGet("AutoIk", 1.0) > 0.5;
        this.m_eyesTogether = APM_SessionGet("EyesTogether", 1.0) > 0.5;

        let layer = new inkCanvas();
        layer.SetName(n"APM_PoseEditorLayer");
        layer.SetAnchor(inkEAnchor.Fill);
        layer.Reparent(root);
        this.m_layer = layer;

        // Gizmos below the panel
        let gizmoLayer = new inkCanvas();
        gizmoLayer.SetName(n"APM_GizmoLayer");
        gizmoLayer.SetAnchor(inkEAnchor.TopLeft);
        gizmoLayer.SetSize(APM_ScreenWidth(), APM_ScreenHeight());
        gizmoLayer.Reparent(layer);
        this.m_gizmoLayer = gizmoLayer;
        this.BuildMarkers();
        this.BuildClassicGizmo();
        this.BuildHint();

        this.m_openButton = this.CreateButton(layer, "open", this.L("APM-Button-PoseEditor", "Pose editor"), 0.0, 0.0, 160.0, 32.0);
        this.BuildPanel();
        this.SetOpen(false);
        this.ApplyDock();
        this.ApplyOpacity();

        controller.RegisterToGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
        controller.RegisterToGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
        controller.RegisterToGlobalInputCallback(n"OnPostOnPress", this, n"OnGlobalPress");
        // Texts queued but not shown by a widget (none expected).
        if ArraySize(this.m_pendingKeys) > 0 {
            APM_LogInfo("Texts: " + IntToString(ArraySize(this.m_pendingKeys)) + " not registered, first " + this.m_pendingKeys[0]);
        }
        ArrayClear(this.m_pendingKeys);
        ArrayClear(this.m_pendingEnglish);
        ArrayClear(this.m_pendingShown);
    }

    public func Uninitialize() {
        APM_SetWheelCapture(false);
        if this.m_pickerTyping {
            APM_TextEntryEnd();
            this.m_pickerTyping = false;
        }
        this.m_pickerOpen = false;
        if this.m_renaming {
            APM_TextEntryEnd();
            this.m_renaming = false;
        }
        if IsDefined(this.m_controller) {
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnPress", this, n"OnGlobalPress");
        }
        this.m_dragRow = null;
        this.m_dragMarker = null;
    }

    // The gizmo layers cover the whole screen: they follow the screen size (resolution or window changes).
    private func UpdateCanvasSize(active: Bool) {
        let width = APM_ScreenWidth();
        let height = APM_ScreenHeight();
        if AbsF(width - this.m_canvasWidth) < 0.5 && AbsF(height - this.m_canvasHeight) < 0.5 {
            return;
        }
        this.m_canvasWidth = width;
        this.m_canvasHeight = height;
        if IsDefined(this.m_gizmoLayer) {
            this.m_gizmoLayer.SetSize(width, height);
        }
        if IsDefined(this.m_lineLayer) {
            this.m_lineLayer.SetSize(width, height);
        }
        let size = APM_GetViewportSize();
        let camera = GameInstance.GetCameraSystem(GetGameInstance());
        let cameraAspect = IsDefined(camera) ? camera.GetAspectRatio() : 0.0;
        APM_LogInfo("Screen " + FloatToStringPrec(size.X, 0) + "x" + FloatToStringPrec(size.Y, 0) + ": gizmo canvas "
            + FloatToStringPrec(width, 0) + "x" + FloatToStringPrec(height, 0) + ", camera aspect "
            + FloatToStringPrec(cameraAspect, 3));
    }

    public func Update(timeDelta: Float) {
        let active = APM_IsPhotoModeActive();
        if active && !this.m_wasActive {
            this.Retranslate();
            this.KeepLayerOnTop(true);
        } else if active {
            this.KeepLayerOnTop(false);
        }
        if active {
            this.KeepLayerVisible();
        }
        this.m_wasActive = active;
        if active {
            this.FollowSelectedCharacter();
        }
        this.UpdateCanvasSize(active);
        this.UpdatePoseList(timeDelta);
        this.UpdateWaitingHover();
        this.UpdateTooltip(timeDelta);
        if active {
            APM_SetPlayerEntity(this.m_controller.GetPlayerControlledObject()); // posed as V in a vehicle (no puppet)
        }
        this.PublishCamera();
        if this.m_clearPressedButton {
            this.m_clearPressedButton = false;
            this.m_pressedButtonId = "";
        }
        this.UpdateMarkers();
        this.UpdateRename();
        this.SyncProps(timeDelta);
        this.UpdatePicker();
        this.UpdateDragWheel();
        this.UpdateRightClick();
        if APM_IsPhotoModeActive() && !this.m_renaming && !this.m_pickerTyping {
            let hotkey = APM_PollHotkey();
            if hotkey == 1 {
                APM_Undo();
                this.Refresh();
            } else if hotkey == 2 {
                APM_Redo();
                this.Refresh();
            } else if hotkey >= 3 && hotkey <= 5 {
                this.SetClassicMode(hotkey == 3 ? APM_ClassicOff() : (hotkey == 4 ? APM_ClassicMove() : APM_ClassicRotate()));
                this.Refresh();
            } else if hotkey == 6 {
                this.ToggleClassicAxes();
                this.Refresh();
            }
        }
        if !this.m_isOpen {
            return;
        }
        this.m_refreshTimer -= timeDelta;
        if this.m_refreshTimer <= 0.0 && !IsDefined(this.m_dragRow) {
            this.m_refreshTimer = 0.25;
            this.Refresh(); // picks up changes made by the gizmos
        }
    }

    // --- Construction ------------------------------------------------------------------------------------------------

    private func BuildPanel() {
        let s = APM_UiScale();
        let width = APM_PanelWidth();

        let panel = new inkCanvas();
        panel.SetName(n"APM_PoseEditor");
        panel.SetSize(width * s, APM_PanelHeight() * s);
        panel.SetInteractive(true);
        panel.Reparent(this.m_layer);
        panel.RegisterToCallback(n"OnHoverOver", this, n"OnPanelHoverOver");
        panel.RegisterToCallback(n"OnHoverOut", this, n"OnPanelHoverOut");
        this.m_panel = panel;

        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.SetTintColor(APM_ColorDark());
        bg.SetInteractive(true);
        bg.Reparent(panel);
        this.m_panelBg = bg;

        let accent = new inkRectangle();
        accent.SetAnchor(inkEAnchor.TopFillHorizontaly);
        accent.SetSize(width * s, 3.0 * s);
        accent.SetTintColor(APM_ColorAccent());
        accent.Reparent(panel);

        // Header: switches, then the tools used all the time
        let header = this.CreateSection(n"APM_Header");
        this.m_sectionHeader = header;
        if APM_IsReady() {
            this.CreateText(header, this.L("APM-Title", "Advanced pose"), 16.0, 8.0, 286.0, 32.0, 20, APM_ColorAccent());
        } else {
            // The game was updated and the mod could not hook into it: nothing would work.
            this.CreateText(header, this.L("APM-Msg-UnsupportedVersion", "Unsupported game version - see the log"), 16.0, 10.0, 286.0, 32.0, 13, APM_ColorWarning());
        }
        let enabledButton = this.CreateButton(header, "enabled", this.L("APM-Button-PoseOff", "Pose off"), 308.0, 8.0, 86.0, 32.0);
        this.TipL(enabledButton, "APM-Tip-PoseOnOff", "Turn your edits on V on / off");
        let gizmosButton = this.CreateButton(header, "gizmos", this.L("APM-Button-Gizmos", "Gizmos"), 398.0, 8.0, 86.0, 32.0);
        this.TipL(gizmosButton, "APM-Tip-Gizmos", "Show / hide the markers on V");
        let opacityButton = this.CreateButton(header, "opacity", "100%", 488.0, 8.0, 48.0, 32.0);
        this.TipL(opacityButton, "APM-Tip-Opacity", "Panel opacity");
        this.CreateIconButton(header, "help", "circle-help", 540.0, 8.0, this.L("APM-Tip-Help", "Info: markers and shortcuts"), 32.0);
        this.CreateIconButton(header, "hide", "x", 584.0, 8.0, this.L("APM-Tip-HidePanel", "Hide the panel"), 32.0);
        // Character selector: V and the characters added in the scene (its own row, shown only when there are some)
        let characterRow = this.CreateSection(n"APM_Character");
        this.m_sectionCharacter = characterRow;
        let charPrev = this.CreateButton(characterRow, "char:prev", "<", 16.0, 4.0, 56.0, 38.0);
        charPrev.label.SetFontSize(Cast<Int32>(24.0 * APM_UiScale()));
        this.TipL(charPrev, "APM-Tip-CharacterPrev", "Previous character to pose");
        // The name fills a fixed box, so it stays centred between the arrows whatever its length.
        let charNameBox = new inkCanvas();
        charNameBox.SetAnchor(inkEAnchor.TopLeft);
        charNameBox.SetMargin(80.0 * APM_UiScale(), 4.0 * APM_UiScale(), 0.0, 0.0);
        charNameBox.SetSize((APM_PanelWidth() - 160.0) * APM_UiScale(), 38.0 * APM_UiScale());
        charNameBox.Reparent(characterRow);
        let charName = this.CreateText(charNameBox, "V", 0.0, 0.0, APM_PanelWidth() - 160.0, 38.0, 22, APM_ColorText());
        charName.SetAnchor(inkEAnchor.Fill);
        charName.SetMargin(0.0, 0.0, 0.0, 0.0);
        charName.SetHorizontalAlignment(textHorizontalAlignment.Center);
        charName.SetVerticalAlignment(textVerticalAlignment.Center);
        charName.SetLetterCase(textLetterCase.OriginalCase);
        this.m_characterName = charName;
        let charNext = this.CreateButton(characterRow, "char:next", ">", APM_PanelWidth() - 72.0, 4.0, 56.0, 38.0);
        charNext.label.SetFontSize(Cast<Int32>(24.0 * APM_UiScale()));
        this.TipL(charNext, "APM-Tip-CharacterNext", "Next character to pose");

        // Tools used all the time
        let tools = this.CreateSection(n"APM_Tools");
        this.m_sectionTools = tools;
        this.CreateIconButton(tools, "undo", "undo-2", 16.0, 4.0, this.L("APM-Tip-Undo", "Undo the last change of this character (Ctrl+Z)"));
        this.CreateIconButton(tools, "redo", "redo-2", 60.0, 4.0, this.L("APM-Tip-Redo", "Redo the last undone change of this character (Ctrl+Y)"));
        this.CreateIconButton(tools, "mirror", "flip-vertical-2", 116.0, 4.0, this.L("APM-Tip-Mirror", "Mirror: while on, the limb you edit is copied to the other side and followed"));
        this.CreateIconButton(tools, "limits:on", "lock", 160.0, 4.0, this.L("APM-Tip-LimitsOn", "Joint limits on: rotations stay natural. Click to unlock"));
        this.CreateIconButton(tools, "limits:off", "lock-open", 160.0, 4.0, this.L("APM-Tip-LimitsOff", "Joint limits off: any rotation is allowed. Click to lock"));
        this.CreateIconButton(tools, "autoik", "magnet", 204.0, 4.0, this.L("APM-Tip-AutoIk", "Auto IK: dragging a hand / foot moves it with IK and pins it (Shift rotates it). Off: dragging rotates it, and IK is used only on limbs pinned in the IK row"));
        // Gizmo mode (as the tool bars of 3D programs): Direct (drag the markers), Move (arrows), Rotate (rings); then
        // the axes of the arrows / rings: Local (cube) or Global (globe).
        this.CreateIconButton(tools, "classic:0", "mouse-pointer-2", 268.0, 4.0, this.L("APM-Tip-GizmoDirect", "Direct (Ctrl+1): drag the markers (rotate, move with IK, Shift / Ctrl / Alt for the other ways)"));
        this.CreateIconButton(tools, "classic:2", "move", 312.0, 4.0, this.L("APM-Tip-GizmoMove", "Move (Ctrl+2): arrows on the selected part (hips, items, hands / feet with IK); drag its center to move it freely. Markers only select"));
        this.CreateIconButton(tools, "classic:1", "rotate-3d", 356.0, 4.0, this.L("APM-Tip-GizmoRotate", "Rotate (Ctrl+3): rings on the selected part. Markers only select"));
        this.CreateIconButton(tools, "axes:local", "box", 412.0, 4.0, this.L("APM-Tip-AxesLocal", "Axes (Ctrl+4): Local, the part's own (they turn with it). Click for Global"));
        this.CreateIconButton(tools, "axes:global", "globe", 412.0, 4.0, this.L("APM-Tip-AxesGlobal", "Axes (Ctrl+4): Global, the character's (fixed). Click for Local"));
        this.CreateIconButton(tools, "resetall", "trash-2", 580.0, 4.0, ""); // tooltip in Refresh: Body or Face

        // Body / Face: the panel shows the body sections below, or the face section
        let modeRow = this.CreateSection(n"APM_Mode");
        this.m_sectionMode = modeRow;
        let bodyMode = this.CreateButton(modeRow, "mode:body", this.L("APM-Button-ModeBody", "Body"), 16.0, 4.0, 302.0, 32.0);
        this.TipL(bodyMode, "APM-Tip-ModeBody", "Pose the body: parts, IK, poses, items");
        let faceMode = this.CreateButton(modeRow, "mode:face", this.L("APM-Button-ModeFace", "Face"), 322.0, 4.0, 302.0, 32.0);
        this.TipL(faceMode, "APM-Tip-ModeFace", "Expression editor: the Photo Mode expression, pose by pose");

        // Body part: group tabs (two rows of four), parts of the group (two rows of three)
        let partSection = this.CreateSection(n"APM_Part");
        this.m_sectionPart = partSection;
        this.AddSeparator(partSection);
        let count = APM_UiGetOptionCount(7701u);
        let i = 0;
        while i < count {
            let group = APM_UiGetOptionGroup(7701u, i);
            if !ArrayContains(this.m_groupNames, group) {
                ArrayPush(this.m_groupNames, group);
            }
            i += 1;
        }
        i = 0;
        while i < ArraySize(this.m_groupNames) {
            let x = 16.0 + Cast<Float>(i % 4) * 152.0;
            let y = 10.0 + Cast<Float>(i / 4) * 36.0;
            let groupButton = this.CreateButton(partSection, "group:" + IntToString(i), this.LGroup(this.m_groupNames[i]), x, y, 146.0, 32.0);
            this.AddDot(groupButton);
            ArrayPush(this.m_groupButtons, groupButton);
            i += 1;
        }
        i = 0;
        while i < 6 {
            let x = 16.0 + Cast<Float>(i % 3) * 203.0;
            let y = 86.0 + Cast<Float>(i / 3) * 36.0;
            let partButton = this.CreateButton(partSection, "part:" + IntToString(i), "", x, y, 197.0, 32.0);
            this.AddDot(partButton);
            ArrayPush(this.m_partButtons, partButton);
            ArrayPush(this.m_partIndices, -1);
            i += 1;
        }
        // Rotation of the selected part
        let rotation = this.CreateSection(n"APM_Rotation");
        this.m_sectionRotation = rotation;
        this.AddSeparator(rotation);
        this.m_partTitle = this.CreateText(rotation, "", 16.0, 6.0, 360.0, 26.0, 18, APM_ColorAccent());
        let partReset = this.CreateSmallButton(rotation, "partreset", this.L("APM-Button-Reset", "Reset"), 530.0, 4.0, 92.0, 26.0);
        this.TipL(partReset, "APM-Tip-PartReset", "Undo every edit of this part (an IK pin goes back to the pose's place too)");
        let eyesBoth = this.CreateSmallButton(rotation, "eyes:0", this.L("APM-Button-EyesBoth", "Both"), 380.0, 4.0, 78.0, 26.0);
        this.TipL(eyesBoth, "APM-Tip-EyesBoth", "Both eyes together: one look marker");
        let eyesLeft = this.CreateSmallButton(rotation, "eyes:1", this.L("APM-Button-EyeLeft", "Left"), 462.0, 4.0, 78.0, 26.0);
        this.TipL(eyesLeft, "APM-Tip-EyeLeft", "Left eye on its own (one marker per eye)");
        let eyesRight = this.CreateSmallButton(rotation, "eyes:2", this.L("APM-Button-EyeRight", "Right"), 544.0, 4.0, 78.0, 26.0);
        this.TipL(eyesRight, "APM-Tip-EyeRight", "Right eye on its own (one marker per eye)");
        this.CreateRow(rotation, 7702u, this.L("APM-Row-BendForwardBack", "Bend forward / back"), 34.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(rotation, 7703u, this.L("APM-Row-BendSideways", "Bend sideways"), 70.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(rotation, 7704u, this.L("APM-Row-Twist", "Twist"), 106.0, -180.0, 180.0, 1.0, 0.2, "");

        // Position (only parts that can move: hips, held items)
        let move = this.CreateSection(n"APM_Move");
        this.m_sectionMove = move;
        this.AddSeparator(move);
        this.CreateText(move, this.L("APM-Title-Move", "Move (cm)"), 16.0, 6.0, 300.0, 26.0, 18, APM_ColorAccent());
        this.CreateRow(move, 7705u, this.L("APM-Row-LeftRight", "Left / right"), 34.0, -1000.0, 1000.0, 0.5, 0.05, " cm");
        this.CreateRow(move, 7706u, this.L("APM-Row-ForwardBack", "Forward / back"), 70.0, -1000.0, 1000.0, 0.5, 0.05, " cm");
        this.CreateRow(move, 7707u, this.L("APM-Row-UpDown", "Up / down"), 106.0, -1000.0, 1000.0, 0.5, 0.05, " cm");

        // IK: limb + pin; the rows only while that limb is pinned
        let ik = this.CreateSection(n"APM_Ik");
        this.m_sectionIk = ik;
        this.AddSeparator(ik);
        this.CreateText(ik, this.L("APM-Title-Ik", "IK"), 16.0, 8.0, 40.0, 30.0, 18, APM_ColorAccent());
        let limbs = APM_UiGetOptionCount(7710u);
        i = 0;
        while i < limbs {
            let limbButton = this.CreateButton(ik, "ik:" + IntToString(i), this.LPart(APM_UiGetOptionLabel(7710u, i)), 60.0 + Cast<Float>(i) * 142.0, 8.0, 138.0, 30.0);
            this.TipL(limbButton, "APM-Tip-IkLimb", "IK limb: the frame lights up while it is pinned");
            this.AddOutline(limbButton, APM_ColorIk());
            i += 1;
        }
        // Pin of the selected limb, on its own row
        let pinOff = this.CreateSmallButton(ik, "pin:0", this.L("APM-Button-PinOff", "Off"), 60.0, 44.0, 180.0, 28.0);
        this.TipL(pinOff, "APM-Tip-PinOff", "Pin off: the hand / foot follows the arm / leg rotations");
        let pinWorld = this.CreateSmallButton(ik, "pin:1", this.L("APM-Button-PinWorld", "World"), 246.0, 44.0, 180.0, 28.0);
        this.TipL(pinWorld, "APM-Tip-PinWorld", "Pin world: the hand / foot stays in its place in the scene, whatever the body does");
        let pinBody = this.CreateSmallButton(ik, "pin:2", this.L("APM-Button-PinBody", "Body"), 432.0, 44.0, 180.0, 28.0);
        this.TipL(pinBody, "APM-Tip-PinBody", "Pin body: the hand / foot keeps its place relative to the shoulder / hips and moves with them");
        let ikRows = new inkCanvas();
        ikRows.SetName(n"APM_IkRows");
        ikRows.SetAnchor(inkEAnchor.TopLeft);
        ikRows.SetMargin(0.0, 80.0 * s, 0.0, 0.0);
        ikRows.SetSize(width * s, 148.0 * s);
        ikRows.Reparent(ik);
        this.m_ikRows = ikRows;
        this.CreateRow(ikRows, 7712u, this.L("APM-Row-RightLeft", "Right / left"), 0.0, -1000.0, 1000.0, 0.5, 0.05, " cm");
        this.CreateRow(ikRows, 7713u, this.L("APM-Row-ForwardBack", "Forward / back"), 36.0, -1000.0, 1000.0, 0.5, 0.05, " cm");
        this.CreateRow(ikRows, 7714u, this.L("APM-Row-UpDown", "Up / down"), 72.0, -1000.0, 1000.0, 0.5, 0.05, " cm");
        this.CreateRow(ikRows, 7715u, this.L("APM-Row-ElbowKnee", "Elbow / knee"), 108.0, -180.0, 180.0, 1.0, 0.2, "");

        // Pose library (folds)
        let poses = this.CreateSection(n"APM_Poses");
        this.m_sectionPoses = poses;
        this.AddSeparator(poses);
        this.m_posesTitle = this.CreateText(poses, this.L("APM-Title-Poses", "Poses"), 16.0, 6.0, 510.0, 28.0, 16, APM_ColorAccent());
        this.CreateIconButton(poses, "poses:folder", "folder-open", 538.0, 6.0, this.L("APM-Tip-PosesFolder", "Open the folder of the saved poses (to share them; in full screen the game is minimized)"));
        this.CreateIconButton(poses, "poses:open", "chevron-down", 584.0, 6.0, this.L("APM-Tip-PosesShow", "Show the saved poses"));
        this.CreateIconButton(poses, "poses:close", "chevron-up", 584.0, 6.0, this.L("APM-Tip-PosesHide", "Hide the saved poses"));
        let libraryTabs = new inkCanvas();
        libraryTabs.SetName(n"APM_LibraryTabs");
        libraryTabs.SetAnchor(inkEAnchor.TopLeft);
        libraryTabs.SetMargin(0.0, 40.0 * s, 0.0, 0.0);
        libraryTabs.SetSize(width * s, 36.0 * s);
        libraryTabs.Reparent(poses);
        this.m_libraryTabs = libraryTabs;
        let bodyTab = this.CreateButton(libraryTabs, "lib:body", this.L("APM-Button-LibraryBody", "Body"), 16.0, 0.0, 300.0, 30.0);
        this.TipL(bodyTab, "APM-Tip-LibraryBody", "Body poses: the T and A poses, then the saved ones");
        let handsTab = this.CreateButton(libraryTabs, "lib:hands", this.L("APM-Button-HandPoses", "Hands"), 324.0, 0.0, 300.0, 30.0);
        this.TipL(handsTab, "APM-Tip-HandPoses", "Hand poses (palm and fingers): the presets, then the saved ones; one pose fits either hand, applied to the hand of the open tab");
        let posesBody = new inkCanvas();
        posesBody.SetName(n"APM_PosesBody");
        posesBody.SetAnchor(inkEAnchor.TopLeft);
        posesBody.SetMargin(0.0, 40.0 * s, 0.0, 0.0);
        posesBody.SetSize(width * s, 208.0 * s);
        posesBody.Reparent(poses);
        this.m_posesBody = posesBody;
        i = 0;
        while i < APM_PosesPerPage() {
            let poseButton = this.CreateButton(posesBody, "pose:" + IntToString(i), "", 16.0, Cast<Float>(i) * 34.0, 420.0, 30.0);
            poseButton.label.SetLetterCase(textLetterCase.OriginalCase);
            ArrayPush(this.m_poseButtons, poseButton);
            ArrayPush(this.m_poseLocks, this.AddRowIcon(poseButton, "lock", false));
            ArrayPush(this.m_poseFolderIcons, this.AddRowIcon(poseButton, "folder", true));
            ArrayPush(this.m_poseUpIcons, this.AddRowIcon(poseButton, "arrow-left", true));
            i += 1;
        }
        this.CreateButton(posesBody, "savenew", this.L("APM-Button-SaveNew", "Save new"), 446.0, 0.0, 178.0, 30.0);
        this.CreateButton(posesBody, "overwrite", this.L("APM-Button-Overwrite", "Overwrite"), 446.0, 34.0, 178.0, 30.0);
        this.CreateButton(posesBody, "load", this.L("APM-Button-Load", "Load"), 446.0, 68.0, 86.0, 30.0);
        this.CreateButton(posesBody, "rename", this.L("APM-Button-Rename", "Rename"), 538.0, 68.0, 86.0, 30.0);
        this.CreateButton(posesBody, "delete", this.L("APM-Button-Delete", "Delete"), 446.0, 102.0, 178.0, 30.0);
        // Pages under the list, as in the face rows: < n/N >
        this.CreateButton(posesBody, "pageprev", "<", 16.0, 170.0, 86.0, 30.0);
        this.m_posesPageText = this.CreateText(posesBody, "", 270.0, 168.0, 100.0, 34.0, 22, APM_ColorAccent());
        this.m_posesPageText.SetHorizontalAlignment(textHorizontalAlignment.Center);
        this.CreateButton(posesBody, "pagenext", ">", 538.0, 170.0, 86.0, 30.0);

        // Item in hand
        let items = this.CreateSection(n"APM_Items");
        this.m_sectionItems = items;
        this.AddSeparator(items);
        this.CreateText(items, this.L("APM-Title-InHand", "In hand"), 16.0, 8.0, 100.0, 30.0, 16, APM_ColorAccent());
        this.CreateText(items, this.L("APM-Label-RightHand", "Right"), 116.0, 8.0, 60.0, 30.0, 14, APM_ColorDimText());
        this.CreateButton(items, "prop:open:r", "", 176.0, 8.0, 356.0, 30.0);
        this.CreateIconButton(items, "prop:restore:r", "rotate-ccw", 538.0, 9.0, this.L("APM-Tip-ItemRestore", "Give back the item of the Photo Mode pose"));
        this.CreateIconButton(items, "prop:clear:r", "x", 584.0, 9.0, this.L("APM-Tip-ItemClearRight", "Empty the right hand"));
        this.CreateText(items, this.L("APM-Label-LeftHand", "Left"), 116.0, 44.0, 60.0, 30.0, 14, APM_ColorDimText());
        this.CreateButton(items, "prop:open:l", "", 176.0, 44.0, 356.0, 30.0);
        this.CreateIconButton(items, "prop:restore:l", "rotate-ccw", 538.0, 45.0, this.L("APM-Tip-ItemRestore", "Give back the item of the Photo Mode pose"));
        this.CreateIconButton(items, "prop:clear:l", "x", 584.0, 45.0, this.L("APM-Tip-ItemClearLeft", "Empty the left hand"));

        // Face (the panel's Face mode): the Photo Mode expression as the game's face poses, one zone tab at a time (a row
        // moved sets that pose; Reset gives the Photo Mode value back), and the saved expressions. Rows and the buttons below them are placed by LayoutFace().
        let face = this.CreateSection(n"APM_Face");
        this.m_sectionFace = face;
        this.AddSeparator(face);
        this.CreateText(face, this.L("APM-Title-Face", "Face"), 16.0, 6.0, 70.0, 28.0, 16, APM_ColorAccent());
        this.m_faceInfo = this.CreateText(face, "", 84.0, 8.0, 402.0, 26.0, 13, APM_ColorDimText());
        this.CreateIconButton(face, "face:unlock", "lock", 492.0, 6.0, this.L("APM-Tip-FaceUnlock", "Face rows stop at 100% (as the game's expressions). Click to allow up to 400%"), 28.0);
        this.CreateIconButton(face, "face:lock", "lock-open", 492.0, 6.0, this.L("APM-Tip-FaceLock", "Face rows go up to 400%: above 100% the face can deform. Click to stop at 100% again"), 28.0);
        let linkedButton = this.CreateSmallButton(face, "face:linked", this.L("APM-Button-FaceLinked", "L+R"), 538.0, 6.0, 86.0, 28.0);
        this.TipL(linkedButton, "APM-Tip-FaceLinked", "L+R: one row moves the left and right pose together. Off: a row for each side");
        let faceBody = new inkCanvas();
        faceBody.SetName(n"APM_FaceBody");
        faceBody.SetAnchor(inkEAnchor.TopLeft);
        faceBody.SetMargin(0.0, 40.0 * s, 0.0, 0.0);
        faceBody.SetSize(width * s, 1200.0 * s);
        faceBody.Reparent(face);
        this.m_faceBody = faceBody;
        let table = APM_BuildFaceRowTable();
        this.m_faceZones = table.zones;
        this.m_faceZone = Clamp(this.m_faceZone, 0, ArraySize(this.m_faceZones) - 1);
        let z = 0;
        while z < ArraySize(table.zones) {
            this.AddDot(this.CreateButton(faceBody, "facezone:" + table.zones[z], this.L(table.zoneKeys[z], table.zoneTexts[z]), 16.0 + Cast<Float>(z % 4) * 154.0, Cast<Float>(z / 4) * 36.0, 148.0, 32.0));
            z += 1;
        }
        this.m_faceMissing = this.CreateText(faceBody, this.L("APM-Msg-FaceDataMissing", "Face data not found: reinstall the mod (red4ext\\plugins\\AdvancedPoseMod\\face)"), 16.0, 76.0, 608.0, 32.0, 14, APM_ColorWarning());
        let r = 0;
        while r < ArraySize(table.poses) {
            let pose = this.FacePoseIndex(table.poses[r]);
            let negative = StrLen(table.negatives[r]) > 0 ? this.FacePoseIndex(table.negatives[r]) : -1;
            if pose >= 0 && (StrLen(table.negatives[r]) == 0 || negative >= 0) {
                let text = this.L(table.keys[r], table.texts[r]);
                let mirror = APM_FacePoseMirror(pose);
                if Equals(APM_FacePoseSide(pose), "l") && mirror >= 0 {
                    this.AddFaceRow(faceBody, pose, negative, "l", table.keys[r], table.texts[r], text);
                    this.AddFaceRow(faceBody, mirror, negative >= 0 ? APM_FacePoseMirror(negative) : -1, "r", table.keys[r], table.texts[r], text);
                } else {
                    this.AddFaceRow(faceBody, pose, negative, "", table.keys[r], table.texts[r], text);
                }
            }
            r += 1;
        }

        // Below the rows: pages of the zone's rows, < n/N > (the saved expressions are in the pose list)
        this.AddFaceFooter(this.CreateButton(faceBody, "facepage:prev", "<", 16.0, 0.0, 86.0, 30.0), 0.0);
        this.m_facePageText = this.CreateText(faceBody, "", 270.0, -2.0, 100.0, 34.0, 22, APM_ColorAccent());
        this.m_facePageText.SetHorizontalAlignment(textHorizontalAlignment.Center);
        this.AddFaceFooter(this.CreateButton(faceBody, "facepage:next", ">", 538.0, 0.0, 86.0, 30.0), 0.0);

        this.BuildPicker();
        this.BuildHelp();
        this.BuildTooltip(); // last: drawn above the panel, the item list and the help
        this.ReloadPoses();
        this.Layout();
    }

    // Stacks the visible sections and sizes the panel to them.
    private func Layout() {
        if !IsDefined(this.m_panel) {
            return;
        }
        let s = APM_UiScale();
        let y = 0.0;
        y = this.PlaceSection(this.m_sectionHeader, y, 42.0, true);
        y = this.PlaceSection(this.m_sectionCharacter, y, 46.0, this.HasAddedCharacters());
        y = this.PlaceSection(this.m_sectionTools, y, 38.0, true);
        y = this.PlaceSection(this.m_sectionMode, y, 40.0, true);
        let body = !this.m_faceMode;
        y = this.PlaceSection(this.m_sectionPart, y, 160.0, body);
        y = this.PlaceSection(this.m_sectionRotation, y, 144.0, body);
        y = this.PlaceSection(this.m_sectionMove, y, 144.0, body && APM_UiIsAttributeEnabled(7705u));
        let pinned = APM_UiGetValue(7711u) > 0.5;
        this.m_ikRows.SetVisible(pinned);
        y = this.PlaceSection(this.m_sectionIk, y, pinned ? 230.0 : 82.0, body);
        // The list: body / hand poses under the IK in Body mode, the saved expressions under the face in Face mode.
        let tabs = body && this.m_posesOpen;
        this.m_libraryTabs.SetVisible(tabs);
        this.m_posesBody.SetVisible(this.m_posesOpen);
        this.m_posesBody.SetMargin(0.0, (tabs ? 76.0 : 40.0) * s, 0.0, 0.0);
        let libraryHeight = (this.m_posesOpen ? 252.0 : 40.0) + (tabs ? 36.0 : 0.0);
        if body {
            y = this.PlaceSection(this.m_sectionPoses, y, libraryHeight, true);
        }
        y = this.PlaceSection(this.m_sectionItems, y, 82.0, body);
        if this.m_faceMode {
            this.LayoutFace();
        }
        y = this.PlaceSection(this.m_sectionFace, y, this.m_faceHeight, this.m_faceMode);
        if this.m_faceMode {
            y = this.PlaceSection(this.m_sectionPoses, y, libraryHeight, true);
        }
        y += 8.0;
        this.m_panel.SetSize(APM_PanelWidth() * s, y * s);
    }

    // Hand the preset row works on: 1 left, 2 right, 0 none (shown in the hand tabs and for the arm tab's hand).
    private func PresetHand() -> Int32 {
        let index = Cast<Int32>(APM_UiGetValue(7701u));
        let group = APM_UiGetOptionGroup(7701u, index);
        let label = APM_UiGetOptionLabel(7701u, index);
        if Equals(group, "Right hand") || Equals(label, "Right hand") {
            return 2;
        }
        if Equals(group, "Left hand") || Equals(label, "Left hand") {
            return 1;
        }
        return 0;
    }

    private func PlaceSection(section: wref<inkCanvas>, y: Float, height: Float, visible: Bool) -> Float {
        let s = APM_UiScale();
        section.SetVisible(visible);
        if !visible {
            return y;
        }
        section.SetMargin(0.0, y * s, 0.0, 0.0);
        section.SetSize(APM_PanelWidth() * s, height * s);
        return y + height;
    }

    // Search list for the item in hand, shown beside the panel.
    private func BuildPicker() {
        let s = APM_UiScale();
        let picker = new inkCanvas();
        picker.SetName(n"APM_ItemPicker");
        picker.SetSize(460.0 * s, 520.0 * s);
        picker.SetInteractive(true);
        picker.SetVisible(false);
        picker.Reparent(this.m_layer);
        picker.RegisterToCallback(n"OnHoverOver", this, n"OnPanelHoverOver");
        picker.RegisterToCallback(n"OnHoverOut", this, n"OnPanelHoverOut");
        this.m_picker = picker;

        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.SetTintColor(APM_ColorDark());
        bg.SetOpacity(0.92);
        bg.SetInteractive(true);
        bg.Reparent(picker);
        let accent = new inkRectangle();
        accent.SetAnchor(inkEAnchor.TopFillHorizontaly);
        accent.SetSize(460.0 * s, 3.0 * s);
        accent.SetTintColor(APM_ColorAccent());
        accent.Reparent(picker);

        // Search box: click it to type. The button's own background is the frame, a dark inset is the field.
        let box = this.CreateButton(picker, "pick:search", "", 16.0, 8.0, 380.0, 32.0);
        let field = new inkRectangle();
        field.SetAnchor(inkEAnchor.Fill);
        field.SetMargin(2.0 * s, 2.0 * s, 2.0 * s, 2.0 * s);
        field.SetTintColor(APM_Color(0.02, 0.03, 0.05, 1.0));
        field.SetInteractive(false);
        field.Reparent(box.root);
        let search = this.CreateText(box.root, "", 12.0, 0.0, 356.0, 32.0, 17, APM_ColorDimText());
        search.SetLetterCase(textLetterCase.OriginalCase);
        search.SetInteractive(false);
        this.m_pickerSearch = search;
        this.CreateIconButton(picker, "pick:close", "x", 404.0, 10.0, "");
        this.CreateSmallButton(picker, "pick:tab0", this.L("APM-Picker-All", "All"), 16.0, 44.0, 104.0, 28.0);
        this.CreateSmallButton(picker, "pick:tab1", this.L("APM-Picker-Weapons", "Weapons"), 124.0, 44.0, 104.0, 28.0);
        this.CreateSmallButton(picker, "pick:tab2", this.L("APM-Picker-Food", "Food"), 232.0, 44.0, 104.0, 28.0);
        this.CreateSmallButton(picker, "pick:tab3", this.L("APM-Picker-Other", "Other"), 340.0, 44.0, 104.0, 28.0);
        let i = 0;
        while i < this.PickerRows() {
            let button = this.CreateButton(picker, "pick:" + IntToString(i), "", 16.0, 80.0 + Cast<Float>(i) * 34.0, 428.0, 30.0);
            button.label.SetLetterCase(textLetterCase.OriginalCase);
            ArrayPush(this.m_pickerButtons, button);
            i += 1;
        }
        let footer = 80.0 + Cast<Float>(this.PickerRows()) * 34.0 + 6.0;
        this.CreateButton(picker, "pick:prev", "<", 16.0, footer, 100.0, 34.0);
        let pageText = this.CreateText(picker, "", 120.0, footer, 220.0, 34.0, 14, APM_ColorDimText());
        pageText.SetHorizontalAlignment(textHorizontalAlignment.Center);
        this.m_pickerPageText = pageText;
        this.CreateButton(picker, "pick:next", ">", 344.0, footer, 100.0, 34.0);
        picker.SetSize(460.0 * s, (footer + 48.0) * s);
    }

    // Window listing the mouse and keyboard shortcuts, shown beside the panel by the "?" button.
    private func BuildHelp() {
        let s = APM_UiScale();
        let width = 680.0;
        let help = new inkCanvas();
        help.SetName(n"APM_Help");
        help.SetInteractive(true);
        help.SetVisible(false);
        help.Reparent(this.m_layer);
        help.RegisterToCallback(n"OnHoverOver", this, n"OnPanelHoverOver");
        help.RegisterToCallback(n"OnHoverOut", this, n"OnPanelHoverOut");
        this.m_help = help;

        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.SetTintColor(APM_ColorDark());
        bg.SetOpacity(0.94);
        bg.SetInteractive(true);
        bg.Reparent(help);
        let accent = new inkRectangle();
        accent.SetAnchor(inkEAnchor.TopFillHorizontaly);
        accent.SetSize(width * s, 3.0 * s);
        accent.SetTintColor(APM_ColorAccent());
        accent.Reparent(help);

        this.CreateText(help, this.L("APM-Help-Title", "Info"), 20.0, 10.0, 400.0, 36.0, 22, APM_ColorAccent());
        this.CreateIconButton(help, "helpclose", "x", width - 56.0, 14.0, "");
        // Marker legend: the shape shows what dragging it does right now (it changes with Shift / Ctrl / Alt).
        let y = 60.0;
        y = this.AddHelpShape(help, 0, APM_ColorText(), this.L("APM-Help-ShapeRotate", "rotate"), y);
        y = this.AddHelpShape(help, 1, APM_ColorText(), this.L("APM-Help-ShapeMove", "move"), y);
        y = this.AddHelpShape(help, 2, APM_ColorText(), this.L("APM-Help-ShapeTwist", "twist"), y);
        y = this.AddHelpShape(help, 1, APM_ColorIk(), this.L("APM-Help-ShapeIk", "IK: moves the hand / foot, the arm / leg follows"), y);
        y = this.AddHelpShape(help, 1, APM_ColorRoot(), this.L("APM-Help-ShapeHips", "hips: V's root, moves the whole body"), y);
        let divider = new inkRectangle();
        divider.SetAnchor(inkEAnchor.TopLeft);
        divider.SetMargin(20.0 * s, (y + 8.0) * s, 0.0, 0.0);
        divider.SetSize((width - 40.0) * s, 1.0 * s);
        divider.SetTintColor(APM_ColorDimText());
        divider.SetOpacity(0.5);
        divider.Reparent(help);
        y += 20.0;
        y = this.AddHelpLine(help, this.L("APM-Help-KeyAltDrag", "Alt + drag"), this.L("APM-Help-RotateFreely", "rotate freely"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyCtrlDrag", "Ctrl + drag"), this.L("APM-Help-Twist", "twist"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyShiftDrag", "Shift + drag"), this.L("APM-Help-ShiftDrag", "move (hips, held items) or rotate (hands / feet with IK)"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyWheel", "Mouse wheel"), this.L("APM-Help-Wheel", "while moving: nearer / farther"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyRightClick", "Right-click a marker"), this.L("APM-Help-RightClick", "reset that part"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyUndoRedo", "Ctrl + Z  /  Ctrl + Y"), this.L("APM-Help-UndoRedo", "undo / redo"), y);
        // Gizmo buttons of the tools row
        y = this.AddHelpLine(help, this.L("APM-Help-KeyGizmoModes", "Ctrl + 1 / 2 / 3"), this.L("APM-Help-GizmoModes", "gizmo: Direct / Move / Rotate"), y);
        y = this.AddHelpLine(help, this.L("APM-Help-KeyGizmoAxes", "Ctrl + 4"), this.L("APM-Help-GizmoAxes", "axes: the part's own / the character's"), y);
        help.SetSize(width * s, (y + 16.0) * s);
    }

    // shape: 0 diamond, 1 square, 2 bar (as the markers on V)
    private func AddHelpShape(parent: wref<inkCompoundWidget>, shape: Int32, color: HDRColor, text: String, y: Float) -> Float {
        let s = APM_UiScale();
        let dot = new inkRectangle();
        dot.SetAnchor(inkEAnchor.TopLeft);
        dot.SetAnchorPoint(0.5, 0.5);
        dot.SetTintColor(color);
        dot.SetInteractive(false);
        if shape == 2 {
            dot.SetSize(25.0 * s, 6.0 * s);
            dot.SetRotation(0.0);
        } else if shape == 1 {
            dot.SetSize(12.0 * s, 12.0 * s);
            dot.SetRotation(0.0);
        } else {
            dot.SetSize(14.0 * s, 14.0 * s);
            dot.SetRotation(45.0);
        }
        dot.SetMargin(44.0 * s, (y + 16.0) * s, 0.0, 0.0);
        dot.Reparent(parent);
        let label = this.CreateText(parent, text, 90.0, y, 560.0, 32.0, 18, APM_ColorDimText());
        label.SetLetterCase(textLetterCase.OriginalCase);
        return y + 36.0;
    }

    private func AddHelpLine(parent: wref<inkCompoundWidget>, key: String, text: String, y: Float) -> Float {
        this.CreateText(parent, key, 20.0, y, 230.0, 32.0, 18, APM_ColorText());
        let label = this.CreateText(parent, text, 250.0, y, 420.0, 32.0, 18, APM_ColorDimText());
        label.SetLetterCase(textLetterCase.OriginalCase);
        return y + 36.0;
    }

    private func SetHelpOpen(open: Bool) {
        if open && this.m_pickerOpen {
            this.ClosePicker();
        }
        this.m_helpOpen = open;
        this.m_help.SetVisible(open);
        this.SetButton("help", open, true);
    }

    private func BuildMarkers() {
        // Bone lines below the markers
        let lineLayer = new inkCanvas();
        lineLayer.SetName(n"APM_GizmoLines");
        lineLayer.SetAnchor(inkEAnchor.TopLeft);
        lineLayer.SetSize(APM_ScreenWidth(), APM_ScreenHeight());
        lineLayer.SetInteractive(false);
        lineLayer.Reparent(this.m_gizmoLayer);
        this.m_lineLayer = lineLayer;
        this.m_leftEyePart = this.PartIndex("Left eye");
        this.m_hipsPart = this.PartIndex("Body (hips)");
        let s0 = APM_UiScale();
        this.m_rootLine = this.CreateLine();
        let rootDot = new inkRectangle();
        rootDot.SetAnchor(inkEAnchor.TopLeft);
        rootDot.SetAnchorPoint(0.5, 0.5);
        rootDot.SetSize(12.0 * s0, 12.0 * s0);
        rootDot.SetRotation(45.0);
        rootDot.SetTintColor(APM_ColorDimText());
        rootDot.SetOpacity(0.8);
        rootDot.SetInteractive(false);
        rootDot.SetVisible(false);
        rootDot.Reparent(this.m_lineLayer);
        this.m_rootDot = rootDot;
        let rootLabel = this.CreateText(this.m_lineLayer, this.L("APM-Label-Root", "V's root (Photo Mode position)"), 0.0, 0.0, 400.0, 24.0, 13, APM_ColorDimText());
        rootLabel.SetLetterCase(textLetterCase.OriginalCase);
        rootLabel.SetInteractive(false);
        rootLabel.SetVisible(false);
        this.m_rootLabel = rootLabel;
        // Hover area over the root point (the label shows while the mouse is on it)
        let rootHit = new inkCanvas();
        rootHit.SetName(n"APM_RootHit");
        rootHit.SetAnchor(inkEAnchor.TopLeft);
        rootHit.SetAnchorPoint(0.5, 0.5);
        rootHit.SetSize(28.0 * s0, 28.0 * s0);
        rootHit.SetInteractive(true);
        rootHit.SetVisible(false);
        rootHit.Reparent(this.m_gizmoLayer);
        let hitArea = new inkRectangle();
        hitArea.SetAnchor(inkEAnchor.Fill);
        hitArea.SetOpacity(0.0);
        hitArea.SetInteractive(true);
        hitArea.Reparent(rootHit);
        rootHit.RegisterToCallback(n"OnHoverOver", this, n"OnRootHoverOver");
        rootHit.RegisterToCallback(n"OnHoverOut", this, n"OnRootHoverOut");
        this.m_rootHit = rootHit;
        this.m_rightEyePart = this.PartIndex("Right eye");

        let s = APM_UiScale();
        let count = APM_UiGetOptionCount(7701u);
        let i = 0;
        while i < count {
            let label = APM_UiGetOptionLabel(7701u, i);
            // Eyes: one look marker for both, or one per eye (whichever was chosen last in the Head tab).
            if true {
                let root = new inkCanvas();
                root.SetName(StringToName("APM_gizmo:" + IntToString(i)));
                root.SetAnchor(inkEAnchor.TopLeft);
                root.SetAnchorPoint(0.5, 0.5);
                root.SetSize(22.0 * s, 22.0 * s);
                root.SetInteractive(true);
                root.Reparent(this.m_gizmoLayer);

                // Soft square behind the hand / foot marker while its limb is pinned
                let halo = new inkRectangle();
                halo.SetAnchor(inkEAnchor.Centered);
                halo.SetAnchorPoint(0.5, 0.5);
                halo.SetSize(26.0 * s, 26.0 * s);
                halo.SetTintColor(APM_ColorIk());
                halo.SetOpacity(0.35);
                halo.SetInteractive(false);
                halo.SetVisible(false);
                halo.Reparent(root);

                let dot = new inkRectangle();
                dot.SetAnchor(inkEAnchor.Centered);
                dot.SetAnchorPoint(0.5, 0.5);
                dot.SetSize(10.0 * s, 10.0 * s);
                dot.SetRotation(45.0);
                dot.Reparent(root);

                root.RegisterToCallback(n"OnPress", this, n"OnMarkerPress");
                root.RegisterToCallback(n"OnHoverOver", this, n"OnMarkerHoverOver");
                root.RegisterToCallback(n"OnHoverOut", this, n"OnMarkerHoverOut");

                let marker = new APM_Marker();
                marker.part = i;
                marker.chain = APM_GizmoGetPartIkChain(i);
                marker.swivelChain = APM_GizmoGetPartSwivelChain(i);
                marker.canMove = APM_GizmoCanMovePart(i);
                marker.isProp = StrEndsWith(label, "hand prop");
                marker.group = APM_UiGetOptionGroup(7701u, i);
                marker.kind = APM_MarkerKindBone();
                if Equals(label, "Eyes (both)") || i == this.m_leftEyePart || i == this.m_rightEyePart {
                    marker.kind = APM_MarkerKindEyeLook();
                } else if APM_GizmoIsPartDetail(i) {
                    marker.kind = APM_MarkerKindFingerDetail();
                } else if APM_IsHandGroup(marker.group) {
                    marker.kind = APM_MarkerKindFinger();
                }
                marker.root = root;
                marker.dot = dot;
                marker.halo = halo;
                marker.line = this.CreateLine();
                if marker.kind == APM_MarkerKindEyeLook() {
                    if !this.IsSingleEye(i) {
                        marker.line2 = this.CreateLine();
                    }
                } else {
                    marker.parentLine = this.CreateLine();
                }
                ArrayPush(this.m_markers, marker);
            }
            i += 1;
        }
    }

    private func BuildHint() {
        let s = APM_UiScale();
        let hint = new inkText();
        hint.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
        hint.SetFontStyle(n"Medium");
        hint.SetFontSize(Cast<Int32>(13.0 * s));
        hint.SetLetterCase(textLetterCase.UpperCase);
        hint.SetTintColor(APM_ColorText());
        hint.SetAnchor(inkEAnchor.TopLeft);
        hint.SetSize(700.0 * s, 24.0 * s);
        hint.SetInteractive(false);
        hint.SetVisible(false);
        hint.Reparent(this.m_gizmoLayer);
        this.m_hint = hint;
    }

    private func CreateLine() -> wref<inkRectangle> {
        let line = new inkRectangle();
        line.SetAnchor(inkEAnchor.TopLeft);
        line.SetAnchorPoint(0.0, 0.5);
        line.SetRenderTransformPivot(0.0, 0.5);
        line.SetInteractive(false);
        line.SetVisible(false);
        line.Reparent(this.m_lineLayer);
        return line;
    }

    // Straight line between two points of the ink canvas.
    private func PlaceLine(line: wref<inkRectangle>, x1: Float, y1: Float, x2: Float, y2: Float, thickness: Float) {
        let dx = x2 - x1;
        let dy = y2 - y1;
        let length = SqrtF(dx * dx + dy * dy);
        line.SetMargin(x1, y1, 0.0, 0.0);
        line.SetSize(length, thickness);
        line.SetRotation(Rad2Deg(AtanF(dy, dx)));
    }

    private func CreateText(parent: wref<inkCompoundWidget>, text: String, x: Float, y: Float, w: Float, h: Float, size: Int32, color: HDRColor) -> ref<inkText> {
        let s = APM_UiScale();
        let label = new inkText();
        label.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
        label.SetFontStyle(n"Medium");
        label.SetFontSize(Cast<Int32>(Cast<Float>(size) * s));
        label.SetLetterCase(textLetterCase.UpperCase);
        label.SetTintColor(color);
        label.SetAnchor(inkEAnchor.TopLeft);
        label.SetMargin(x * s, y * s, 0.0, 0.0);
        label.SetSize(w * s, h * s);
        label.SetVerticalAlignment(textVerticalAlignment.Center);
        label.SetOverflowPolicy(textOverflowPolicy.AdjustToSize); // longer translations shrink to fit
        label.SetText(text);
        label.Reparent(parent);
        this.RegisterText(label, text);
        return label;
    }

    private func CreateSmallButton(parent: wref<inkCompoundWidget>, id: String, text: String, x: Float, y: Float, w: Float, h: Float) -> ref<APM_Button> {
        let button = this.CreateButton(parent, id, text, x, y, w, h);
        button.label.SetFontSize(Cast<Int32>(14.0 * APM_UiScale()));
        return button;
    }

    // Small square button with an icon (see Icons.reds).
    private func CreateIconButton(parent: wref<inkCompoundWidget>, id: String, icon: String, x: Float, y: Float, tooltip: String, opt height: Float) -> ref<APM_Button> {
        let s = APM_UiScale();
        let h = height > 0.0 ? height : 28.0;
        let button = this.CreateButton(parent, id, "", x, y, 40.0, h);
        button.tooltip = tooltip;
        this.RegisterTip(button, tooltip);
        // 24 x 24 grid drawn 22 x 22 units, centered.
        let cell = 22.0 / 24.0;
        let left = 9.0;
        let top = (h - 22.0) * 0.5;
        let spans = APM_IconSpans(icon);
        let i = 0;
        while i + 3 < ArraySize(spans) {
            let rect = new inkRectangle();
            rect.SetAnchor(inkEAnchor.TopLeft);
            rect.SetMargin((left + spans[i] * cell) * s, (top + spans[i + 1] * cell) * s, 0.0, 0.0);
            rect.SetSize(spans[i + 2] * cell * s, cell * s);
            rect.SetOpacity(spans[i + 3]);
            rect.SetInteractive(false);
            rect.Reparent(button.root);
            ArrayPush(button.icon, rect);
            i += 4;
        }
        button.UpdateLook();
        return button;
    }

    // Frame around a button, hidden until SetOutline.
    private func AddOutline(button: ref<APM_Button>, color: HDRColor) {
        let s = APM_UiScale();
        let side = 0;
        while side < 4 {
            let rect = new inkRectangle();
            rect.SetTintColor(color);
            rect.SetInteractive(false);
            rect.SetVisible(false);
            if side == 0 {
                rect.SetAnchor(inkEAnchor.TopFillHorizontaly);
                rect.SetSize(10.0 * s, 2.0 * s);
            } else if side == 1 {
                rect.SetAnchor(inkEAnchor.BottomFillHorizontaly);
                rect.SetSize(10.0 * s, 2.0 * s);
            } else if side == 2 {
                rect.SetAnchor(inkEAnchor.LeftFillVerticaly);
                rect.SetSize(2.0 * s, 10.0 * s);
            } else {
                rect.SetAnchor(inkEAnchor.RightFillVerticaly);
                rect.SetSize(2.0 * s, 10.0 * s);
            }
            rect.Reparent(button.root);
            ArrayPush(button.outline, rect);
            side += 1;
        }
    }

    private func SetOutline(id: String, on: Bool) {
        let button = this.FindButton(id);
        if !IsDefined(button) {
            return;
        }
        let i = 0;
        while i < ArraySize(button.outline) {
            button.outline[i].SetVisible(on);
            i += 1;
        }
    }

    private func CreateSection(name: CName) -> ref<inkCanvas> {
        let s = APM_UiScale();
        let section = new inkCanvas();
        section.SetName(name);
        section.SetAnchor(inkEAnchor.TopLeft);
        section.SetSize(APM_PanelWidth() * s, 10.0 * s);
        section.Reparent(this.m_panel);
        return section;
    }

    // Thin line at the top of a section.
    private func AddSeparator(section: wref<inkCanvas>) {
        let s = APM_UiScale();
        let line = new inkRectangle();
        line.SetAnchor(inkEAnchor.TopLeft);
        line.SetMargin(12.0 * s, 0.0, 0.0, 0.0);
        line.SetSize((APM_PanelWidth() - 24.0) * s, 1.5 * s);
        line.SetTintColor(APM_ColorAccent());
        line.SetOpacity(0.35);
        line.Reparent(section);
    }

    private func CreateButton(parent: wref<inkCompoundWidget>, id: String, text: String, x: Float, y: Float, w: Float, h: Float) -> ref<APM_Button> {
        let s = APM_UiScale();
        let root = new inkCanvas();
        root.SetName(StringToName("APM_" + id));
        root.SetAnchor(inkEAnchor.TopLeft);
        root.SetMargin(x * s, y * s, 0.0, 0.0);
        root.SetSize(w * s, h * s);
        root.SetInteractive(true);
        root.Reparent(parent);

        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.Reparent(root);

        let label = new inkText();
        label.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
        label.SetFontStyle(n"Medium");
        label.SetFontSize(Cast<Int32>(17.0 * s));
        label.SetLetterCase(textLetterCase.UpperCase);
        label.SetAnchor(inkEAnchor.Fill);
        label.SetHorizontalAlignment(textHorizontalAlignment.Center);
        label.SetVerticalAlignment(textVerticalAlignment.Center);
        label.SetOverflowPolicy(textOverflowPolicy.AdjustToSize); // longer translations shrink to fit the button
        label.SetText(text);
        label.Reparent(root);
        this.RegisterText(label, text);

        root.RegisterToCallback(n"OnPress", this, n"OnButtonPress");
        root.RegisterToCallback(n"OnRelease", this, n"OnButtonRelease");
        root.RegisterToCallback(n"OnHoverOver", this, n"OnButtonHoverOver");
        root.RegisterToCallback(n"OnHoverOut", this, n"OnButtonHoverOut");

        let button = new APM_Button();
        button.id = id;
        button.root = root;
        button.bg = bg;
        button.label = label;
        button.isEnabled = true;
        button.parent = parent;
        button.x = x;
        button.y = y;
        button.w = w;
        button.h = h;
        button.UpdateLook();
        ArrayPush(this.m_buttons, button);
        return button;
    }

    private func CreateRow(parent: wref<inkCompoundWidget>, attribute: Uint32, text: String, y: Float, minValue: Float, maxValue: Float, step: Float, dragScale: Float, unit: String, opt resetIcon: Bool) {
        let row = new APM_ValueRow();
        row.attribute = attribute;
        row.minValue = minValue;
        row.maxValue = maxValue;
        row.step = step;
        row.dragScale = dragScale;
        row.unit = unit;
        let key = IntToString(Cast<Int32>(attribute));
        row.label = this.CreateText(parent, text, 16.0, y, 240.0, 32.0, 16, APM_ColorText());
        ArrayPush(row.buttons, this.CreateButton(parent, "dec:" + key, "-", 262.0, y, 48.0, 32.0));
        row.value = this.CreateButton(parent, "drag:" + key, "0", 314.0, y, 200.0, 32.0);
        // Arrows: drag the value left / right to change it
        let left = this.CreateText(row.value.root, "<", 10.0, 0.0, 20.0, 32.0, 17, APM_ColorDimText());
        left.SetInteractive(false);
        let right = this.CreateText(row.value.root, ">", 178.0, 0.0, 20.0, 32.0, 17, APM_ColorDimText());
        right.SetInteractive(false);
        ArrayPush(row.buttons, this.CreateButton(parent, "inc:" + key, "+", 518.0, y, 48.0, 32.0));
        if resetIcon {
            // Face rows: back to the Photo Mode value (not 0)
            ArrayPush(row.buttons, this.CreateIconButton(parent, "facerowreset:" + key, "rotate-ccw", 578.0, y, APM_Text("APM-Tip-FaceRowReset", "Back to the value of the Photo Mode expression"), 32.0));
        } else {
            ArrayPush(row.buttons, this.CreateButton(parent, "zero:" + key, "0", 572.0, y, 52.0, 32.0));
        }
        ArrayPush(this.m_rows, row);
    }

    // --- Face ----------------------------------------------------------------------------------------------------

    // Index of a face pose by name (-1 when the face data lacks it).
    private func FacePoseIndex(name: String) -> Int32 {
        let count = APM_FacePoseCount();
        let i = 0;
        while i < count {
            if Equals(APM_FacePoseName(i), name) {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    // One face row: 0..100 (a pose's weight), or -100..100 when it has a negative pose. Placed by LayoutFace().
    private func AddFaceRow(parent: wref<inkCompoundWidget>, pose: Int32, negative: Int32, side: String, key: String, english: String, text: String) {
        let maxValue = this.FaceRowMax();
        this.CreateRow(parent, 7800u + Cast<Uint32>(pose), text, 0.0, negative >= 0 ? -maxValue : 0.0, maxValue, 5.0, 0.3, "", true);
        let row = this.m_rows[ArraySize(this.m_rows) - 1];
        row.pairAttribute = negative >= 0 ? 7800u + Cast<Uint32>(negative) : 0u;
        ArrayPush(this.m_faceRows, row);
        ArrayPush(this.m_faceRowZones, APM_FacePoseZone(pose));
        ArrayPush(this.m_faceRowSides, side);
        ArrayPush(this.m_faceRowKeys, key);
        ArrayPush(this.m_faceRowTexts, english);
    }

    // Highest value of a face row: 100 (as the game's expressions) or 400 with the limits unlocked.
    private func FaceRowMax() -> Float {
        return this.m_faceUnlocked ? 400.0 : 100.0;
    }

    private func SetFaceUnlocked(unlocked: Bool) {
        this.m_faceUnlocked = unlocked;
        APM_FaceSetUnlocked(unlocked);
        APM_PrefSet("FaceUnlocked", unlocked ? 1.0 : 0.0);
        let maxValue = this.FaceRowMax();
        let i = 0;
        while i < ArraySize(this.m_faceRows) {
            let row = this.m_faceRows[i];
            row.maxValue = maxValue;
            row.minValue = row.pairAttribute != 0u ? -maxValue : 0.0;
            i += 1;
        }
    }

    private func AddFaceFooter(button: ref<APM_Button>, offset: Float) {
        ArrayPush(this.m_faceFooter, button);
        ArrayPush(this.m_faceFooterOffsets, offset);
    }

    private func IsFaceRowShown(row: Int32) -> Bool {
        return Equals(this.m_faceRowZones[row], this.m_faceZones[this.m_faceZone]) && !(this.m_faceLinked && Equals(this.m_faceRowSides[row], "r"));
    }

    private func MoveButton(button: ref<APM_Button>, y: Float) {
        let s = APM_UiScale();
        button.y = y;
        button.root.SetMargin(button.x * s, y * s, 0.0, 0.0);
    }

    private func PlaceRow(row: ref<APM_ValueRow>, y: Float, visible: Bool) {
        let s = APM_UiScale();
        row.label.SetVisible(visible);
        row.value.root.SetVisible(visible);
        let b = 0;
        while b < ArraySize(row.buttons) {
            row.buttons[b].root.SetVisible(visible);
            b += 1;
        }
        if visible {
            row.label.SetMargin(16.0 * s, y * s, 0.0, 0.0);
            this.MoveButton(row.value, y);
            b = 0;
            while b < ArraySize(row.buttons) {
                this.MoveButton(row.buttons[b], y);
                b += 1;
            }
        }
    }

    // Rows of the zone shown under the tabs (one page of them), then the footer; sets the section height. Only when
    // the zone, the page or L+R changed.
    private func LayoutFace() {
        let available = APM_FaceAvailable();
        let shownCount = 0;
        let i = 0;
        while i < ArraySize(this.m_faceRows) {
            if this.IsFaceRowShown(i) {
                shownCount += 1;
            }
            i += 1;
        }
        let pages = Max(1, (shownCount + APM_FaceRowsPerPage() - 1) / APM_FaceRowsPerPage());
        this.m_faceRowPage = Clamp(this.m_faceRowPage, 0, pages - 1);
        let layout = IntToString(this.m_faceZone) + "/" + IntToString(this.m_faceRowPage) + (this.m_faceLinked ? "+" : "-") + (available ? "" : "!");
        if Equals(layout, this.m_faceLayout) {
            return;
        }
        this.m_faceLayout = layout;
        this.m_faceMissing.SetVisible(!available);
        let s = APM_UiScale();
        let first = this.m_faceRowPage * APM_FaceRowsPerPage();
        let y = available ? 76.0 : 112.0;
        let k = 0;
        i = 0;
        while i < ArraySize(this.m_faceRows) {
            let onPage = false;
            if this.IsFaceRowShown(i) {
                onPage = k >= first && k < first + APM_FaceRowsPerPage();
                k += 1;
            }
            this.PlaceRow(this.m_faceRows[i], y, onPage);
            if onPage {
                y += 36.0;
            }
            i += 1;
        }
        y += 4.0;
        i = 0;
        while i < ArraySize(this.m_faceFooter) {
            this.MoveButton(this.m_faceFooter[i], y + this.m_faceFooterOffsets[i]);
            i += 1;
        }
        let paged = pages > 1;
        this.m_facePageText.SetMargin(270.0 * s, (y - 2.0) * s, 0.0, 0.0);
        this.m_facePageText.SetText(IntToString(this.m_faceRowPage + 1) + "/" + IntToString(pages));
        this.m_facePageText.SetVisible(paged);
        let prev = this.FindButton("facepage:prev");
        let next = this.FindButton("facepage:next");
        if IsDefined(prev) && IsDefined(next) {
            prev.root.SetVisible(paged);
            next.root.SetVisible(paged);
        }
        this.SetButton("facepage:prev", false, this.m_faceRowPage > 0);
        this.SetButton("facepage:next", false, this.m_faceRowPage < pages - 1);
        this.m_faceHeight = 40.0 + y + 36.0;
    }

    // Shown value of a row: a face row with a negative pose shows its pose's weight minus the negative one's.
    private func RowValue(row: ref<APM_ValueRow>) -> Float {
        if row.pairAttribute != 0u {
            return APM_UiGetValue(row.attribute) - APM_UiGetValue(row.pairAttribute);
        }
        return APM_UiGetValue(row.attribute);
    }

    private func SetRowValue(row: ref<APM_ValueRow>, value: Float) {
        if row.pairAttribute != 0u {
            APM_UiSetValue(row.attribute, MaxF(value, 0.0));
            APM_UiSetValue(row.pairAttribute, MaxF(-value, 0.0));
            return;
        }
        APM_UiSetValue(row.attribute, value);
    }

    private func RefreshFace() {
        this.SetButton("mode:body", !this.m_faceMode, true);
        this.SetButton("mode:face", this.m_faceMode, true);
        this.SetButton("face:linked", this.m_faceLinked, true);
        let unlockButton = this.FindButton("face:unlock");
        let lockButton = this.FindButton("face:lock");
        if IsDefined(unlockButton) && IsDefined(lockButton) {
            unlockButton.root.SetVisible(!this.m_faceUnlocked);
            lockButton.root.SetVisible(this.m_faceUnlocked);
            lockButton.isMarked = true; // warning color while the limits are off
            lockButton.UpdateLook();
        }
        let edited: array<Bool>;
        let z = 0;
        while z < ArraySize(this.m_faceZones) {
            ArrayPush(edited, false);
            z += 1;
        }
        let i = 0;
        while i < ArraySize(this.m_faceRows) {
            let row = this.m_faceRows[i];
            let isEdited = APM_FaceIsEdited(Cast<Int32>(row.attribute) - 7800) || (row.pairAttribute != 0u && APM_FaceIsEdited(Cast<Int32>(row.pairAttribute) - 7800));
            if isEdited {
                z = 0;
                while z < ArraySize(this.m_faceZones) {
                    if Equals(this.m_faceZones[z], this.m_faceRowZones[i]) {
                        edited[z] = true;
                    }
                    z += 1;
                }
            }
            if this.IsFaceRowShown(i) {
                // "Smile" moves both sides with L+R, else "Smile L" / "Smile R"
                let side = this.m_faceRowSides[i];
                let suffix = this.m_faceLinked || StrLen(side) == 0 ? "" : (Equals(side, "l") ? " L" : " R");
                row.label.SetText(APM_Text(this.m_faceRowKeys[i], this.m_faceRowTexts[i]) + suffix);
            }
            i += 1;
        }
        let anyEdited = false;
        z = 0;
        while z < ArraySize(this.m_faceZones) {
            let tab = this.FindButton("facezone:" + this.m_faceZones[z]);
            if IsDefined(tab) {
                tab.isSelected = z == this.m_faceZone;
                tab.UpdateLook();
                this.SetDot(tab, edited[z]); // a zone with poses changed from Photo Mode
            }
            anyEdited = anyEdited || edited[z];
            z += 1;
        }
        let loaded = APM_FaceGetLoadedName();
        let info = anyEdited ? APM_Text("APM-Label-EditedExpression", "- edited Photo Mode expression") : APM_Text("APM-Label-PhotoModeExpression", "- Photo Mode expression");
        this.m_faceInfo.SetText(StrLen(loaded) > 0 ? "- " + loaded : info);
    }

    // --- State -----------------------------------------------------------------------------------------------------

    private func SetOpen(open: Bool) {
        this.m_isOpen = open;
        this.m_panel.SetVisible(open);
        this.m_openButton.root.SetVisible(!open);
        this.m_dragRow = null;
        if open {
            this.ReloadPoses();
            this.Refresh();
        } else {
            if this.m_renaming {
                this.FinishRename(true);
            }
            this.m_panelHovered = false;
            this.SetHover(false);
            if this.m_pickerOpen {
                this.ClosePicker();
            }
            if this.m_helpOpen {
                this.SetHelpOpen(false);
            }
            this.m_messageTimer = 0.0;
            this.m_tipButton = null;
            this.HideTip();
        }
    }

    // The panel sits on the left (the Photo Mode menu is on the right); the item list and the help open beside it.
    private func ApplyDock() {
        let s = APM_UiScale();
        this.m_panel.SetAnchor(inkEAnchor.TopLeft);
        this.m_panel.SetAnchorPoint(0.0, 0.0);
        this.m_panel.SetMargin(24.0 * s, 60.0 * s, 0.0, 0.0);
        this.m_openButton.root.SetAnchor(inkEAnchor.TopLeft);
        this.m_openButton.root.SetAnchorPoint(0.0, 0.0);
        this.m_openButton.root.SetMargin(24.0 * s, 60.0 * s, 0.0, 0.0);
        this.m_picker.SetAnchor(inkEAnchor.TopLeft);
        this.m_picker.SetAnchorPoint(0.0, 0.0);
        this.m_picker.SetMargin((24.0 + APM_PanelWidth() + 8.0) * s, 60.0 * s, 0.0, 0.0);
        this.m_help.SetAnchor(inkEAnchor.TopLeft);
        this.m_help.SetAnchorPoint(0.0, 0.0);
        this.m_help.SetMargin((24.0 + APM_PanelWidth() + 8.0) * s, 60.0 * s, 0.0, 0.0);
    }

    // Photo Mode changed the frame (aspect ratio) or turned its black bars on / off: the panel stays at the screen's
    // edges, drawn above the black bars like Photo Mode's own menu. The root draws its first child (the game menu,
    // options_panel) above the black bars (aspectratio_panel, child 4): our layer as the last child was drawn below them.
    public func OnFrameChanged() {
        this.KeepLayerOnTop(false);
    }

    // The game hides the menu root's children while Photo Mode opens and shows them again; our layer must not stay
    // hidden when the game's own menu (options_panel) is shown (a user lost the "Pose editor" button from the second
    // session). It is never hidden by us: the UI is hidden for screenshots through the root's opacity.
    private func KeepLayerVisible() {
        if !IsDefined(this.m_layer) || this.m_layer.IsVisible() {
            return;
        }
        let root = this.m_controller.GetRootCompoundWidget();
        let menu = IsDefined(root) ? root.GetWidget(n"options_panel") : null;
        if IsDefined(menu) && menu.IsVisible() {
            this.m_layer.SetVisible(true);
            APM_LogInfo("Pose editor layer was hidden while the Photo Mode menu is shown: shown again");
        }
    }

    // The menu root draws its children backward (the first one on top). Other Photo Mode mods move their own layers
    // to the front when they add them, and the menu (with our layer) lives for the whole game, not one Photo Mode
    // session: from the second session their layers could cover the panel and the "Pose editor" button. Ours goes
    // back to the front whenever it is not there.
    private func KeepLayerOnTop(logChildren: Bool) {
        let root = this.m_controller.GetRootCompoundWidget();
        if !IsDefined(root) || !IsDefined(this.m_layer) {
            return;
        }
        let backward = Equals(root.GetChildOrder(), inkEChildOrder.Backward);
        let count = root.GetNumChildren();
        let top = backward ? 0 : count - 1;
        if logChildren {
            // Debug log ([Debug] Log=1): what the menu root holds when Photo Mode opens.
            let text = "Menu root (" + (backward ? "backward" : "forward") + ", " + IntToString(count) + " children):";
            let i = 0;
            while i < count {
                let child = root.GetWidgetByIndex(i);
                if IsDefined(child) {
                    text += " [" + IntToString(i) + "] " + NameToString(child.GetName()) + (child.IsVisible() ? "" : " (hidden)");
                }
                i += 1;
            }
            APM_LogInfo(text);
        }
        if count > 0 && root.GetWidgetByIndex(top) != this.m_layer {
            root.ReorderChild(this.m_layer, top);
            if logChildren {
                APM_LogInfo("Pose editor layer moved to the front of the menu root");
            }
        }
    }

    // Panel transparency: background 85/60/35/15 %, controls slightly faded at the lowest steps.
    private func ApplyOpacity() {
        let step = this.m_opacityStep % 4;
        let bgOpacity = 0.85;
        let panelOpacity = 1.0;
        let label = "100%";
        if step == 1 {
            bgOpacity = 0.6;
            label = "75%";
        } else if step == 2 {
            bgOpacity = 0.35;
            panelOpacity = 0.9;
            label = "50%";
        } else if step == 3 {
            bgOpacity = 0.15;
            panelOpacity = 0.75;
            label = "25%";
        }
        this.m_panelBg.SetOpacity(bgOpacity);
        this.m_panel.SetOpacity(panelOpacity);
        let button = this.FindButton("opacity");
        if IsDefined(button) {
            button.label.SetText(label);
        }
    }

    // Only a hover we took is released: leaving one of our widgets must not take the hover from the game's menu.
    private func SetHover(hover: Bool) {
        if !hover && !this.m_ownsHover {
            return;
        }
        this.m_ownsHover = hover;
        if IsDefined(this.m_controller) {
            this.m_controller.APM_SetHover(hover);
        }
    }

    private func FindButton(id: String) -> ref<APM_Button> {
        let i = 0;
        while i < ArraySize(this.m_buttons) {
            if Equals(this.m_buttons[i].id, id) {
                return this.m_buttons[i];
            }
            i += 1;
        }
        return null;
    }

    private func FindButtonByWidget(widget: wref<inkWidget>) -> ref<APM_Button> {
        if !IsDefined(widget) {
            return null;
        }
        let name = widget.GetName();
        let i = 0;
        while i < ArraySize(this.m_buttons) {
            if Equals(this.m_buttons[i].root.GetName(), name) {
                return this.m_buttons[i];
            }
            i += 1;
        }
        return null;
    }

    private func FindMarkerByWidget(widget: wref<inkWidget>) -> ref<APM_Marker> {
        if !IsDefined(widget) {
            return null;
        }
        let name = widget.GetName();
        let i = 0;
        while i < ArraySize(this.m_markers) {
            if Equals(this.m_markers[i].root.GetName(), name) {
                return this.m_markers[i];
            }
            i += 1;
        }
        return null;
    }

    private func FindRow(attribute: Uint32) -> ref<APM_ValueRow> {
        let i = 0;
        while i < ArraySize(this.m_rows) {
            if this.m_rows[i].attribute == attribute {
                return this.m_rows[i];
            }
            i += 1;
        }
        return null;
    }

    private func SetButton(id: String, selected: Bool, enabled: Bool) {
        let button = this.FindButton(id);
        if IsDefined(button) {
            button.isSelected = selected;
            button.isEnabled = enabled;
            button.UpdateLook();
        }
    }

    private func FormatValue(row: ref<APM_ValueRow>, value: Float) -> String {
        if row.attribute >= 7800u {
            return IntToString(RoundF(value)); // face pose weight, 0..100
        }
        let text = FloatToStringPrec(value, 2);
        if Equals(text, "-0.00") {
            text = "0.00";
        }
        return text + row.unit;
    }

    private func GroupIndexOf(group: String) -> Int32 {
        let i = 0;
        while i < ArraySize(this.m_groupNames) {
            if Equals(this.m_groupNames[i], group) {
                return i;
            }
            i += 1;
        }
        return 0;
    }

    private func FirstPartOfGroup(group: String) -> Int32 {
        let count = APM_UiGetOptionCount(7701u);
        let i = 0;
        while i < count {
            if Equals(APM_UiGetOptionGroup(7701u, i), group) {
                return i;
            }
            i += 1;
        }
        return 0;
    }

    private func BuildTooltip() {
        let s = APM_UiScale();
        let tip = new inkCanvas();
        tip.SetName(n"APM_Tooltip");
        tip.SetAnchor(inkEAnchor.TopLeft);
        tip.SetInteractive(false);
        tip.SetVisible(false);
        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.SetTintColor(APM_Color(0.03, 0.05, 0.07, 1.0));
        bg.SetOpacity(1.0);
        bg.SetInteractive(false);
        bg.Reparent(tip);
        let line = new inkRectangle();
        line.SetAnchor(inkEAnchor.TopFillHorizontaly);
        line.SetSize(100.0 * s, 2.0 * s);
        line.SetTintColor(APM_ColorAccent());
        line.SetInteractive(false);
        line.Reparent(tip);
        let text = new inkText();
        text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
        text.SetFontStyle(n"Medium");
        text.SetFontSize(Cast<Int32>(16.0 * s));
        text.SetLetterCase(textLetterCase.OriginalCase);
        text.SetTintColor(APM_ColorText());
        text.SetAnchor(inkEAnchor.Fill);
        text.SetMargin(10.0 * s, 0.0, 10.0 * s, 0.0);
        text.SetVerticalAlignment(textVerticalAlignment.Center);
        text.SetInteractive(false);
        text.Reparent(tip);
        tip.Reparent(this.m_layer); // above the panel and not faded with it
        this.m_tip = tip;
        this.m_tipText = text;
    }

    // Top-left corner of a button's container on the screen layer (panel units).
    private func ContainerOrigin(container: wref<inkCompoundWidget>) -> Vector2 {
        let s = APM_UiScale();
        let own = container.GetMargin();
        let name = container.GetName();
        if Equals(name, n"APM_ItemPicker") || Equals(name, n"APM_Help") {
            return new Vector2(own.left / s, own.top / s);
        }
        let panel = this.m_panel.GetMargin();
        let x = (panel.left + own.left) / s;
        let y = (panel.top + own.top) / s;
        if Equals(name, n"APM_IkRows") {
            let section = this.m_sectionIk.GetMargin();
            x += section.left / s;
            y += section.top / s;
        } else if Equals(name, n"APM_PosesBody") {
            let section = this.m_sectionPoses.GetMargin();
            x += section.left / s;
            y += section.top / s;
        } else if Equals(name, n"APM_FaceBody") {
            let section = this.m_sectionFace.GetMargin();
            x += section.left / s;
            y += section.top / s;
        }
        return new Vector2(x, y);
    }

    // Shows the box just above a button (below it when there is no room), inside the button's own container.
    private func ShowTip(button: ref<APM_Button>, text: String) {
        if !IsDefined(button) || !IsDefined(button.parent) || StrLen(text) == 0 {
            return;
        }
        let s = APM_UiScale();
        let origin = this.ContainerOrigin(button.parent);
        let width = MinF(600.0, 24.0 + Cast<Float>(StrLen(text)) * 7.6);
        let x = MaxF(4.0, origin.X + button.x + button.w * 0.5 - width * 0.5);
        let y = origin.Y + button.y - 38.0;
        if y < 4.0 {
            y = origin.Y + button.y + button.h + 6.0;
        }
        this.m_tip.SetMargin(x * s, y * s, 0.0, 0.0);
        this.m_tip.SetSize(width * s, 32.0 * s);
        this.m_tipText.SetText(text);
        this.m_tip.SetVisible(true);
        this.m_tipShown = true;
    }

    private func HideTip() {
        if IsDefined(this.m_tip) {
            this.m_tip.SetVisible(false);
        }
        this.m_tipShown = false;
    }

    // Hover tooltips appear after a short wait; messages stay a few seconds.
    private func UpdateTooltip(timeDelta: Float) {
        if this.m_messageTimer > 0.0 {
            this.m_messageTimer -= timeDelta;
            if this.m_messageTimer <= 0.0 {
                this.HideTip();
                // "Click again" for Reset / Delete lasts as long as its message.
                if this.m_confirmResetAll || this.m_confirmDelete || this.m_confirmOverwrite {
                    this.m_confirmResetAll = false;
                    this.m_confirmDelete = false;
                    this.m_confirmOverwrite = false;
                    this.Refresh();
                }
            }
            return;
        }
        if !IsDefined(this.m_tipButton) || this.m_tipShown {
            return;
        }
        this.m_tipTimer += timeDelta;
        if this.m_tipTimer >= 0.6 {
            this.ShowTip(this.m_tipButton, this.m_tipButton.tooltip);
        }
    }

    // Feedback after an action, shown above the button that started it.
    private func SetStatus(text: String) {
        let button = this.m_lastButton;
        if !IsDefined(button) {
            button = this.FindButton("undo");
        }
        this.ShowTip(button, text);
        this.m_messageTimer = 2.5;
    }

    // --- Pose library --------------------------------------------------------------------------------------------

    // What the list shows: 0 body poses, 1 hand poses (Hands tab), 2 saved expressions (Face mode).
    private func LibraryKind() -> Int32 {
        return this.m_faceMode ? 2 : (this.m_handLibrary ? 1 : 0);
    }

    // Saved files of the list (re-read from the folder).
    private func LibraryCount() -> Int32 {
        let kind = this.LibraryKind();
        if kind == 2 {
            APM_FaceReloadList();
            return APM_FaceListCount();
        }
        return kind == 1 ? APM_GetHandPoseCount() : APM_GetPoseCount();
    }

    private func LibraryName(index: Int32) -> String {
        let kind = this.LibraryKind();
        if kind == 2 {
            return APM_FaceListName(index);
        }
        return kind == 1 ? APM_GetHandPoseName(index) : APM_GetPoseName(index);
    }

    // Built-in entries listed before the saved ones, as ids no file name can have: the T and A poses, the hand presets.
    private func BuiltInIds() -> array<String> {
        let ids: array<String>;
        let kind = this.LibraryKind();
        if kind == 0 {
            ArrayPush(ids, "::t");
            ArrayPush(ids, "::a");
        } else if kind == 1 {
            let p = 0;
            while p < 5 {
                ArrayPush(ids, "::preset:" + IntToString(p));
                p += 1;
            }
        }
        return ids;
    }

    private func IsBuiltIn(name: String) -> Bool {
        return StrBeginsWith(name, "::") && !this.IsFolderEntry(name);
    }

    // List entries that open a folder: "::up" (the parent), "::builtin/" (the built-in entries), "Some mod/".
    private func IsFolderEntry(name: String) -> Bool {
        return Equals(name, "::up") || StrEndsWith(name, "/");
    }

    // Last part of a path ("Some mod/Sitting" -> "Sitting").
    private func LastPart(path: String) -> String {
        return StrContains(path, "/") ? StrAfterLast(path, "/") : path;
    }

    private func EntryText(name: String) -> String {
        if Equals(name, "::up") {
            return "..";
        }
        if Equals(name, "::builtin/") {
            return APM_Text("APM-Folder-BuiltIn", "Built-in");
        }
        if StrEndsWith(name, "/") {
            return this.LastPart(StrLeft(name, StrLen(name) - 1));
        }
        if !this.IsBuiltIn(name) {
            return this.LastPart(name);
        }
        let text: String;
        if Equals(name, "::t") {
            text = APM_Text("APM-Button-TPose", "T pose");
        } else if Equals(name, "::a") {
            text = APM_Text("APM-Button-APose", "A pose");
        } else {
            let preset = StringToInt(StrAfterFirst(name, "::preset:"));
            let keys: array<String> = ["APM-Preset-Open", "APM-Preset-Relaxed", "APM-Preset-Fist", "APM-Preset-Point", "APM-Preset-Grip"];
            let texts: array<String> = ["Open", "Relaxed", "Fist", "Point", "Grip"];
            text = APM_Text(keys[preset], texts[preset]);
        }
        return text;
    }

    private func LoadBuiltIn(name: String) {
        if Equals(name, "::t") || Equals(name, "::a") {
            if !APM_SetNeutralPose(Equals(name, "::a")) {
                this.SetStatus(APM_Text("APM-Msg-NeutralUnavailable", "Neutral pose not available"));
            }
            return;
        }
        let hand = this.HandPoseTarget();
        if hand > 0 {
            APM_HistoryCheckpoint();
            APM_ApplyHandPreset(hand == 2, StringToInt(StrAfterFirst(name, "::preset:")));
        }
    }

    // Body / Hands tab of the list (opens it).
    private func ShowLibrary(hands: Bool) {
        this.m_handLibrary = hands;
        this.m_posesOpen = true;
        APM_SessionSet("PosesOpen", 1.0);
        this.m_selectedPose = "";
        this.m_posePage = 0;
        this.ReloadPoses();
    }

    // Hand a hand pose is saved from / applied to: the hand of the open tab (1 left, 2 right), else a message.
    private func HandPoseTarget() -> Int32 {
        let hand = this.PresetHand();
        if hand == 0 {
            this.SetStatus(APM_Text("APM-Msg-HandPoseNoHand", "Open the tab of a hand (or of its arm) first"));
        }
        return hand;
    }

    // Folder of the list open in the library shown: "" its own folder, "Some mod/" a subfolder, "::builtin/" the
    // built-in entries (T and A poses, hand presets).
    private func LibraryFolder() -> String {
        let kind = this.LibraryKind();
        return kind < ArraySize(this.m_libraryFolders) ? this.m_libraryFolders[kind] : "";
    }

    // Folder on disk for saving and Explorer (the built-in entries have none: the library's own folder).
    private func LibraryDiskFolder() -> String {
        let folder = this.LibraryFolder();
        return StrBeginsWith(folder, "::") ? "" : folder;
    }

    private func OpenLibraryFolder(folder: String) {
        while ArraySize(this.m_libraryFolders) < 3 {
            ArrayPush(this.m_libraryFolders, "");
        }
        this.m_libraryFolders[this.LibraryKind()] = folder;
        this.m_selectedPose = "";
        this.m_posePage = 0;
        this.ReloadPoses();
    }

    // Parent of an open folder ("A/B/" -> "A/", "A/" and "::builtin/" -> "").
    private func ParentFolder(folder: String) -> String {
        let trimmed = StrLeft(folder, StrLen(folder) - 1);
        return !StrBeginsWith(folder, "::") && StrContains(trimmed, "/") ? StrBeforeLast(trimmed, "/") + "/" : "";
    }

    // Entries of the open folder, as in Explorer: ".." in a subfolder, then the folders (Built-in first in the
    // library's own folder), then the files.
    private func BuildEntries() {
        let folder = this.LibraryFolder();
        let builtIns = this.BuiltInIds();
        let entries: array<String>;
        let files: array<String>;
        if StrLen(folder) > 0 {
            ArrayPush(entries, "::up");
        }
        let i = 0;
        if Equals(folder, "::builtin/") {
            while i < ArraySize(builtIns) {
                ArrayPush(entries, builtIns[i]);
                i += 1;
            }
            this.m_poseNames = entries;
            return;
        }
        if StrLen(folder) == 0 && ArraySize(builtIns) > 0 {
            ArrayPush(entries, "::builtin/");
        }
        while i < ArraySize(this.m_libraryAll) {
            let name = this.m_libraryAll[i];
            if StrLen(folder) == 0 || StrBeginsWith(name, folder) {
                let rest = StrMid(name, StrLen(folder));
                if StrContains(rest, "/") {
                    let sub = folder + StrBeforeFirst(rest, "/") + "/";
                    if !ArrayContains(entries, sub) {
                        ArrayPush(entries, sub);
                    }
                } else {
                    ArrayPush(files, name);
                }
            }
            i += 1;
        }
        i = 0;
        while i < ArraySize(files) {
            ArrayPush(entries, files[i]);
            i += 1;
        }
        this.m_poseNames = entries;
    }

    private func ReloadPoses() {
        ArrayClear(this.m_libraryAll);
        let count = this.LibraryCount();
        let i = 0;
        while i < count {
            ArrayPush(this.m_libraryAll, this.LibraryName(i));
            i += 1;
        }
        this.BuildEntries();
        let pages = this.PageCount();
        if this.m_posePage >= pages {
            this.m_posePage = pages - 1;
        }
        if this.m_posePage < 0 {
            this.m_posePage = 0;
        }
        if !ArrayContains(this.m_poseNames, this.m_selectedPose) {
            this.m_selectedPose = "";
        }
    }

    // While the saved poses are shown, the folder is checked every 2 seconds: poses added or removed there (e.g.
    // shared by someone) appear without reopening the panel.
    private func UpdatePoseList(timeDelta: Float) {
        if !this.m_isOpen || !this.m_posesOpen || this.m_renaming {
            this.m_poseListTimer = 0.0;
            return;
        }
        this.m_poseListTimer += timeDelta;
        if this.m_poseListTimer < 2.0 {
            return;
        }
        this.m_poseListTimer = 0.0;
        let count = this.LibraryCount();
        let changed = count != ArraySize(this.m_libraryAll);
        let i = 0;
        while !changed && i < count {
            changed = NotEquals(this.LibraryName(i), this.m_libraryAll[i]);
            i += 1;
        }
        if changed {
            this.ReloadPoses();
            this.Refresh();
        }
    }

    private func PageCount() -> Int32 {
        let count = ArraySize(this.m_poseNames);
        let pages = (count + APM_PosesPerPage() - 1) / APM_PosesPerPage();
        return pages < 1 ? 1 : pages;
    }

    // --- Items in hand -------------------------------------------------------------------------------------------

    // Picker category of an item record: 1 weapon, 2 food & drink, 3 other small item, 0 not offered.
    // Shards, braindances, quest and misc items are left out: most of them have no model to hold.
    private func ItemCategory(record: ref<Item_Record>) -> Int32 {
        let itemType = record.ItemType();
        if !IsDefined(itemType) {
            return 0;
        }
        let typeName = EnumValueToString("gamedataItemType", Cast<Int64>(EnumInt(itemType.Type())));
        // Items without a model to show (no entity) cannot be held.
        if !IsNameValid(record.EntityName()) {
            return 0;
        }
        if record.TagsContains(n"Quest") || record.TagsContains(n"Shard") || record.TagsContains(n"Braindance") || record.TagsContains(n"HideInUI") {
            return 0;
        }
        if StrBeginsWith(typeName, "Wea_") {
            let usable = !Equals(typeName, "Wea_Fists") && !Equals(typeName, "Wea_VehicleMissileLauncher") && !Equals(typeName, "Wea_VehiclePowerWeapon");
            return usable ? 1 : 0;
        }
        if Equals(typeName, "Con_Edible") {
            return 2;
        }
        if Equals(typeName, "Gen_Junk") || Equals(typeName, "Gen_Jewellery") {
            return 3;
        }
        return 0;
    }

    // Every item with a name, one per name, sorted by name (built the first time the picker opens).
    private func BuildItemList() {
        if this.m_itemsBuilt {
            return;
        }
        this.m_itemsBuilt = true;
        let kind = 0;
        while kind < 3 {
            let count = APM_PropRecordCount(kind);
            let i = 0;
            while i < count {
                let id = APM_RecordFromNumber(APM_PropRecordAt(kind, i));
                let record = TweakDBInterface.GetItemRecord(id);
                let category = IsDefined(record) ? this.ItemCategory(record) : 0;
                if category > 0 {
                    let name = GetLocalizedItemNameByCName(record.DisplayName());
                    if StrLen(name) > 0 && !StrBeginsWith(name, "LocKey") && !ArrayContains(this.m_itemNames, name) {
                        let low = 0;
                        let high = ArraySize(this.m_itemNames);
                        while low < high {
                            let middle = (low + high) / 2;
                            if StrCmp(this.m_itemNames[middle], name) < 0 {
                                low = middle + 1;
                            } else {
                                high = middle;
                            }
                        }
                        ArrayInsert(this.m_itemNames, low, name);
                        ArrayInsert(this.m_itemSearch, low, StrLower(name));
                        ArrayInsert(this.m_itemIds, low, id);
                        ArrayInsert(this.m_itemWeapon, low, category == 1);
                        ArrayInsert(this.m_itemCategory, low, category);
                    }
                }
                i += 1;
            }
            kind += 1;
        }
        APM_LogInfo("Items: " + IntToString(ArraySize(this.m_itemIds)) + " in the picker");
    }

    private func PickerRows() -> Int32 {
        return 12;
    }

    // An item can go in the current hand: weapons only in the right one (the game poses both hands around them).
    private func ItemAllowed(index: Int32) -> Bool {
        if !this.m_propRight && this.m_itemWeapon[index] {
            return false;
        }
        if this.m_pickerCategory > 0 && this.m_itemCategory[index] != this.m_pickerCategory {
            return false;
        }
        let number = TDBID.ToNumber(this.m_itemIds[index]);
        return !ArrayContains(this.m_noModelItems, number);
    }

    private func OpenPicker() {
        if this.m_renaming {
            this.FinishRename(true);
        }
        if this.m_helpOpen {
            this.SetHelpOpen(false);
        }
        this.BuildItemList();
        this.m_pickerOpen = true;
        this.m_pickerTyping = false;
        this.m_pickerQuery = "";
        this.m_pickerPage = 0;
        this.UpdatePickerResults();
        this.m_picker.SetVisible(true);
    }

    private func ClosePicker() {
        this.StopPickerTyping();
        this.m_pickerOpen = false;
        this.m_picker.SetVisible(false);
    }

    private func StartPickerTyping() {
        if this.m_pickerTyping {
            return;
        }
        if this.m_renaming {
            this.FinishRename(true);
        }
        if !APM_TextEntryBegin(this.m_pickerQuery) {
            this.SetStatus(APM_Text("APM-Msg-TypingUnavailable", "Typing is not available"));
            return;
        }
        this.m_pickerTyping = true;
        this.RefreshPicker();
    }

    private func StopPickerTyping() {
        if this.m_pickerTyping {
            APM_TextEntryEnd();
        }
        this.m_pickerTyping = false;
    }

    // Results for the current text: recently used items first when nothing is typed.
    private func UpdatePickerResults() {
        ArrayClear(this.m_pickerResults);
        let query = StrLower(this.m_pickerQuery);
        if StrLen(query) == 0 {
            let r = 0;
            while r < ArraySize(this.m_recentItems) {
                let index = this.ItemIndex(this.m_recentItems[r]);
                if index >= 0 && this.ItemAllowed(index) {
                    ArrayPush(this.m_pickerResults, index);
                }
                r += 1;
            }
        }
        let i = 0;
        while i < ArraySize(this.m_itemIds) {
            if this.ItemAllowed(i) && (StrLen(query) == 0 || StrContains(this.m_itemSearch[i], query)) && !ArrayContains(this.m_pickerResults, i) {
                ArrayPush(this.m_pickerResults, i);
            }
            i += 1;
        }
        let pages = Max(1, (ArraySize(this.m_pickerResults) + this.PickerRows() - 1) / this.PickerRows());
        this.m_pickerPage = Clamp(this.m_pickerPage, 0, pages - 1);
        this.RefreshPicker();
    }

    private func ItemIndex(id: TweakDBID) -> Int32 {
        let i = 0;
        while i < ArraySize(this.m_itemIds) {
            if this.m_itemIds[i] == id {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    private func RefreshPicker() {
        let first = this.m_pickerPage * this.PickerRows();
        let held = APM_GetHandProp(this.m_propRight);
        let i = 0;
        while i < ArraySize(this.m_pickerButtons) {
            let button = this.m_pickerButtons[i];
            let result = first + i;
            if result < ArraySize(this.m_pickerResults) {
                let index = this.m_pickerResults[result];
                button.root.SetVisible(true);
                button.label.SetText(this.m_itemNames[index]);
                button.isSelected = TDBID.ToNumber(this.m_itemIds[index]) == held;
                button.UpdateLook();
            } else {
                button.root.SetVisible(false);
            }
            i += 1;
        }
        let tab = 0;
        while tab < 4 {
            let tabButton = this.FindButton("pick:tab" + IntToString(tab));
            if IsDefined(tabButton) {
                tabButton.isSelected = tab == this.m_pickerCategory;
                tabButton.UpdateLook();
            }
            tab += 1;
        }
        let pages = Max(1, (ArraySize(this.m_pickerResults) + this.PickerRows() - 1) / this.PickerRows());
        this.m_pickerPageText.SetText(APM_TextWith("APM-Picker-Count", "{0} items", IntToString(ArraySize(this.m_pickerResults))) + "  " + IntToString(this.m_pickerPage + 1) + "/" + IntToString(pages));
        if this.m_pickerTyping {
            this.m_pickerSearch.SetText(this.m_pickerQuery + "_");
            this.m_pickerSearch.SetTintColor(APM_ColorText());
        } else {
            this.m_pickerSearch.SetText(StrLen(this.m_pickerQuery) > 0 ? this.m_pickerQuery : APM_Text("APM-Picker-ClickToSearch", "Click to search"));
            this.m_pickerSearch.SetTintColor(StrLen(this.m_pickerQuery) > 0 ? APM_ColorText() : APM_ColorDimText());
        }
        let searchBox = this.FindButton("pick:search");
        if IsDefined(searchBox) {
            searchBox.isSelected = this.m_pickerTyping;
            searchBox.UpdateLook();
        }
    }

    // While the picker is open: follow the typed text, Enter takes the first result, Esc closes.
    private func UpdatePicker() {
        if !this.m_pickerOpen {
            return;
        }
        if !APM_IsPhotoModeActive() {
            this.ClosePicker();
            return;
        }
        if !this.m_pickerTyping {
            return;
        }
        // Enter takes the first result and closes the list; Esc just stops typing.
        let state = APM_TextEntryGetState();
        if state == 2 {
            if ArraySize(this.m_pickerResults) > 0 {
                this.ChooseItem(this.m_pickerResults[0]);
            }
            this.ClosePicker();
            this.Refresh();
            return;
        }
        if state != 1 {
            this.StopPickerTyping();
            this.RefreshPicker();
            return;
        }
        let text = APM_TextEntryGetText();
        if !Equals(text, this.m_pickerQuery) {
            this.m_pickerQuery = text;
            this.m_pickerPage = 0;
            this.UpdatePickerResults();
        }
    }

    private func RunPickerButton(arg: String) {
        if Equals(arg, "close") {
            this.ClosePicker();
        } else if Equals(arg, "search") {
            this.StartPickerTyping();
        } else if StrBeginsWith(arg, "tab") {
            this.m_pickerCategory = StringToInt(StrAfterFirst(arg, "tab"));
            this.m_pickerPage = 0;
            this.UpdatePickerResults();
        } else if Equals(arg, "prev") {
            this.m_pickerPage = Max(0, this.m_pickerPage - 1);
            this.RefreshPicker();
        } else if Equals(arg, "next") {
            this.m_pickerPage += 1;
            this.UpdatePickerResults();
        } else {
            let result = this.m_pickerPage * this.PickerRows() + StringToInt(arg);
            if result >= 0 && result < ArraySize(this.m_pickerResults) {
                this.ChooseItem(this.m_pickerResults[result]);
                this.RefreshPicker();
            }
        }
        this.Refresh();
    }

    private func ChooseItem(index: Int32) {
        let id = this.m_itemIds[index];
        APM_HistoryCheckpoint();
        APM_SetHandProp(this.m_propRight, TDBID.ToNumber(id));
        ArrayRemove(this.m_recentItems, id);
        ArrayInsert(this.m_recentItems, 0, id);
        if ArraySize(this.m_recentItems) > 8 {
            ArrayPop(this.m_recentItems);
        }
    }

    // arg: "open:r", "clear:l", "restore:r"...
    private func RunPropButton(arg: String) {
        let action = StrBeforeFirst(arg, ":");
        let right = Equals(StrAfterFirst(arg, ":"), "r");
        let hasPose = right ? this.m_hasPoseItemRight : this.m_hasPoseItemLeft;
        if Equals(action, "clear") {
            APM_HistoryCheckpoint();
            APM_SetHandProp(right, hasPose ? 1ul : 0ul);
            return;
        }
        if Equals(action, "restore") {
            APM_HistoryCheckpoint();
            APM_SetHandProp(right, 0ul);
            return;
        }
        if Equals(action, "open") {
            if this.m_pickerOpen && Equals(this.m_propRight, right) {
                this.ClosePicker();
            } else {
                this.m_propRight = right;
                if this.m_pickerOpen {
                    this.m_pickerPage = 0;
                    this.UpdatePickerResults();
                } else {
                    this.OpenPicker();
                }
            }
        }
    }

    // A hand that keeps the Photo Mode pose's own item saves that item, so the pose loads with it on any pose.
    private func SavePoseWithItems(name: String) {
        let right = APM_GetHandProp(true);
        let left = APM_GetHandProp(false);
        if right == 0ul && this.m_hasPoseItemRight {
            APM_SetHandProp(true, TDBID.ToNumber(ItemID.GetTDBID(this.m_poseItemRight)));
        }
        if left == 0ul && this.m_hasPoseItemLeft {
            APM_SetHandProp(false, TDBID.ToNumber(ItemID.GetTDBID(this.m_poseItemLeft)));
        }
        APM_SavePose(name);
        APM_SetHandProp(true, right);
        APM_SetHandProp(false, left);
    }

    private func ItemName(record: Uint64) -> String {
        let itemRecord = TweakDBInterface.GetItemRecord(APM_RecordFromNumber(record));
        return IsDefined(itemRecord) ? GetLocalizedItemNameByCName(itemRecord.DisplayName()) : "Item";
    }

    private func RefreshProps() {
        this.RefreshHand(true);
        this.RefreshHand(false);
    }

    private func RefreshHand(right: Bool) {
        let suffix = right ? "r" : "l";
        let held = APM_GetHandProp(right);
        let hasPose = right ? this.m_hasPoseItemRight : this.m_hasPoseItemLeft;
        let shows = held > 1ul || (held == 0ul && hasPose);
        let openButton = this.FindButton("prop:open:" + suffix);
        if IsDefined(openButton) {
            let text = APM_Text("APM-Item-Nothing", "Nothing - click to choose");
            if held > 1ul {
                text = this.ItemName(held);
            } else if held == 0ul && hasPose {
                let poseItem = right ? this.m_poseItemRight : this.m_poseItemLeft;
                text = APM_TextWith("APM-Item-FromPose", "{0} (pose)", this.ItemName(TDBID.ToNumber(ItemID.GetTDBID(poseItem))));
            }
            openButton.label.SetText(text);
            openButton.isSelected = this.m_pickerOpen && Equals(this.m_propRight, right);
            openButton.isMarked = held > 1ul;
            openButton.UpdateLook();
        }
        this.SetButton("prop:clear:" + suffix, false, shows);
        let restoreButton = this.FindButton("prop:restore:" + suffix);
        if IsDefined(restoreButton) {
            restoreButton.root.SetVisible(hasPose && held != 0ul);
        }
    }

    // While a hand keeps the Photo Mode pose's own item, remember what that item is.
    private func ReadPoseItem(puppet: ref<GameObject>, right: Bool) -> Bool {
        let slot = right ? t"AttachmentSlots.WeaponRight" : t"AttachmentSlots.WeaponLeft";
        let item = GameInstance.GetTransactionSystem(puppet.GetGame()).GetItemInSlot(puppet, slot);
        let has = IsDefined(item) && ItemID.IsValid(item.GetItemID());
        let changed = false;
        if right {
            changed = !Equals(has, this.m_hasPoseItemRight) || (has && !Equals(item.GetItemID(), this.m_poseItemRight));
            this.m_hasPoseItemRight = has;
            if has {
                this.m_poseItemRight = item.GetItemID();
            }
        } else {
            changed = !Equals(has, this.m_hasPoseItemLeft) || (has && !Equals(item.GetItemID(), this.m_poseItemLeft));
            this.m_hasPoseItemLeft = has;
            if has {
                this.m_poseItemLeft = item.GetItemID();
            }
        }
        return changed;
    }

    // Puts the chosen items on the Photo Mode puppet (also after loading a pose or undo / redo).
    private func SyncProps(timeDelta: Float) {
        if !APM_IsPhotoModeActive() {
            this.RestorePlayerHands(); // if leaving did not go through the exit callbacks
            this.m_shownRight = 0ul;
            this.m_shownLeft = 0ul;
            this.m_checkTimer = 0.0;
            this.m_hasPoseItemRight = false;
            this.m_hasPoseItemLeft = false;
            this.ResetCharacterProps();
            return;
        }
        let right = APM_GetHandProp(true);
        let left = APM_GetHandProp(false);
        if right != this.m_shownRight && this.PlaceProp(true, right) {
            this.m_shownRight = right;
            this.m_poseItemDelay = 0.5;
        }
        if left != this.m_shownLeft && this.PlaceProp(false, left) {
            this.m_shownLeft = left;
            this.m_poseItemDelay = 0.5;
        }
        // The pose's own item (e.g. the gun of an "Action" pose), read while that hand keeps it.
        if this.m_poseItemDelay > 0.0 {
            this.m_poseItemDelay -= timeDelta;
        } else {
            let puppet = this.SelectedPuppet();
            if IsDefined(puppet) {
                let changed = false;
                if right == 0ul && this.ReadPoseItem(puppet, true) {
                    changed = true;
                }
                if left == 0ul && this.ReadPoseItem(puppet, false) {
                    changed = true;
                }
                if changed {
                    this.RefreshProps();
                }
            }
        }
        // Some items have no model that can be held: they show nothing. Take them out of the hand and the list.
        if this.m_checkTimer > 0.0 {
            this.m_checkTimer -= timeDelta;
            if this.m_checkTimer <= 0.0 {
                this.CheckPlacedItem();
            }
        }
    }

    private func CheckPlacedItem() {
        let puppet = this.SelectedPuppet();
        if !IsDefined(puppet) || APM_GetHandProp(this.m_checkRight) != this.m_checkRecord {
            return;
        }
        let slot = this.m_checkRight ? t"AttachmentSlots.WeaponRight" : t"AttachmentSlots.WeaponLeft";
        if IsDefined(GameInstance.GetTransactionSystem(puppet.GetGame()).GetItemInSlot(puppet, slot)) {
            return;
        }
        ArrayPush(this.m_noModelItems, this.m_checkRecord);
        APM_SetHandProp(this.m_checkRight, 0ul);
        let record = TweakDBInterface.GetItemRecord(APM_RecordFromNumber(this.m_checkRecord));
        this.SetStatus(APM_TextWith("APM-Msg-NoModel", "{0} has no model to hold", IsDefined(record) ? GetLocalizedItemNameByCName(record.DisplayName()) : APM_Text("APM-Item-ThisItem", "This item")));
        APM_LogInfo("Items: no model to hold, removed from the list");
        if this.m_pickerOpen {
            this.UpdatePickerResults();
        }
    }

    private func PlaceProp(right: Bool, record: Uint64) -> Bool {
        let puppet = this.SelectedPuppet();
        if !IsDefined(puppet) {
            return false;
        }
        let transactions = GameInstance.GetTransactionSystem(puppet.GetGame());
        let slot = right ? t"AttachmentSlots.WeaponRight" : t"AttachmentSlots.WeaponLeft";
        let hasPose = right ? this.m_hasPoseItemRight : this.m_hasPoseItemLeft;
        let poseItem = right ? this.m_poseItemRight : this.m_poseItemLeft;
        // The pose's own item stays in the inventory, so that it can be given back.
        let current = transactions.GetItemInSlot(puppet, slot);
        let isPoseItem = hasPose && IsDefined(current) && Equals(current.GetItemID(), poseItem);
        let isPlayer = this.IsPlayer(puppet);
        if isPlayer {
            this.NotePlayerHand(right, current); // the player's own inventory is never emptied
        }
        transactions.RemoveItemFromSlot(puppet, slot, !isPoseItem && !isPlayer);
        if record == 0ul {
            if hasPose {
                if !transactions.HasItem(puppet, poseItem) {
                    transactions.GiveItem(puppet, poseItem, 1);
                }
                transactions.AddItemToSlot(puppet, slot, poseItem, true, null, ERenderingPlane.RPl_Scene, false, true);
            }
            return true;
        }
        if record == 1ul {
            return true; // emptied on purpose
        }
        let id = APM_RecordFromNumber(record);
        let itemRecord = TweakDBInterface.GetItemRecord(id);
        if !IsDefined(itemRecord) {
            return true; // unknown item (e.g. from a removed mod): nothing to place
        }
        let isWeapon = IsDefined(itemRecord as WeaponItem_Record);
        let itemID = isWeapon ? ItemID.CreateQuery(id) : ItemID.FromTDBID(id);
        if !transactions.HasItem(puppet, itemID) {
            transactions.GiveItem(puppet, itemID, 1);
            if isPlayer {
                ArrayPush(this.m_playerGivenItems, itemID);
            }
        }
        let placed = transactions.AddItemToSlot(puppet, slot, itemID, true, null, ERenderingPlane.RPl_Scene, false, true);
        APM_LogInfo("Items: " + GetLocalizedItemNameByCName(itemRecord.DisplayName()) + (placed ? " placed in the " : " could not be placed in the ") + (right ? "right" : "left") + " hand");
        this.m_checkTimer = 3.0;
        this.m_checkRight = right;
        this.m_checkRecord = record;
        return true;
    }

    private func IsPlayer(puppet: ref<GameObject>) -> Bool {
        let player = this.m_controller.GetPlayerControlledObject();
        return IsDefined(puppet) && IsDefined(player) && Equals(puppet.GetEntityID(), player.GetEntityID());
    }

    // First change of a hand of the player: what it held, to give it back when leaving Photo Mode.
    private func NotePlayerHand(right: Bool, current: ref<ItemObject>) {
        while ArraySize(this.m_playerHandTouched) < 2 {
            ArrayPush(this.m_playerHandTouched, false);
            ArrayPush(this.m_playerHandHad, false);
            ArrayPush(this.m_playerHandItem, ItemID.None());
        }
        let hand = right ? 1 : 0;
        if this.m_playerHandTouched[hand] {
            return;
        }
        this.m_playerHandTouched[hand] = true;
        this.m_playerHandHad[hand] = IsDefined(current) && ItemID.IsValid(current.GetItemID());
        this.m_playerHandItem[hand] = this.m_playerHandHad[hand] ? current.GetItemID() : ItemID.None();
    }

    // Leaving Photo Mode after items were placed on the player itself: its hands as they were, and the items the mod
    // gave taken out of its inventory (they would stay equipped as the weapon to use in the vehicle).
    public func RestorePlayerHands() {
        let touched = false;
        let i = 0;
        while i < ArraySize(this.m_playerHandTouched) {
            touched = touched || this.m_playerHandTouched[i];
            i += 1;
        }
        if !touched && ArraySize(this.m_playerGivenItems) == 0 {
            return;
        }
        let player = this.m_controller.GetPlayerControlledObject();
        if IsDefined(player) {
            let transactions = GameInstance.GetTransactionSystem(player.GetGame());
            let hand = 0;
            while hand < ArraySize(this.m_playerHandTouched) {
                if this.m_playerHandTouched[hand] {
                    transactions.RemoveItemFromSlot(player, hand == 1 ? t"AttachmentSlots.WeaponRight" : t"AttachmentSlots.WeaponLeft", false);
                }
                hand += 1;
            }
            i = 0;
            while i < ArraySize(this.m_playerGivenItems) {
                if transactions.HasItem(player, this.m_playerGivenItems[i]) {
                    transactions.RemoveItem(player, this.m_playerGivenItems[i], 1);
                }
                i += 1;
            }
            hand = 0;
            while hand < ArraySize(this.m_playerHandTouched) {
                if this.m_playerHandTouched[hand] && this.m_playerHandHad[hand] && transactions.HasItem(player, this.m_playerHandItem[hand]) {
                    transactions.AddItemToSlot(player, hand == 1 ? t"AttachmentSlots.WeaponRight" : t"AttachmentSlots.WeaponLeft", this.m_playerHandItem[hand], true, null, ERenderingPlane.RPl_Scene, false, true);
                }
                hand += 1;
            }
            APM_LogInfo("Items: the player's hands given back as before Photo Mode");
        }
        ArrayClear(this.m_playerHandTouched);
        ArrayClear(this.m_playerHandHad);
        ArrayClear(this.m_playerHandItem);
        ArrayClear(this.m_playerGivenItems);
    }

    // --- Pose names ----------------------------------------------------------------------------------------------

    private func StartRename(name: String) {
        if this.m_pickerOpen {
            this.ClosePicker();
        }
        if !APM_TextEntryBegin(this.EntryText(name)) { // its own name: the file stays in its folder
            this.SetStatus(APM_Text("APM-Msg-TypingUnavailable", "Typing is not available"));
            return;
        }
        this.m_renaming = true;
        this.m_renameFrom = name;
        this.m_selectedPose = name;
        this.ShowPose(name);
    }

    private func FinishRename(confirm: Bool) {
        let text = APM_TextEntryGetText();
        APM_TextEntryEnd();
        this.m_renaming = false;
        if !confirm || Equals(text, this.EntryText(this.m_renameFrom)) {
            return;
        }
        let kind = this.LibraryKind();
        let renamed = kind == 2 ? APM_FaceRename(this.m_renameFrom, text) : (kind == 1 ? APM_RenameHandPose(this.m_renameFrom, text) : APM_RenamePose(this.m_renameFrom, text));
        if StrLen(renamed) > 0 {
            this.m_selectedPose = renamed;
        } else {
            this.SetStatus(APM_TextWith("APM-Msg-RenameFailed", "Could not rename {0}", this.m_renameFrom));
        }
        this.ReloadPoses();
        this.ShowPose(this.m_selectedPose);
    }

    // While typing: show the text with a cursor on the pose's button, react to Enter / Esc.
    private func UpdateRename() {
        if !this.m_renaming {
            return;
        }
        if !APM_IsPhotoModeActive() {
            this.FinishRename(false);
            return;
        }
        let state = APM_TextEntryGetState();
        if state != 1 {
            this.FinishRename(state == 2);
            APM_UiRequestRefresh();
            this.Refresh();
            return;
        }
        let first = this.m_posePage * APM_PosesPerPage();
        let i = 0;
        while i < ArraySize(this.m_poseButtons) {
            let index = first + i;
            if index < ArraySize(this.m_poseNames) && Equals(this.m_poseNames[index], this.m_renameFrom) {
                this.m_poseButtons[i].label.SetText(APM_TextEntryGetText() + "_");
            }
            i += 1;
        }
    }

    private func ShowPose(name: String) {
        let index = 0;
        while index < ArraySize(this.m_poseNames) {
            if Equals(this.m_poseNames[index], name) {
                this.m_posePage = index / APM_PosesPerPage();
                return;
            }
            index += 1;
        }
    }

    // Right mouse button pressed over a marker: reset that part. Polled, so it works whatever the button's input action.
    private func UpdateRightClick() {
        let down = APM_IsRightMouseDown();
        let pressed = down && !this.m_rightWasDown;
        this.m_rightWasDown = down;
        if !pressed || IsDefined(this.m_dragMarker) || !this.m_gizmosOn || !APM_IsPhotoModeActive() {
            return;
        }
        let i = 0;
        while i < ArraySize(this.m_markers) {
            if this.m_markers[i].isHovered && this.m_markers[i].root.IsVisible() {
                this.ResetPart(this.m_markers[i].part);
                return;
            }
            i += 1;
        }
    }

    // Undoes the edits of one part (selects it), as the panel's Reset button.
    private func ResetPart(part: Int32) {
        if part < 0 {
            return;
        }
        let label = APM_ShortPartLabel(APM_UiGetOptionLabel(7701u, part));
        let resetButton = this.FindButton("partreset");
        if IsDefined(resetButton) {
            this.m_lastButton = resetButton; // messages show above the part's Reset button
        }
        if APM_GizmoIsPartLocked(part) {
            this.SetStatus(APM_TextWith("APM-Msg-TurnedByLookAt", "{0} is turned by Look at camera", APM_PartText(label)));
            return;
        }
        APM_UiSetValue(7701u, Cast<Float>(part));
        if !APM_UiIsPartEdited(part) {
            APM_UiRequestRefresh();
            this.Refresh();
            return;
        }
        APM_HistoryCheckpoint();
        APM_UiSetValue(7721u, 1.0);
        APM_UiRequestRefresh();
        this.Refresh();
    }

    // Small mark in the top right corner of a button, hidden until SetDot.
    private func AddDot(button: ref<APM_Button>) {
        let s = APM_UiScale();
        let dot = new inkRectangle();
        dot.SetAnchor(inkEAnchor.TopLeft);
        dot.SetMargin((button.w - 12.0) * s, 5.0 * s, 0.0, 0.0);
        dot.SetSize(7.0 * s, 7.0 * s);
        dot.SetInteractive(false);
        dot.SetVisible(false);
        dot.Reparent(button.root);
        button.dot = dot;
    }

    // Small icon at one end of a list entry, hidden until shown: the lock of the built-in entries (right, they can only
    // be loaded), the folder icon and the ".." arrow (left).
    private func AddRowIcon(button: ref<APM_Button>, name: String, left: Bool) -> wref<inkCanvas> {
        let s = APM_UiScale();
        let size = 18.0;
        let lock = new inkCanvas();
        lock.SetAnchor(inkEAnchor.TopLeft);
        lock.SetMargin((left ? 10.0 : button.w - size - 10.0) * s, (button.h - size) * 0.5 * s, 0.0, 0.0);
        lock.SetSize(size * s, size * s);
        lock.SetInteractive(false);
        lock.SetVisible(false);
        lock.Reparent(button.root);
        let cell = size / 24.0;
        let spans = APM_IconSpans(name);
        let i = 0;
        while i + 3 < ArraySize(spans) {
            let rect = new inkRectangle();
            rect.SetAnchor(inkEAnchor.TopLeft);
            rect.SetMargin(spans[i] * cell * s, spans[i + 1] * cell * s, 0.0, 0.0);
            rect.SetSize(spans[i + 2] * cell * s, cell * s);
            rect.SetOpacity(spans[i + 3]);
            rect.SetInteractive(false);
            rect.Reparent(lock);
            ArrayPush(button.icon, rect); // tinted with the button
            i += 4;
        }
        return lock;
    }

    private func SetDot(button: ref<APM_Button>, on: Bool) {
        if !IsDefined(button) || !IsDefined(button.dot) {
            return;
        }
        button.dot.SetVisible(on);
        button.dot.SetTintColor(button.isSelected ? APM_ColorDark() : APM_ColorAccent());
    }

    // Marks the edited parts and the tabs that hold them; enables the part's Reset button.
    private func RefreshEditMarks(part: Int32) {
        let count = APM_UiGetOptionCount(7701u);
        let edited: array<Bool>;
        let i = 0;
        while i < count {
            ArrayPush(edited, APM_UiIsPartEdited(i));
            i += 1;
        }
        let g = 0;
        while g < ArraySize(this.m_groupButtons) {
            let any = false;
            i = 0;
            while i < count && !any {
                any = edited[i] && Equals(APM_UiGetOptionGroup(7701u, i), this.m_groupNames[g]);
                i += 1;
            }
            this.SetDot(this.m_groupButtons[g], any);
            g += 1;
        }
        let slot = 0;
        while slot < ArraySize(this.m_partButtons) {
            let index = this.m_partIndices[slot];
            let on = index >= 0 && index < count && edited[index];
            if index >= 0 && Equals(APM_UiGetOptionLabel(7701u, index), "Eyes (both)") {
                on = on || (this.m_leftEyePart >= 0 && edited[this.m_leftEyePart]) || (this.m_rightEyePart >= 0 && edited[this.m_rightEyePart]);
            }
            this.SetDot(this.m_partButtons[slot], on);
            slot += 1;
        }
        let resetButton = this.FindButton("partreset");
        if IsDefined(resetButton) {
            let eyePart = this.IsSingleEye(part) || Equals(APM_UiGetOptionLabel(7701u, part), "Eyes (both)");
            // The eye switch takes the right side of the title row: the button moves left of it.
            resetButton.root.SetMargin((eyePart ? 284.0 : 530.0) * APM_UiScale(), 4.0 * APM_UiScale(), 0.0, 0.0);
            resetButton.isEnabled = part >= 0 && part < count && edited[part] && !APM_GizmoIsPartLocked(part);
            resetButton.UpdateLook();
        }
    }

    // --- Refresh -------------------------------------------------------------------------------------------------

    public func Refresh() {
        if !IsDefined(this.m_panel) {
            return;
        }

        // Header
        let enabled = APM_UiGetValue(7700u) > 0.5;
        let enabledButton = this.FindButton("enabled");
        if IsDefined(enabledButton) {
            enabledButton.label.SetText(enabled ? APM_Text("APM-Button-PoseOn", "Pose on") : APM_Text("APM-Button-PoseOff", "Pose off"));
        }
        this.SetButton("enabled", enabled, true);
        this.SetButton("gizmos", this.m_gizmosOn, true);
        let mode = 0;
        while mode < 3 {
            this.SetButton("classic:" + IntToString(mode), mode == this.m_classicMode, true);
            mode += 1;
        }
        let axesLocal = this.FindButton("axes:local");
        let axesGlobal = this.FindButton("axes:global");
        if IsDefined(axesLocal) && IsDefined(axesGlobal) {
            axesLocal.root.SetVisible(!this.m_classicModelAxes);
            axesGlobal.root.SetVisible(this.m_classicModelAxes);
        }
        this.SetButton("axes:local", false, this.m_classicMode != APM_ClassicOff());
        this.SetButton("axes:global", false, this.m_classicMode != APM_ClassicOff());

        // Body part tabs
        let part = Cast<Int32>(APM_UiGetValue(7701u));
        let group = APM_UiGetOptionGroup(7701u, part);
        let groupIndex = this.GroupIndexOf(group);
        let i = 0;
        while i < ArraySize(this.m_groupButtons) {
            this.m_groupButtons[i].isSelected = i == groupIndex;
            this.m_groupButtons[i].UpdateLook();
            i += 1;
        }

        let count = APM_UiGetOptionCount(7701u);
        let slot = 0;
        i = 0;
        while i < count && slot < ArraySize(this.m_partButtons) {
            if Equals(APM_UiGetOptionGroup(7701u, i), group) && !APM_GizmoIsPartDetail(i) && !this.IsSingleEye(i) {
                this.m_partIndices[slot] = i;
                let button = this.m_partButtons[slot];
                button.root.SetVisible(true);
                let partLabel = APM_UiGetOptionLabel(7701u, i);
                let isEyes = Equals(partLabel, "Eyes (both)");
                button.label.SetText(isEyes ? APM_Text("APM-PartShort-Eyes", "Eyes") : APM_PartShortText(partLabel));
                button.isSelected = i == part || (isEyes && this.IsSingleEye(part));
                button.UpdateLook();
                slot += 1;
            }
            i += 1;
        }
        while slot < ArraySize(this.m_partButtons) {
            this.m_partIndices[slot] = -1;
            this.m_partButtons[slot].root.SetVisible(false);
            slot += 1;
        }
        this.m_partTitle.SetText(APM_PartText(APM_UiGetOptionLabel(7701u, part)));
        this.RefreshEditMarks(part);
        // Eyes: both together, or one at a time
        let eyePart = this.IsSingleEye(part) || Equals(APM_UiGetOptionLabel(7701u, part), "Eyes (both)");
        let e = 0;
        while e < 3 {
            let eyeButton = this.FindButton("eyes:" + IntToString(e));
            if IsDefined(eyeButton) {
                eyeButton.root.SetVisible(eyePart);
                eyeButton.isSelected = (e == 0 && !this.IsSingleEye(part)) || (e == 1 && part == this.m_leftEyePart) || (e == 2 && part == this.m_rightEyePart);
                eyeButton.UpdateLook();
            }
            e += 1;
        }

        // Both eyes: the third row is the focus distance instead of the twist.
        let thirdRow = this.FindRow(7704u);
        if IsDefined(thirdRow) {
            if Equals(APM_UiGetOptionLabel(7701u, part), "Eyes (both)") {
                thirdRow.label.SetText(APM_Text("APM-Row-FocusDistance", "Focus distance"));
                thirdRow.unit = " cm";
                thirdRow.minValue = 10.0;
                thirdRow.maxValue = 1000.0;
                thirdRow.step = 5.0;
                thirdRow.dragScale = 1.0;
            } else {
                thirdRow.label.SetText(APM_Text("APM-Row-Twist", "Twist"));
                thirdRow.unit = "";
                thirdRow.minValue = -180.0;
                thirdRow.maxValue = 180.0;
                thirdRow.step = 1.0;
                thirdRow.dragScale = 0.2;
            }
        }

        // Value rows
        i = 0;
        while i < ArraySize(this.m_rows) {
            let row = this.m_rows[i];
            let rowEnabled = APM_UiIsAttributeEnabled(row.attribute);
            let isDragged = IsDefined(this.m_dragRow) && this.m_dragRow.attribute == row.attribute;
            if !isDragged {
                row.value.label.SetText(this.FormatValue(row, this.RowValue(row)));
            }
            row.value.isEnabled = rowEnabled;
            row.value.UpdateLook();
            row.label.SetOpacity(rowEnabled ? 1.0 : 0.35);
            let b = 0;
            while b < ArraySize(row.buttons) {
                row.buttons[b].isEnabled = rowEnabled;
                row.buttons[b].UpdateLook();
                b += 1;
            }
            i += 1;
        }
        let resetAllButton = this.FindButton("resetall");
        if IsDefined(resetAllButton) {
            if this.m_faceMode {
                resetAllButton.tooltip = this.m_confirmResetAll ? APM_Text("APM-Tip-ResetFaceConfirm", "Click again to reset the face") : APM_Text("APM-Tip-ResetFace", "Reset the face (click twice): the Photo Mode expression");
            } else {
                resetAllButton.tooltip = this.m_confirmResetAll ? APM_Text("APM-Tip-ResetBodyConfirm", "Click again to reset the body") : APM_Text("APM-Tip-ResetBody", "Reset the body (click twice): pose and items in hand (the face stays)");
            }
            resetAllButton.isMarked = this.m_confirmResetAll;
            resetAllButton.UpdateLook();
            if this.m_confirmResetAll {
                resetAllButton.label.SetTintColor(APM_ColorWarning());
            }
        }

        // IK
        let limb = Cast<Int32>(APM_UiGetValue(7710u));
        let limbs = APM_UiGetOptionCount(7710u);
        i = 0;
        while i < limbs {
            this.SetButton("ik:" + IntToString(i), i == limb, true);
            this.SetOutline("ik:" + IntToString(i), APM_GizmoIsIkActive(i));
            i += 1;
        }
        let pin = Cast<Int32>(APM_UiGetValue(7711u) + 0.5);
        i = 0;
        while i < 3 {
            this.SetButton("pin:" + IntToString(i), i == pin, true);
            i += 1;
        }

        // Pose library
        let first = this.m_posePage * APM_PosesPerPage();
        i = 0;
        while i < ArraySize(this.m_poseButtons) {
            let button = this.m_poseButtons[i];
            let index = first + i;
            if index < ArraySize(this.m_poseNames) {
                button.root.SetVisible(true);
                this.m_poseLocks[i].SetVisible(this.IsBuiltIn(this.m_poseNames[index]));
                this.m_poseUpIcons[i].SetVisible(Equals(this.m_poseNames[index], "::up"));
                this.m_poseFolderIcons[i].SetVisible(StrEndsWith(this.m_poseNames[index], "/"));
                let isRenamed = this.m_renaming && Equals(this.m_poseNames[index], this.m_renameFrom);
                button.label.SetText(isRenamed ? APM_TextEntryGetText() + "_" : this.EntryText(this.m_poseNames[index]));
                button.isSelected = Equals(this.m_poseNames[index], this.m_selectedPose);
                button.isMarked = this.m_renaming && Equals(this.m_poseNames[index], this.m_renameFrom);
                button.UpdateLook();
            } else {
                button.root.SetVisible(false);
            }
            i += 1;
        }
        // The open folder after the title ("Poses / Some mod"); the page is under the list.
        let openFolder = this.LibraryFolder();
        let pageText = StrLen(openFolder) == 0 ? "" : " / " + (Equals(openFolder, "::builtin/") ? APM_Text("APM-Folder-BuiltIn", "Built-in") : StrLeft(openFolder, StrLen(openFolder) - 1));
        this.m_posesPageText.SetText(IntToString(this.m_posePage + 1) + "/" + IntToString(this.PageCount()));
        let kind = this.LibraryKind();
        if kind == 2 {
            this.m_posesTitle.SetText(APM_Text("APM-Title-Expressions", "Expressions") + pageText);
        } else if kind == 1 {
            let hand = this.PresetHand();
            let handText = hand == 1 ? APM_Text("APM-Label-HandPoseLeft", "left hand") : (hand == 2 ? APM_Text("APM-Label-HandPoseRight", "right hand") : "");
            this.m_posesTitle.SetText(APM_Text("APM-Title-HandPoses", "Hand poses") + pageText + (StrLen(handText) > 0 ? "  - " + handText : ""));
        } else {
            this.m_posesTitle.SetText(APM_Text("APM-Title-Poses", "Poses") + pageText);
        }
        this.SetButton("lib:body", !this.m_handLibrary, true);
        this.SetButton("lib:hands", this.m_handLibrary, true);
        this.SetButton("autoik", this.m_autoIk, true);
        this.RefreshCharacter();
        let limitsOn = this.FindButton("limits:on");
        let limitsOff = this.FindButton("limits:off");
        if IsDefined(limitsOn) && IsDefined(limitsOff) {
            limitsOn.root.SetVisible(APM_AreLimitsOn());
            limitsOff.root.SetVisible(!APM_AreLimitsOn());
            limitsOff.isMarked = true;
            limitsOff.UpdateLook();
        }
        let openPoses = this.FindButton("poses:open");
        let closePoses = this.FindButton("poses:close");
        if IsDefined(openPoses) && IsDefined(closePoses) {
            openPoses.root.SetVisible(!this.m_posesOpen);
            closePoses.root.SetVisible(this.m_posesOpen);
        }
        let hasSelection = StrLen(this.m_selectedPose) > 0;
        let hasFile = hasSelection && !this.IsBuiltIn(this.m_selectedPose); // built-in entries can only be loaded
        this.SetButton("overwrite", false, hasFile);
        this.SetButton("load", false, hasSelection);
        this.RefreshProps();
        this.RefreshFace();
        this.SetButton("mirror", APM_IsMirror(), true);
        this.SetButton("undo", false, APM_CanUndo());
        this.SetButton("redo", false, APM_CanRedo());
        this.SetButton("rename", this.m_renaming, hasFile);
        this.SetButton("delete", false, hasFile);
        let overwriteButton = this.FindButton("overwrite");
        if IsDefined(overwriteButton) {
            overwriteButton.label.SetText(this.m_confirmOverwrite ? APM_Text("APM-Button-ConfirmOverwrite", "Confirm overwrite") : APM_Text("APM-Button-Overwrite", "Overwrite"));
            if this.m_confirmOverwrite {
                overwriteButton.label.SetTintColor(APM_ColorWarning());
            }
        }
        let deleteButton = this.FindButton("delete");
        if IsDefined(deleteButton) {
            deleteButton.label.SetText(this.m_confirmDelete ? APM_Text("APM-Button-ConfirmDelete", "Confirm delete") : APM_Text("APM-Button-Delete", "Delete"));
            if this.m_confirmDelete {
                deleteButton.label.SetTintColor(APM_ColorWarning());
            }
        }
        this.SetButton("pageprev", false, this.m_posePage > 0);
        this.SetButton("pagenext", false, this.m_posePage < this.PageCount() - 1);
        this.Layout();
    }

    // --- Gizmos --------------------------------------------------------------------------------------------------

    // Camera distance below which the finger gizmos appear (whole fingers, then their segments).
    private func FingerDistance() -> Float {
        return 1.3;
    }

    private func FingerDetailDistance() -> Float {
        return 0.8;
    }

    // How far in front of the eyes their "look" marker floats (meters).
    private func IsSingleEye(part: Int32) -> Bool {
        return part == this.m_leftEyePart || part == this.m_rightEyePart;
    }

    private func SetEyesTogether(together: Bool) {
        if !Equals(this.m_eyesTogether, together) {
            this.m_eyesTogether = together;
            APM_SessionSet("EyesTogether", together ? 1.0 : 0.0);
        }
    }

    // Point one eye looks at, that far ahead.
    private func SingleEyePoint(toWorld: Matrix, part: Int32, distance: Float) -> Vector4 {
        let eye = APM_PartWorldPosition(toWorld, part);
        let forwardModel = APM_GizmoGetPartForward(part);
        if eye.W < 0.5 || forwardModel.W < 0.5 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let forward = Vector4.TransformDir(toWorld, APM_Vector(forwardModel.X, forwardModel.Y, forwardModel.Z, 0.0));
        let length = SqrtF(forward.X * forward.X + forward.Y * forward.Y + forward.Z * forward.Z);
        if length < 0.0001 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let k = distance / length;
        return APM_Vector(eye.X + forward.X * k, eye.Y + forward.Y * k, eye.Z + forward.Z * k, 1.0);
    }

    private func LookDistance() -> Float {
        return 0.6;
    }

    // World point both eyes look at: where their lines of sight meet (or LookDistance() ahead when they do not
    // meet); w = 1 when valid.
    // While dragging the eyes: the point they really look at (they stop at their limit), as far as the cursor's point.
    private func EyeDirectionPoint(toWorld: Matrix, target: Vector4) -> Vector4 {
        let left = APM_PartWorldPosition(toWorld, this.m_leftEyePart);
        let right = APM_PartWorldPosition(toWorld, this.m_rightEyePart);
        let leftModel = APM_GizmoGetPartForward(this.m_leftEyePart);
        let rightModel = APM_GizmoGetPartForward(this.m_rightEyePart);
        if left.W < 0.5 || right.W < 0.5 || leftModel.W < 0.5 || rightModel.W < 0.5 {
            return target;
        }
        let gl = Vector4.TransformDir(toWorld, APM_Vector(leftModel.X, leftModel.Y, leftModel.Z, 0.0));
        let gr = Vector4.TransformDir(toWorld, APM_Vector(rightModel.X, rightModel.Y, rightModel.Z, 0.0));
        let dx = gl.X + gr.X;
        let dy = gl.Y + gr.Y;
        let dz = gl.Z + gr.Z;
        let length = SqrtF(dx * dx + dy * dy + dz * dz);
        if length < 0.0001 {
            return target;
        }
        let mx = (left.X + right.X) * 0.5;
        let my = (left.Y + right.Y) * 0.5;
        let mz = (left.Z + right.Z) * 0.5;
        let tx = target.X - mx;
        let ty = target.Y - my;
        let tz = target.Z - mz;
        let distance = SqrtF(tx * tx + ty * ty + tz * tz);
        return APM_Vector(mx + dx / length * distance, my + dy / length * distance, mz + dz / length * distance, 1.0);
    }

    private func EyeLookPoint(toWorld: Matrix) -> Vector4 {
        let left = APM_PartWorldPosition(toWorld, this.m_leftEyePart);
        let right = APM_PartWorldPosition(toWorld, this.m_rightEyePart);
        let leftModel = APM_GizmoGetPartForward(this.m_leftEyePart);
        let rightModel = APM_GizmoGetPartForward(this.m_rightEyePart);
        if left.W < 0.5 || right.W < 0.5 || leftModel.W < 0.5 || rightModel.W < 0.5 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let gl = Vector4.TransformDir(toWorld, APM_Vector(leftModel.X, leftModel.Y, leftModel.Z, 0.0));
        let gr = Vector4.TransformDir(toWorld, APM_Vector(rightModel.X, rightModel.Y, rightModel.Z, 0.0));
        let ll = SqrtF(gl.X * gl.X + gl.Y * gl.Y + gl.Z * gl.Z);
        let lr = SqrtF(gr.X * gr.X + gr.Y * gr.Y + gr.Z * gr.Z);
        if ll < 0.0001 || lr < 0.0001 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        gl = APM_Vector(gl.X / ll, gl.Y / ll, gl.Z / ll, 0.0);
        gr = APM_Vector(gr.X / lr, gr.Y / lr, gr.Z / lr, 0.0);
        let mx = (left.X + right.X) * 0.5;
        let my = (left.Y + right.Y) * 0.5;
        let mz = (left.Z + right.Z) * 0.5;
        if this.m_eyeLookDistance <= 0.0 {
            this.m_eyeLookDistance = this.LookDistance();
        }
        // Dropped by the user: it stays at that distance until the eyes turn elsewhere (more than ~5 degrees: reset,
        // Look at camera, another pose), rather than moving to where the lines of sight meet.
        let dragging = IsDefined(this.m_dragMarker) && this.m_dragMode == APM_DragEyeLook();
        if this.m_eyeLookPinned && !dragging {
            let fx = leftModel.X + rightModel.X;
            let fy = leftModel.Y + rightModel.Y;
            let fz = leftModel.Z + rightModel.Z;
            let fl = SqrtF(fx * fx + fy * fy + fz * fz);
            if fl > 0.0001 {
                if this.m_eyeLookPinForward.W < 0.5 {
                    this.m_eyeLookPinForward = APM_Vector(fx / fl, fy / fl, fz / fl, 1.0);
                } else {
                    let p = this.m_eyeLookPinForward;
                    if (p.X * fx + p.Y * fy + p.Z * fz) / fl < 0.996 {
                        this.m_eyeLookPinned = false;
                    }
                }
            }
        }
        // Closest points of the two lines of sight, only when they clearly converge (at least ~1 degree): nearly
        // parallel eyes moved by the animation made the meeting point jump by many centimeters (test 31/10). Then
        // the last distance stays, eased towards the new one.
        let wx = left.X - right.X;
        let wy = left.Y - right.Y;
        let wz = left.Z - right.Z;
        let b = gl.X * gr.X + gl.Y * gr.Y + gl.Z * gr.Z;
        let d = gl.X * wx + gl.Y * wy + gl.Z * wz;
        let e = gr.X * wx + gr.Y * wy + gr.Z * wz;
        let denom = 1.0 - b * b;
        if denom > 0.0003 && !this.m_eyeLookPinned {
            let sl = (b * e - d) / denom;
            let sr = (e - b * d) / denom;
            if sl > 0.15 && sr > 0.15 && sl < 4.0 && sr < 4.0 {
                let px = (left.X + gl.X * sl + right.X + gr.X * sr) * 0.5 - mx;
                let py = (left.Y + gl.Y * sl + right.Y + gr.Y * sr) * 0.5 - my;
                let pz = (left.Z + gl.Z * sl + right.Z + gr.Z * sr) * 0.5 - mz;
                let meet = SqrtF(px * px + py * py + pz * pz);
                this.m_eyeLookDistance += (meet - this.m_eyeLookDistance) * 0.2;
            }
        }
        let dx = gl.X + gr.X;
        let dy = gl.Y + gr.Y;
        let dz = gl.Z + gr.Z;
        let length = SqrtF(dx * dx + dy * dy + dz * dz);
        if length < 0.0001 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let k = this.m_eyeLookDistance / length;
        return APM_Vector(mx + dx * k, my + dy * k, mz + dz * k, 1.0);
    }

    // Sends the camera to the plugin in V's model space (full poses turn towards it with "Look at camera").
    private func PublishCamera() {
        if !APM_IsPhotoModeActive() || !IsDefined(this.m_controller) {
            return;
        }
        let player = this.m_controller.GetPlayerControlledObject();
        if !IsDefined(player) {
            return;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        let cameraTransform: Transform;
        if !camera.GetActiveCameraWorldTransform(cameraTransform) {
            return;
        }
        // In the model space of every character (V and the added ones turn towards the camera each on its own).
        let c = 0;
        while c < 4 {
            let component = APM_CharacterGetComponent(c);
            if IsDefined(component) {
                let toWorld = component.GetLocalToWorld();
                APM_SetCameraModel(c, APM_WorldToModelPoint(toWorld, cameraTransform.position), APM_WorldToModelDirection(toWorld, camera.GetActiveCameraForward()), APM_WorldToModelDirection(toWorld, camera.GetActiveCameraUp()));
            }
            c += 1;
        }
    }

    private func UpdateMarkers() {
        let visible = this.m_gizmosOn && APM_IsPhotoModeActive();
        this.m_gizmoLayer.SetVisible(visible);
        if !visible {
            return;
        }

        let player = this.m_controller.GetPlayerControlledObject();
        if !IsDefined(player) {
            this.m_gizmoLayer.SetVisible(false);
            return;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        let component = APM_GizmoGetComponent();
        if !IsDefined(component) {
            this.m_gizmoLayer.SetVisible(false);
            return;
        }
        let toWorld = component.GetLocalToWorld();
        let selected = Cast<Int32>(APM_UiGetValue(7701u));
        let selectedGroup = APM_UiGetOptionGroup(7701u, selected);
        let s = APM_UiScale();

        let i = 0;
        while i < ArraySize(this.m_markers) {
            let marker = this.m_markers[i];
            let isDragged = IsDefined(this.m_dragMarker) && this.m_dragMarker.part == marker.part;
            let world: Vector4;
            if marker.kind == APM_MarkerKindEyeLook() && this.IsSingleEye(marker.part) {
                let eyeDistance = this.m_eyeLookDistance > 0.0 ? this.m_eyeLookDistance : this.LookDistance();
                if isDragged && this.m_dragMode == APM_DragEyeLook() {
                    let eye = APM_PartWorldPosition(toWorld, marker.part);
                    eyeDistance = Vector4.Distance(APM_Vector(eye.X, eye.Y, eye.Z, 0.0), APM_Vector(this.m_lookTarget.X, this.m_lookTarget.Y, this.m_lookTarget.Z, 0.0));
                }
                world = this.SingleEyePoint(toWorld, marker.part, eyeDistance);
                if this.m_eyesTogether {
                    world.W = 0.0; // hidden: both eyes move together
                }
            } else if marker.kind == APM_MarkerKindEyeLook() {
                world = isDragged && this.m_dragMode == APM_DragEyeLook() ? this.EyeDirectionPoint(toWorld, this.m_lookTarget) : this.EyeLookPoint(toWorld);
                if !this.m_eyesTogether {
                    world.W = 0.0; // hidden: each eye has its own marker
                }
            } else {
                let markerModel = APM_GizmoGetPartMarker(marker.part);
                world = markerModel.W > 0.5 ? Vector4.Transform(toWorld, APM_Vector(markerModel.X, markerModel.Y, markerModel.Z, 1.0)) : APM_Vector(0.0, 0.0, 0.0, 0.0);
                world.W = markerModel.W;
            }
            // In use: has a position, and (fingers) the camera is close enough. Its lines are drawn even when the
            // marker itself is off screen.
            let inUse = world.W > 0.5;
            let anywhere = inUse ? this.ProjectPoint(camera, world, false) : APM_Vector(0.0, 0.0, 0.0, 0.0);
            // Fingers only up close (or while their hand is being edited), their segments even closer.
            if inUse && marker.kind == APM_MarkerKindFinger() {
                inUse = anywhere.W > 0.5 && (anywhere.Z < this.FingerDistance() || Equals(marker.group, selectedGroup));
            }
            if inUse && marker.kind == APM_MarkerKindFingerDetail() {
                inUse = anywhere.W > 0.5 && (anywhere.Z < this.FingerDetailDistance() || (Equals(marker.group, selectedGroup) && anywhere.Z < this.FingerDistance()));
            }
            inUse = inUse || (isDragged && world.W > 0.5);
            let screen = inUse ? this.ProjectToScreen(camera, world) : APM_Vector(0.0, 0.0, 0.0, 0.0);
            let show = inUse && screen.W > 0.5;

            let isSelected = marker.part == selected;
            // The shape tells what a drag does with the keys held now: diamond = rotate, square = move,
            // flat bar across the bone = twist. Orange = IK.
            let mode = isDragged ? this.m_dragMode : this.DragModeFor(marker, this.m_shiftDown, this.m_controlDown);
            let locked = APM_GizmoIsPartLocked(marker.part);
            let color = APM_ColorText();
            if locked {
                color = APM_Color(0.45, 0.45, 0.45, 1.0); // turned by Photo Mode's "Look at camera": not editable
            } else if isSelected {
                color = APM_ColorAccent();
            } else if mode == APM_DragIkMove() || mode == APM_DragSwivel() || mode == APM_DragEyeLook() {
                color = APM_ColorIk();
            } else if marker.isProp {
                color = APM_ColorProp();
            } else if marker.part == this.m_hipsPart {
                color = APM_ColorRoot();
            }
            let boneAngle = 0.0;
            if inUse {
                boneAngle = this.UpdateMarkerLines(camera, toWorld, marker, world, color, isSelected || marker.isHovered);
            } else {
                marker.line.SetVisible(false);
                if IsDefined(marker.line2) {
                    marker.line2.SetVisible(false);
                }
                if IsDefined(marker.parentLine) {
                    marker.parentLine.SetVisible(false);
                }
            }

            marker.root.SetVisible(show);
            if show {
                marker.root.SetMargin(screen.X, screen.Y, 0.0, 0.0);
                marker.screenX = screen.X;
                marker.screenY = screen.Y;
                marker.depth = anywhere.Z;
                let isDetail = (marker.kind == APM_MarkerKindFingerDetail() || marker.kind == APM_MarkerKindFinger()) && !marker.isProp;
                let size = isDetail ? 7.0 : 10.0;
                if isSelected || marker.isHovered {
                    size += 4.0;
                }
                marker.dot.SetTintColor(color);
                if IsDefined(marker.halo) {
                    marker.halo.SetVisible(marker.chain >= 0 && APM_GizmoIsIkActive(marker.chain));
                }
                let opacity = locked ? 0.5 : (isSelected || marker.isHovered ? 1.0 : 0.75);
                if !isSelected && !marker.isHovered {
                    opacity *= this.DepthFade(marker); // farther back (last frame's overlap pass)
                }
                marker.root.SetOpacity(opacity);
                if mode == APM_DragTwist() {
                    marker.dot.SetSize(size * 1.8 * s, size * 0.45 * s);
                    marker.dot.SetRotation(boneAngle + 90.0);
                } else if mode == APM_DragIkMove() || mode == APM_DragEyeLook() || mode == APM_DragMove() {
                    marker.dot.SetSize(size * 0.85 * s, size * 0.85 * s);
                    marker.dot.SetRotation(0.0);
                } else {
                    marker.dot.SetSize(size * s, size * s);
                    marker.dot.SetRotation(45.0);
                }
            }
            i += 1;
        }
        this.ResolveMarkerOverlaps(camera, s);
        // Choosing "Eyes (both)" or a single eye in the Head tab switches the eye markers.
        if selected == this.m_leftEyePart || selected == this.m_rightEyePart {
            this.SetEyesTogether(false);
        } else if Equals(APM_UiGetOptionLabel(7701u, selected), "Eyes (both)") {
            this.SetEyesTogether(true);
        }
        this.UpdateRootMarker(camera, toWorld, selected);
        this.UpdateClassicGizmo(camera, toWorld, selected, s);
        this.UpdateHint();
    }

    // Markers that cover each other on screen (e.g. the fingers of two hands one above the other): the nearer one is
    // drawn on top, so it is the one the cursor picks. Fading only up close (nearest marker under 1 m, none from
    // 1.5 m, the eye look marker left out), where many markers crowd: markers within 2 cm on screen (at least 24 px)
    // of a nearer one turn much fainter, with their lines: full under 1 cm behind it, faintest from 4 cm. All stay
    // selectable. Besides, every marker fades with its depth behind the nearest one shown: full at the same depth,
    // faintest from 40 cm behind (the far side of the body).
    private func ResolveMarkerOverlaps(camera: ref<CameraSystem>, s: Float) {
        let shown: array<ref<APM_Marker>>;
        let nearest = -1.0;
        let i = 0;
        while i < ArraySize(this.m_markers) {
            let marker = this.m_markers[i];
            marker.behindFade = 0.0;
            marker.coverFade = 0.0;
            if marker.root.IsVisible() {
                ArrayPush(shown, marker);
                if marker.kind != APM_MarkerKindEyeLook() && (nearest < 0.0 || marker.depth < nearest) {
                    nearest = marker.depth;
                }
            }
            i += 1;
        }
        let closeness = nearest < 0.0 ? 0.0 : ClampF((1.5 - nearest) / 0.5, 0.0, 1.0);
        // Pixels per meter at the nearest marker: the cover distance follows the hand's size on screen.
        let fov = camera.GetActiveCameraFOV();
        if fov < 1.0 || fov > 170.0 {
            fov = 60.0;
        }
        let perMeter = APM_ScreenHeight() / (2.0 * MaxF(nearest, 0.05) * TanF(Deg2Rad(fov) * 0.5));
        let coverReach = MaxF(24.0 * s, 0.02 * perMeter);
        let reach = 0.5 * coverReach;
        i = 0;
        while i < ArraySize(shown) {
            if shown[i].kind != APM_MarkerKindEyeLook() {
                shown[i].behindFade = closeness * ClampF((shown[i].depth - nearest) / 0.4, 0.0, 1.0);
            }
            i += 1;
        }
        let front: array<ref<APM_Marker>>;
        i = 0;
        while i < ArraySize(shown) {
            let a = shown[i];
            let j = i + 1;
            while j < ArraySize(shown) {
                let b = shown[j];
                let nearer = a.depth <= b.depth ? a : b;
                let farther = a.depth <= b.depth ? b : a;
                let dx = a.screenX - b.screenX;
                let dy = a.screenY - b.screenY;
                let apart = dx * dx + dy * dy;
                if closeness > 0.0 && apart < coverReach * coverReach {
                    let fade = closeness * ClampF((farther.depth - nearer.depth - 0.01) / 0.03, 0.0, 1.0);
                    farther.coverFade = MaxF(farther.coverFade, fade);
                }
                if apart < reach * reach {
                    if !ArrayContains(front, nearer) {
                        ArrayPush(front, nearer);
                    }
                }
                j += 1;
            }
            i += 1;
        }
        if ArraySize(front) == 0 {
            ArrayClear(this.m_raisedParts);
            return;
        }
        // Raised farthest first, so among overlapping front markers the nearest ends on top; the hint stays above.
        let sorted = 0;
        while sorted < ArraySize(front) {
            let farthest = sorted;
            let k = sorted + 1;
            while k < ArraySize(front) {
                if front[k].depth > front[farthest].depth {
                    farthest = k;
                }
                k += 1;
            }
            let swap = front[sorted];
            front[sorted] = front[farthest];
            front[farthest] = swap;
            sorted += 1;
        }
        // Reordered only when that changes (reordering resets the hover under the cursor).
        let same = ArraySize(front) == ArraySize(this.m_raisedParts);
        i = 0;
        while same && i < ArraySize(front) {
            same = front[i].part == this.m_raisedParts[i];
            i += 1;
        }
        if same {
            return;
        }
        ArrayClear(this.m_raisedParts);
        let layer = this.m_gizmoLayer;
        i = 0;
        while i < ArraySize(front) {
            ArrayPush(this.m_raisedParts, front[i].part);
            layer.ReorderChild(front[i].root, layer.GetNumChildren() - 1);
            i += 1;
        }
        layer.ReorderChild(this.m_hint, layer.GetNumChildren() - 1);
    }

    // --- Classic gizmo ---------------------------------------------------------------------------------------------
    // Rotation: three rings around the selected part's joint (its own axes, the same as the panel's rows, or the
    // character's X / Y / Z) and an outer ring facing the camera. Move: three arrows (hips, held items, hands / feet
    // with IK). Each ring / arrow is drawn in the line layer and picked through small invisible squares along it
    // (a rotated widget's hit area would not follow its rotation).

    private func BuildClassicGizmo() {
        let s = APM_UiScale();
        let ring = 0;
        while ring < 4 {
            let k = 0;
            while k < APM_ClassicRingSegments() {
                this.AddHandle(ring, false, k, true, s);
                k += 1;
            }
            ring += 1;
        }
        let axis = 0;
        while axis < 3 {
            let k = 0;
            while k < APM_ClassicArrowPoints() {
                this.AddHandle(axis, true, k, k == 0, s);
                k += 1;
            }
            let head = new inkRectangle();
            head.SetAnchor(inkEAnchor.TopLeft);
            head.SetAnchorPoint(0.5, 0.5);
            head.SetSize(11.0 * s, 11.0 * s);
            head.SetRotation(45.0);
            head.SetTintColor(APM_ColorAxis(axis));
            head.SetInteractive(false);
            head.SetVisible(false);
            head.Reparent(this.m_lineLayer);
            ArrayPush(this.m_arrowHeads, head);
            axis += 1;
        }
    }

    private func AddHandle(axis: Int32, isArrow: Bool, segment: Int32, withLine: Bool, s: Float) {
        let root = new inkCanvas();
        root.SetName(StringToName("APM_handle:" + IntToString(ArraySize(this.m_handles))));
        root.SetAnchor(inkEAnchor.TopLeft);
        root.SetAnchorPoint(0.5, 0.5);
        root.SetSize(18.0 * s, 18.0 * s);
        root.SetInteractive(true);
        root.SetVisible(false);
        root.Reparent(this.m_gizmoLayer);
        let hit = new inkRectangle();
        hit.SetAnchor(inkEAnchor.Fill);
        hit.SetOpacity(0.0);
        hit.Reparent(root);
        root.RegisterToCallback(n"OnPress", this, n"OnHandlePress");
        root.RegisterToCallback(n"OnHoverOver", this, n"OnHandleHoverOver");
        root.RegisterToCallback(n"OnHoverOut", this, n"OnHandleHoverOut");
        let handle = new APM_GizmoHandle();
        handle.axis = axis;
        handle.isArrow = isArrow;
        handle.segment = segment;
        handle.root = root;
        if withLine {
            handle.line = this.CreateLine();
        }
        ArrayPush(this.m_handles, handle);
    }

    private func HideClassicGizmo() {
        let i = 0;
        while i < ArraySize(this.m_handles) {
            this.m_handles[i].root.SetVisible(false);
            this.m_handles[i].isHovered = false;
            if IsDefined(this.m_handles[i].line) {
                this.m_handles[i].line.SetVisible(false);
            }
            i += 1;
        }
        i = 0;
        while i < ArraySize(this.m_arrowHeads) {
            this.m_arrowHeads[i].SetVisible(false);
            i += 1;
        }
        this.m_hoverAxis = -1;
    }

    // World direction of a gizmo axis of a part (0..2), or of the view ring (3, the camera's forward); w = 1 when valid.
    private func ClassicAxis(camera: ref<CameraSystem>, toWorld: Matrix, part: Int32, axis: Int32) -> Vector4 {
        if axis == 3 {
            let forward = camera.GetActiveCameraForward();
            return APM_Vector(forward.X, forward.Y, forward.Z, 1.0);
        }
        let model = APM_Vector(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
        if !this.m_classicModelAxes {
            model = APM_GizmoGetPartAxis(part, axis);
            if model.W < 0.5 {
                return APM_Vector(0.0, 0.0, 0.0, 0.0);
            }
        }
        let world = Vector4.TransformDir(toWorld, APM_Vector(model.X, model.Y, model.Z, 0.0));
        let length = SqrtF(world.X * world.X + world.Y * world.Y + world.Z * world.Z);
        if length < 0.0001 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        return APM_Vector(world.X / length, world.Y / length, world.Z / length, 1.0);
    }

    // Can the classic gizmo show for this part now (a marker with a position, not turned by "Look at camera").
    private func ClassicPart(selected: Int32) -> ref<APM_Marker> {
        if this.m_classicMode == APM_ClassicOff() || APM_GizmoIsPartLocked(selected) {
            return null;
        }
        let i = 0;
        while i < ArraySize(this.m_markers) {
            let marker = this.m_markers[i];
            if marker.part == selected {
                if marker.kind == APM_MarkerKindEyeLook() {
                    return null;
                }
                if this.m_classicMode == APM_ClassicMove() && !marker.canMove && marker.chain < 0 {
                    return null; // only the hips, held items and hands / feet (IK) move
                }
                return marker;
            }
            i += 1;
        }
        return null;
    }

    private func UpdateClassicGizmo(camera: ref<CameraSystem>, toWorld: Matrix, selected: Int32, s: Float) {
        let marker = this.ClassicPart(selected);
        let centerModel = APM_GizmoGetPartMarker(selected);
        if !IsDefined(marker) || centerModel.W < 0.5 {
            this.HideClassicGizmo();
            return;
        }
        let center = Vector4.Transform(toWorld, APM_Vector(centerModel.X, centerModel.Y, centerModel.Z, 1.0));
        center.W = 1.0;
        let centerScreen = this.ProjectPoint(camera, center, false);
        if centerScreen.W < 0.5 {
            this.HideClassicGizmo();
            return;
        }
        let cameraTransform: Transform;
        camera.GetActiveCameraWorldTransform(cameraTransform);
        let toCameraX = cameraTransform.position.X - center.X;
        let toCameraY = cameraTransform.position.Y - center.Y;
        let toCameraZ = cameraTransform.position.Z - center.Z;
        // Same size on screen whatever the distance.
        let radius = 90.0 * s * this.MetersPerCanvasUnit(camera, centerScreen.Z);
        let isRotate = this.m_classicMode == APM_ClassicRotate();
        let dragAxis = IsDefined(this.m_dragHandle) ? this.m_dragHandle.axis : -1;
        let dragArrow = IsDefined(this.m_dragHandle) && this.m_dragHandle.isArrow;

        let axes: array<Vector4>;
        let a = 0;
        while a < 4 {
            ArrayPush(axes, this.ClassicAxis(camera, toWorld, selected, a));
            a += 1;
        }

        let i = 0;
        while i < ArraySize(this.m_handles) {
            let handle = this.m_handles[i];
            let axis = axes[handle.axis];
            let shown = axis.W > 0.5 && ((handle.isArrow && !isRotate) || (!handle.isArrow && isRotate));
            if !shown {
                handle.root.SetVisible(false);
                handle.isHovered = false;
                if IsDefined(handle.line) {
                    handle.line.SetVisible(false);
                }
            } else {
                let active = (handle.axis == dragAxis && Equals(handle.isArrow, dragArrow)) || (dragAxis < 0 && handle.axis == this.m_hoverAxis);
                let color = active ? APM_ColorAccent() : APM_ColorAxis(handle.axis);
                let thickness = (active ? 4.0 : 2.5) * s;
                if handle.isArrow {
                    // Points along the arrow, from the joint to its tip; the shaft is drawn by the first one.
                    let length = radius * 1.1;
                    let t = (Cast<Float>(handle.segment) + 1.0) / Cast<Float>(APM_ClassicArrowPoints());
                    let point = APM_Vector(center.X + axis.X * length * t, center.Y + axis.Y * length * t, center.Z + axis.Z * length * t, 1.0);
                    let screen = this.ProjectPoint(camera, point, false);
                    let step = APM_Vector(center.X + axis.X * 0.01, center.Y + axis.Y * 0.01, center.Z + axis.Z * 0.01, 1.0);
                    let stepScreen = this.ProjectPoint(camera, step, false);
                    handle.tangentX = (stepScreen.X - centerScreen.X) / 0.01;
                    handle.tangentY = (stepScreen.Y - centerScreen.Y) / 0.01;
                    this.PlaceHandle(handle, screen, true);
                    if IsDefined(handle.line) {
                        let tip = APM_Vector(center.X + axis.X * length, center.Y + axis.Y * length, center.Z + axis.Z * length, 1.0);
                        let tipScreen = this.ProjectPoint(camera, tip, false);
                        let visibleLine = tipScreen.W > 0.5;
                        handle.line.SetVisible(visibleLine);
                        if visibleLine {
                            this.PlaceLine(handle.line, centerScreen.X, centerScreen.Y, tipScreen.X, tipScreen.Y, thickness);
                            handle.line.SetTintColor(color);
                            handle.line.SetOpacity(0.95);
                            let head = this.m_arrowHeads[handle.axis];
                            head.SetVisible(true);
                            head.SetMargin(tipScreen.X, tipScreen.Y, 0.0, 0.0);
                            head.SetTintColor(color);
                        }
                    }
                } else {
                    // Ring of the axis: u, v span the plane around it (u x v = axis), segment k from angle k to k + 1.
                    let ringRadius = handle.axis == 3 ? radius * 1.2 : radius;
                    let ux = 0.0;
                    let uy = 0.0;
                    let uz = 0.0;
                    if handle.axis == 3 {
                        let right = camera.GetActiveCameraRight();
                        ux = right.X;
                        uy = right.Y;
                        uz = right.Z;
                    } else if AbsF(axis.Z) < 0.9 {
                        // axis x up
                        ux = axis.Y;
                        uy = -axis.X;
                        uz = 0.0;
                    } else {
                        // axis x forward
                        ux = 0.0;
                        uy = axis.Z;
                        uz = -axis.Y;
                    }
                    let uLength = SqrtF(ux * ux + uy * uy + uz * uz);
                    ux /= uLength;
                    uy /= uLength;
                    uz /= uLength;
                    let vx = axis.Y * uz - axis.Z * uy;
                    let vy = axis.Z * ux - axis.X * uz;
                    let vz = axis.X * uy - axis.Y * ux;
                    let step = 6.2831853 / Cast<Float>(APM_ClassicRingSegments());
                    let angle0 = Cast<Float>(handle.segment) * step;
                    let angle1 = angle0 + step;
                    let mid = angle0 + step * 0.5;
                    let p0 = APM_Vector(center.X + (ux * CosF(angle0) + vx * SinF(angle0)) * ringRadius, center.Y + (uy * CosF(angle0) + vy * SinF(angle0)) * ringRadius, center.Z + (uz * CosF(angle0) + vz * SinF(angle0)) * ringRadius, 1.0);
                    let p1 = APM_Vector(center.X + (ux * CosF(angle1) + vx * SinF(angle1)) * ringRadius, center.Y + (uy * CosF(angle1) + vy * SinF(angle1)) * ringRadius, center.Z + (uz * CosF(angle1) + vz * SinF(angle1)) * ringRadius, 1.0);
                    let offsetX = ux * CosF(mid) + vx * SinF(mid);
                    let offsetY = uy * CosF(mid) + vy * SinF(mid);
                    let offsetZ = uz * CosF(mid) + vz * SinF(mid);
                    let pm = APM_Vector(center.X + offsetX * ringRadius, center.Y + offsetY * ringRadius, center.Z + offsetZ * ringRadius, 1.0);
                    let s0 = this.ProjectPoint(camera, p0, false);
                    let s1 = this.ProjectPoint(camera, p1, false);
                    let sm = this.ProjectPoint(camera, pm, false);
                    // Turning by +1 radian around the axis moves the point along d/dangle (screen units per radian).
                    handle.tangentX = (s1.X - s0.X) / step;
                    handle.tangentY = (s1.Y - s0.Y) / step;
                    // The half of the ring facing away from the camera is drawn faint and cannot be picked.
                    let toCameraLength = MaxF(SqrtF(toCameraX * toCameraX + toCameraY * toCameraY + toCameraZ * toCameraZ), 0.001);
                    let facing = (offsetX * toCameraX + offsetY * toCameraY + offsetZ * toCameraZ) / toCameraLength;
                    let front = handle.axis == 3 || facing > -0.1;
                    this.PlaceHandle(handle, sm, front);
                    let visibleLine = s0.W > 0.5 && s1.W > 0.5;
                    handle.line.SetVisible(visibleLine);
                    if visibleLine {
                        this.PlaceLine(handle.line, s0.X, s0.Y, s1.X, s1.Y, thickness);
                        handle.line.SetTintColor(color);
                        handle.line.SetOpacity(front ? (handle.axis == 3 ? 0.6 : 0.95) : 0.2);
                    }
                }
            }
            i += 1;
        }
        if isRotate {
            i = 0;
            while i < ArraySize(this.m_arrowHeads) {
                this.m_arrowHeads[i].SetVisible(false);
                i += 1;
            }
        }
    }

    private func PlaceHandle(handle: ref<APM_GizmoHandle>, screen: Vector4, pickable: Bool) {
        let show = pickable && screen.W > 0.5 && this.IsOnCanvas(screen);
        handle.root.SetVisible(show);
        if !show {
            handle.isHovered = false;
            return;
        }
        handle.root.SetMargin(screen.X, screen.Y, 0.0, 0.0);
        handle.screenX = screen.X;
        handle.screenY = screen.Y;
    }

    // By name ("APM_handle:<index>"), as the markers are found.
    private func FindHandleByWidget(widget: wref<inkWidget>) -> ref<APM_GizmoHandle> {
        if !IsDefined(widget) {
            return null;
        }
        let name = NameToString(widget.GetName());
        if !StrBeginsWith(name, "APM_handle:") {
            return null;
        }
        let index = StringToInt(StrAfterFirst(name, ":"));
        return index >= 0 && index < ArraySize(this.m_handles) ? this.m_handles[index] : null;
    }

    // Cursor movement while a ring / arrow is dragged: the part turns around / moves along its axis by as much as
    // the cursor went along the ring / arrow on screen.
    private func DragHandle(dx: Float, dy: Float) {
        let handle = this.m_dragHandle;
        let part = Cast<Int32>(APM_UiGetValue(7701u));
        let component = APM_GizmoGetComponent();
        let lengthSquared = handle.tangentX * handle.tangentX + handle.tangentY * handle.tangentY;
        if !IsDefined(component) || lengthSquared < 0.0001 {
            return;
        }
        let toWorld = component.GetLocalToWorld();
        let amount = (dx * handle.tangentX + dy * handle.tangentY) / lengthSquared;
        let axis = this.m_handleAxis;
        if handle.isArrow {
            let modelDelta = APM_WorldToModelDirection(toWorld, APM_Vector(axis.X * amount, axis.Y * amount, axis.Z * amount, 0.0));
            let chain = APM_GizmoGetPartIkChain(part);
            if APM_GizmoCanMovePart(part) {
                APM_GizmoMovePart(part, modelDelta);
            } else if chain >= 0 {
                APM_GizmoMoveIk(chain, modelDelta);
            }
        } else {
            APM_GizmoRotatePart(part, APM_WorldToModelDirection(toWorld, APM_Vector(axis.X, axis.Y, axis.Z, 0.0)), Rad2Deg(amount));
        }
    }

    protected cb func OnHandleHoverOver(e: ref<inkPointerEvent>) -> Bool {
        let handle = this.FindHandleByWidget(e.GetCurrentTarget());
        if !IsDefined(handle) || !this.CanTakeHover() {
            return false;
        }
        handle.isHovered = true;
        this.m_hoverAxis = handle.axis;
        this.SetHover(true);
    }

    protected cb func OnHandleHoverOut(e: ref<inkPointerEvent>) -> Bool {
        let handle = this.FindHandleByWidget(e.GetCurrentTarget());
        if IsDefined(handle) {
            handle.isHovered = false;
            if this.m_hoverAxis == handle.axis {
                this.m_hoverAxis = -1;
            }
        }
        if !IsDefined(this.m_dragMarker) && !IsDefined(this.m_dragHandle) && !this.m_panelHovered {
            this.SetHover(false);
        }
    }

    protected cb func OnHandlePress(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") {
            return false;
        }
        this.m_pressedButtonId = "";
        let handle = this.FindHandleByWidget(e.GetCurrentTarget());
        if !IsDefined(handle) || !handle.isHovered {
            return false; // hover not taken (camera moving, UI hidden): the click belongs to the camera
        }
        let player = this.m_controller.GetPlayerControlledObject();
        let component = APM_GizmoGetComponent();
        if !IsDefined(player) || !IsDefined(component) {
            return false;
        }
        let part = Cast<Int32>(APM_UiGetValue(7701u));
        this.m_handleAxis = this.ClassicAxis(GameInstance.GetCameraSystem(player.GetGame()), component.GetLocalToWorld(), part, handle.axis);
        if this.m_handleAxis.W < 0.5 {
            return false;
        }
        // Calibrate pointer -> canvas scale with the square under the cursor (as for the markers).
        let pointer = e.GetScreenSpacePosition();
        this.m_lastPointer = pointer;
        this.m_useRealCursor = false;
        if pointer.X > 10.0 && pointer.Y > 10.0 && handle.screenX > 10.0 && handle.screenY > 10.0 {
            this.m_cursorScaleX = handle.screenX / pointer.X;
            this.m_cursorScaleY = handle.screenY / pointer.Y;
            this.m_useRealCursor = this.m_cursorScaleX > 0.3 && this.m_cursorScaleX < 6.0 && this.m_cursorScaleY > 0.3 && this.m_cursorScaleY < 6.0;
        }
        APM_HistoryCheckpoint();
        this.m_dragHandle = handle;
        this.SetHover(true);
        return true;
    }

    // Label next to the hovered (or dragged) marker: what a drag does now, and what the keys change.
    private func UpdateHint() {
        let marker = this.m_dragMarker;
        if !IsDefined(marker) {
            let i = 0;
            while i < ArraySize(this.m_markers) {
                if this.m_markers[i].isHovered && this.m_markers[i].root.IsVisible() {
                    marker = this.m_markers[i];
                }
                i += 1;
            }
        }
        if !IsDefined(marker) {
            this.m_hint.SetVisible(false);
            return;
        }
        let s = APM_UiScale();
        let mode = IsDefined(this.m_dragMarker) ? this.m_dragMode : this.DragModeFor(marker, this.m_shiftDown, this.m_controlDown);
        let text = APM_PartShortText(APM_UiGetOptionLabel(7701u, marker.part)) + ": " + this.ModeName(mode);
        let isCenter = this.m_classicMode == APM_ClassicMove() && (marker.kind == APM_MarkerKindEyeLook() || (marker.part == Cast<Int32>(APM_UiGetValue(7701u)) && (marker.canMove || marker.chain >= 0)));
        if isCenter {
            text = APM_PartShortText(APM_UiGetOptionLabel(7701u, marker.part)) + ": " + APM_Text("APM-Hint-ClassicCenter", "drag to move freely (or the arrows)");
        } else if this.m_classicMode != APM_ClassicOff() {
            // Classic gizmo on: the markers only select (the rings / arrows turn and move).
            text = APM_PartShortText(APM_UiGetOptionLabel(7701u, marker.part)) + ": " + APM_Text("APM-Hint-ClassicSelect", "click to select (drag the rings / arrows)");
        } else if !IsDefined(this.m_dragMarker) && !this.m_shiftDown && !this.m_controlDown && !APM_IsAltDown() && marker.kind != APM_MarkerKindEyeLook() {
            if marker.chain >= 0 {
                if this.m_autoIk {
                    text += "   " + APM_Text("APM-Hint-ShiftRotate", "(Shift: rotate, Alt: free, Ctrl: twist)");
                } else if APM_GizmoIsIkActive(marker.chain) {
                    text += "   " + APM_Text("APM-Hint-ShiftMoveIk", "(Shift: move with IK, Alt: free, Ctrl: twist)");
                } else {
                    text += "   " + APM_Text("APM-Hint-AltFree", "(Alt: free, Ctrl: twist)");
                }
            } else if marker.part == this.m_hipsPart {
                text += "   " + APM_Text("APM-Hint-ShiftRotate", "(Shift: rotate, Alt: free, Ctrl: twist)");
            } else if marker.canMove {
                text += "   " + APM_Text("APM-Hint-ShiftMove", "(Shift: move, Alt: free, Ctrl: twist)");
            } else if !this.IsTrackballPart(marker.part) {
                text += "   " + APM_Text("APM-Hint-AltFree", "(Alt: free, Ctrl: twist)");
            } else {
                text += "   " + APM_Text("APM-Hint-CtrlTwist", "(Ctrl: twist)");
            }
        }
        if !IsDefined(this.m_dragMarker) && APM_UiIsPartEdited(marker.part) && !APM_GizmoIsPartLocked(marker.part) {
            text += "   " + APM_Text("APM-Hint-RightClickReset", "Right-click: reset");
        }
        this.m_hint.SetText(text);
        this.m_hint.SetMargin(marker.screenX + 16.0 * s, marker.screenY - 30.0 * s, 0.0, 0.0);
        this.m_hint.SetVisible(true);
    }

    private func ModeName(mode: Int32) -> String {
        if mode == APM_DragIkMove() {
            return APM_Text("APM-Mode-MoveIk", "move (IK)");
        }
        if mode == APM_DragMove() {
            return APM_Text("APM-Mode-Move", "move");
        }
        if mode == APM_DragSwivel() {
            return APM_Text("APM-Mode-Swivel", "turn elbow / knee (IK)");
        }
        if mode == APM_DragTwist() {
            return APM_Text("APM-Mode-Twist", "twist");
        }
        if mode == APM_DragEyeLook() {
            return APM_Text("APM-Mode-Look", "look");
        }
        if mode == APM_DragTrackball() {
            return APM_Text("APM-Mode-FreeRotate", "free rotate");
        }
        return APM_Text("APM-Mode-Rotate", "rotate");
    }

    // Bone lines: from the joint towards the point the bone points at, and from the parent part's joint; for the
    // eyes, from each eye to the point they look at. Returns the on-screen angle of the bone (degrees).
    // Lines of a marker, from world points (they stay visible when one end is off screen). Returns the bone's
    // angle on screen, for the twist bar.
    // Opacity factor of a marker and its lines from how far behind the nearest markers it is (see ResolveMarkerOverlaps).
    private func DepthFade(marker: ref<APM_Marker>) -> Float {
        return (1.0 - 0.7 * marker.behindFade) * (1.0 - 0.85 * marker.coverFade);
    }

    private func UpdateMarkerLines(camera: ref<CameraSystem>, toWorld: Matrix, marker: ref<APM_Marker>, world: Vector4, color: HDRColor, strong: Bool) -> Float {
        let s = APM_UiScale();
        let thickness = (strong ? 2.5 : 1.5) * s;
        let fade = strong ? 1.0 : this.DepthFade(marker);
        let opacity = strong ? 0.9 : 0.45 * fade;
        if marker.kind == APM_MarkerKindEyeLook() && this.IsSingleEye(marker.part) {
            this.DrawWorldLine(camera, marker.line, APM_PartWorldPosition(toWorld, marker.part), world, color, thickness, opacity);
            return 0.0;
        }
        if marker.kind == APM_MarkerKindEyeLook() {
            this.DrawWorldLine(camera, marker.line, APM_PartWorldPosition(toWorld, this.m_leftEyePart), world, color, thickness, opacity);
            this.DrawWorldLine(camera, marker.line2, APM_PartWorldPosition(toWorld, this.m_rightEyePart), world, color, thickness, opacity);
            return 0.0;
        }
        // Held items: just the marker (their bones do not point anywhere meaningful).
        if marker.isProp {
            marker.line.SetVisible(false);
            if IsDefined(marker.parentLine) {
                marker.parentLine.SetVisible(false);
            }
            return 0.0;
        }
        let parentModel = APM_GizmoGetPartParent(marker.part);
        let parent = parentModel.W > 0.5 ? Vector4.Transform(toWorld, APM_Vector(parentModel.X, parentModel.Y, parentModel.Z, 1.0)) : APM_Vector(0.0, 0.0, 0.0, 0.0);
        this.DrawWorldLine(camera, marker.parentLine, APM_Vector(parent.X, parent.Y, parent.Z, parentModel.W), world, APM_ColorText(), 1.5 * s, 0.35 * fade);

        let tipModel = APM_GizmoGetPartTip(marker.part);
        if tipModel.W < 0.5 {
            marker.line.SetVisible(false);
            return 0.0;
        }
        let tip = Vector4.Transform(toWorld, APM_Vector(tipModel.X, tipModel.Y, tipModel.Z, 1.0));
        // Fingers and palm: a short line that only shows where the bone points (no longer than 4 cm).
        if marker.kind == APM_MarkerKindFinger() || marker.kind == APM_MarkerKindFingerDetail() {
            let tx = tip.X - world.X;
            let ty = tip.Y - world.Y;
            let tz = tip.Z - world.Z;
            let length = SqrtF(tx * tx + ty * ty + tz * tz);
            if length > 0.04 {
                let k = 0.04 / length;
                tip = APM_Vector(world.X + tx * k, world.Y + ty * k, world.Z + tz * k, 1.0);
            }
        }
        return this.DrawWorldLine(camera, marker.line, world, APM_Vector(tip.X, tip.Y, tip.Z, 1.0), color, thickness, opacity);
    }

    // Line between two world points (W = 1 when valid), cut where it goes behind the camera. Returns its screen angle.
    // maxOffscreen > 0: when one end is off screen only a short piece pointing at it is drawn (none if both are).
    private func DrawWorldLine(camera: ref<CameraSystem>, line: wref<inkRectangle>, from: Vector4, to: Vector4, color: HDRColor, thickness: Float, opacity: Float, opt maxOffscreen: Float) -> Float {
        if !IsDefined(line) {
            return 0.0;
        }
        let cameraTransform: Transform;
        if from.W < 0.5 || to.W < 0.5 || !camera.GetActiveCameraWorldTransform(cameraTransform) {
            line.SetVisible(false);
            return 0.0;
        }
        let forward = camera.GetActiveCameraForward();
        let near = 0.2;
        let depthA = (from.X - cameraTransform.position.X) * forward.X + (from.Y - cameraTransform.position.Y) * forward.Y + (from.Z - cameraTransform.position.Z) * forward.Z;
        let depthB = (to.X - cameraTransform.position.X) * forward.X + (to.Y - cameraTransform.position.Y) * forward.Y + (to.Z - cameraTransform.position.Z) * forward.Z;
        if depthA < near && depthB < near {
            line.SetVisible(false);
            return 0.0;
        }
        let a = from;
        let b = to;
        if depthA < near {
            let t = (near - depthA) / (depthB - depthA);
            a = APM_Vector(from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t, from.Z + (to.Z - from.Z) * t, 1.0);
        } else if depthB < near {
            let t = (near - depthB) / (depthA - depthB);
            b = APM_Vector(to.X + (from.X - to.X) * t, to.Y + (from.Y - to.Y) * t, to.Z + (from.Z - to.Z) * t, 1.0);
        }
        let screenA = this.ProjectPoint(camera, a, false);
        let screenB = this.ProjectPoint(camera, b, false);
        if screenA.W < 0.5 || screenB.W < 0.5 {
            line.SetVisible(false);
            return 0.0;
        }
        let angle = Rad2Deg(AtanF(screenB.Y - screenA.Y, screenB.X - screenA.X));
        let alpha = opacity;
        if maxOffscreen > 0.0 {
            let onA = depthA >= near && this.IsOnCanvas(screenA);
            let onB = depthB >= near && this.IsOnCanvas(screenB);
            if !onA && !onB {
                line.SetVisible(false);
                return angle;
            }
            let dx = screenB.X - screenA.X;
            let dy = screenB.Y - screenA.Y;
            let length = SqrtF(dx * dx + dy * dy);
            if (!onA || !onB) && length > maxOffscreen {
                let k = maxOffscreen / length;
                if onA {
                    screenB = APM_Vector(screenA.X + dx * k, screenA.Y + dy * k, 0.0, 1.0);
                } else {
                    screenA = APM_Vector(screenB.X - dx * k, screenB.Y - dy * k, 0.0, 1.0);
                }
                alpha = opacity * 0.6;
            }
        }
        this.PlaceLine(line, screenA.X, screenA.Y, screenB.X, screenB.Y, thickness);
        line.SetTintColor(color);
        line.SetOpacity(alpha);
        line.SetVisible(true);
        return angle;
    }

    private func IsOnCanvas(point: Vector4) -> Bool {
        return point.X >= 0.0 && point.Y >= 0.0 && point.X <= APM_ScreenWidth() && point.Y <= APM_ScreenHeight();
    }

    // While the hips are selected, hovered or dragged: V's root point and a line from it to the hips.
    private func UpdateRootMarker(camera: ref<CameraSystem>, toWorld: Matrix, selected: Int32) {
        let hips: ref<APM_Marker>;
        let i = 0;
        while i < ArraySize(this.m_markers) {
            if this.m_markers[i].part == this.m_hipsPart {
                hips = this.m_markers[i];
            }
            i += 1;
        }
        let dragged = IsDefined(this.m_dragMarker) && this.m_dragMarker.part == this.m_hipsPart;
        let show = IsDefined(hips) && (selected == this.m_hipsPart || hips.isHovered || dragged);
        let hipsModel = APM_GizmoGetPartMarker(this.m_hipsPart);
        if !show || hipsModel.W < 0.5 {
            this.m_rootDot.SetVisible(false);
            this.m_rootLabel.SetVisible(false);
            this.m_rootHit.SetVisible(false);
            this.m_rootHovered = false;
            this.m_rootLine.SetVisible(false);
            return;
        }
        let hipsWorld = Vector4.Transform(toWorld, APM_Vector(hipsModel.X, hipsModel.Y, hipsModel.Z, 1.0));
        let rootWorld = Vector4.Transform(toWorld, APM_Vector(0.0, 0.0, 0.0, 1.0));
        let rootScreen = this.ProjectToScreen(camera, APM_Vector(rootWorld.X, rootWorld.Y, rootWorld.Z, 1.0));
        // The point only when on screen; the line always.
        this.m_rootDot.SetVisible(rootScreen.W > 0.5);
        this.m_rootHit.SetVisible(rootScreen.W > 0.5);
        this.m_rootLabel.SetVisible(rootScreen.W > 0.5 && this.m_rootHovered);
        if rootScreen.W > 0.5 {
            this.m_rootDot.SetMargin(rootScreen.X, rootScreen.Y, 0.0, 0.0);
            this.m_rootHit.SetMargin(rootScreen.X, rootScreen.Y, 0.0, 0.0);
            this.m_rootLabel.SetMargin(rootScreen.X + 16.0 * APM_UiScale(), rootScreen.Y - 12.0 * APM_UiScale(), 0.0, 0.0);
        }
        this.DrawWorldLine(camera, this.m_rootLine, APM_Vector(rootWorld.X, rootWorld.Y, rootWorld.Z, 1.0), APM_Vector(hipsWorld.X, hipsWorld.Y, hipsWorld.Z, 1.0), APM_ColorDimText(), 1.5 * APM_UiScale(), 0.6);
    }

    // Perspective projection of a world point to the full-screen ink canvas (X, Y); W = 1 when on screen.
    // Done by hand from the camera axes and field of view (CameraSystem.ProjectPoint did not give screen
    // coordinates). Assumes a vertical field of view.
    private func ProjectToScreen(camera: ref<CameraSystem>, world: Vector4) -> Vector4 {
        return this.ProjectPoint(camera, world, true);
    }

    // onScreenOnly = false: also points outside the screen (still in front of the camera), for lines.
    private func ProjectPoint(camera: ref<CameraSystem>, world: Vector4, onScreenOnly: Bool) -> Vector4 {
        let cameraTransform: Transform;
        if !camera.GetActiveCameraWorldTransform(cameraTransform) {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let right = camera.GetActiveCameraRight();
        let up = camera.GetActiveCameraUp();
        let forward = camera.GetActiveCameraForward();
        let dx = world.X - cameraTransform.position.X;
        let dy = world.Y - cameraTransform.position.Y;
        let dz = world.Z - cameraTransform.position.Z;
        let depth = dx * forward.X + dy * forward.Y + dz * forward.Z;
        if depth < 0.05 {
            return APM_Vector(0.0, 0.0, 0.0, 0.0); // behind the camera
        }
        let side = dx * right.X + dy * right.Y + dz * right.Z;
        let height = dx * up.X + dy * up.Y + dz * up.Z;

        let fov = camera.GetActiveCameraFOV();
        if fov < 1.0 || fov > 170.0 {
            fov = 60.0;
        }
        let aspect = camera.GetAspectRatio();
        if aspect < 0.5 || aspect > 5.0 {
            aspect = APM_ScreenWidth() / APM_ScreenHeight();
        }
        let tanHalf = TanF(Deg2Rad(fov) * 0.5);
        let ndcX = side / (depth * tanHalf * aspect);
        let ndcY = height / (depth * tanHalf);
        if onScreenOnly && (AbsF(ndcX) > 1.05 || AbsF(ndcY) > 1.05) {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        return APM_Vector((ndcX + 1.0) * 0.5 * APM_ScreenWidth(), (1.0 - ndcY) * 0.5 * APM_ScreenHeight(), depth, 1.0);
    }

    private func PartIndex(label: String) -> Int32 {
        let count = APM_UiGetOptionCount(7701u);
        let i = 0;
        while i < count {
            if Equals(APM_UiGetOptionLabel(7701u, i), label) {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    // Canvas units per raw mouse unit, used only when pointer events carry no screen position.
    private func FallbackCanvasPerMouseUnit() -> Float {
        return 0.6;
    }

    // Degrees of rotation per canvas unit dragged (the ink canvas is at least 3840 units wide).
    private func DegreesPerCanvasUnit() -> Float {
        return 0.12;
    }

    // Point at the given depth in front of the camera, under a point of the ink canvas. W = 1 when valid.
    private func UnprojectFromScreen(camera: ref<CameraSystem>, screenX: Float, screenY: Float, depth: Float) -> Vector4 {
        let cameraTransform: Transform;
        if !camera.GetActiveCameraWorldTransform(cameraTransform) {
            return APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        let right = camera.GetActiveCameraRight();
        let up = camera.GetActiveCameraUp();
        let forward = camera.GetActiveCameraForward();
        let fov = camera.GetActiveCameraFOV();
        if fov < 1.0 || fov > 170.0 {
            fov = 60.0;
        }
        let aspect = camera.GetAspectRatio();
        if aspect < 0.5 || aspect > 5.0 {
            aspect = APM_ScreenWidth() / APM_ScreenHeight();
        }
        let tanHalf = TanF(Deg2Rad(fov) * 0.5);
        let ndcX = screenX / APM_ScreenWidth() * 2.0 - 1.0;
        let ndcY = 1.0 - screenY / APM_ScreenHeight() * 2.0;
        let sx = ndcX * tanHalf * aspect * depth;
        let sy = ndcY * tanHalf * depth;
        return APM_Vector(
            cameraTransform.position.X + forward.X * depth + right.X * sx + up.X * sy,
            cameraTransform.position.Y + forward.Y * depth + right.Y * sx + up.Y * sy,
            cameraTransform.position.Z + forward.Z * depth + right.Z * sx + up.Z * sy,
            1.0);
    }

    // What dragging a marker does, from the marker and the keys held when the drag starts.
    // Gizmo mode: APM_ClassicOff (Direct) / Rotate / Move (tools row, Ctrl+1 / 3 / 2); shows the gizmos.
    private func SetClassicMode(mode: Int32) {
        this.m_classicMode = mode;
        APM_PrefSet("ClassicGizmo", Cast<Float>(this.m_classicMode));
        if !this.m_gizmosOn {
            this.m_gizmosOn = true;
            APM_PrefSet("Gizmos", 1.0);
        }
    }

    // Axes of the arrows / rings: the part's own or the model's (tools row, Ctrl+4).
    private func ToggleClassicAxes() {
        this.m_classicModelAxes = !this.m_classicModelAxes;
        APM_PrefSet("ClassicGizmoModelAxes", this.m_classicModelAxes ? 1.0 : 0.0);
    }

    private func DragModeFor(marker: ref<APM_Marker>, shift: Bool, control: Bool) -> Int32 {
        if this.m_centerDrag && marker == this.m_dragMarker {
            if marker.kind == APM_MarkerKindEyeLook() {
                return APM_DragEyeLook();
            }
            return marker.canMove ? APM_DragMove() : APM_DragIkMove();
        }
        if marker.kind == APM_MarkerKindEyeLook() {
            return APM_DragEyeLook();
        }
        // Alt: free rotation of any part, like a ball under the cursor (seen from the camera).
        if APM_IsAltDown() {
            return APM_DragTrackball();
        }
        if control {
            return APM_DragTwist();
        }
        // Hips (V's root): move by default, Shift rotates. Held items: Shift moves.
        let isHips = marker.part == this.m_hipsPart;
        if isHips {
            // The hips have no bone to point along (their children surround them): they turn as seen from the
            // camera, like the head.
            return shift ? APM_DragTrackball() : APM_DragMove();
        }
        if shift && marker.canMove && !isHips {
            return APM_DragMove();
        }
        if marker.chain >= 0 {
            // Auto IK on: drag moves the hand / foot (IK), Shift rotates it. Off: drag rotates it; Shift moves it with IK
            // only when that limb is already pinned (the magnet off never pins a limb).
            if this.m_autoIk {
                return shift ? APM_DragRotate() : APM_DragIkMove();
            }
            return shift && APM_GizmoIsIkActive(marker.chain) ? APM_DragIkMove() : APM_DragRotate();
        }
        if marker.swivelChain >= 0 && !shift && APM_GizmoIsIkActive(marker.swivelChain) {
            return APM_DragSwivel();
        }
        if this.IsTrackballPart(marker.part) {
            return APM_DragTrackball();
        }
        return APM_DragRotate();
    }

    // Drags that the mouse wheel pushes away from / pulls towards the camera.
    private func UsesWheelDepth(mode: Int32) -> Bool {
        return mode == APM_DragIkMove() || mode == APM_DragEyeLook() || mode == APM_DragMove();
    }

    // Mouse wheel during a move: depth (the wheel is kept from the Photo Mode camera meanwhile).
    private func UpdateDragWheel() {
        if !IsDefined(this.m_dragMarker) || !this.UsesWheelDepth(this.m_dragMode) {
            return;
        }
        let wheel = APM_ConsumeWheel();
        if wheel == 0 {
            return;
        }
        let notches = Cast<Float>(wheel) / 120.0;
        let player = this.m_controller.GetPlayerControlledObject();
        let component = APM_GizmoGetComponent();
        if !IsDefined(player) || !IsDefined(component) {
            return;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        if this.m_dragMode == APM_DragMove() {
            let joint = APM_PartWorldPosition(component.GetLocalToWorld(), this.m_dragMarker.part);
            let jointScreen = joint.W > 0.5 ? this.ProjectToScreen(camera, joint) : APM_Vector(0.0, 0.0, 2.0, 0.0);
            let amount = MaxF(jointScreen.Z, 0.3) * 0.015 * notches;
            let forward = camera.GetActiveCameraForward();
            APM_GizmoMovePart(this.m_dragMarker.part, APM_WorldToModelDirection(component.GetLocalToWorld(), APM_Vector(forward.X * amount, forward.Y * amount, forward.Z * amount, 0.0)));
            return;
        }
        // IK / eyes: the point follows the cursor at a new distance.
        this.m_dragDepth = ClampF(this.m_dragDepth * (1.0 + 0.015 * notches), 0.2, 50.0);
        this.DragMarker(this.m_dragMarker, 0.0, 0.0);
    }

    private func BeginMarkerDrag(marker: ref<APM_Marker>, shift: Bool, control: Bool) {
        this.m_dragMarker = marker;
        this.m_dragMode = this.DragModeFor(marker, shift, control);
        APM_SetWheelCapture(this.UsesWheelDepth(this.m_dragMode));
        let player = this.m_controller.GetPlayerControlledObject();
        let component = APM_GizmoGetComponent();
        if !IsDefined(player) || !IsDefined(component) {
            this.m_dragMarker = null;
            return;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        let toWorld = component.GetLocalToWorld();

        if this.m_dragMode == APM_DragSwivel() {
            APM_UiSetValue(7710u, Cast<Float>(marker.swivelChain));
            this.m_dragSwivel = APM_UiGetValue(7715u);
            return;
        }
        if this.m_dragMode == APM_DragEyeLook() {
            let look = this.IsSingleEye(marker.part) ? this.SingleEyePoint(toWorld, marker.part, this.m_eyeLookDistance > 0.0 ? this.m_eyeLookDistance : this.LookDistance()) : this.EyeLookPoint(toWorld);
            let lookScreen = this.ProjectToScreen(camera, look);
            if look.W < 0.5 || lookScreen.W < 0.5 {
                this.m_dragMarker = null;
                return;
            }
            this.m_lookTarget = look;
            this.m_dragCursorX = lookScreen.X;
            this.m_dragCursorY = lookScreen.Y;
            this.m_dragDepth = lookScreen.Z;
            return;
        }
        if this.m_dragMode != APM_DragIkMove() {
            return;
        }
        let world = APM_PartWorldPosition(toWorld, marker.part);
        let screen = this.ProjectToScreen(camera, world);
        if world.W < 0.5 || screen.W < 0.5 {
            this.m_dragMarker = null;
            return;
        }
        // Select and pin the limb (keeping a body pin), remember where the drag started.
        APM_UiSetValue(7710u, Cast<Float>(marker.chain));
        if APM_UiGetValue(7711u) < 0.5 {
            APM_UiSetValue(7711u, 1.0);
        }
        this.m_dragCursorX = screen.X;
        this.m_dragCursorY = screen.Y;
        this.m_dragDepth = screen.Z;
        this.m_dragStartWorld = world;
        this.m_dragStartOffset = APM_Vector(APM_UiGetValue(7712u), APM_UiGetValue(7713u), APM_UiGetValue(7714u), 0.0);
    }

    // dx / dy: cursor movement on the ink canvas (y grows downwards).
    private func DragMarker(marker: ref<APM_Marker>, dx: Float, dy: Float) {
        let player = this.m_controller.GetPlayerControlledObject();
        let component = APM_GizmoGetComponent();
        if !IsDefined(player) || !IsDefined(component) {
            return;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        let toWorld = component.GetLocalToWorld();

        if this.m_dragMode == APM_DragIkMove() {
            // The hand / foot follows the cursor on a plane facing the camera, at its starting depth.
            this.m_dragCursorX += dx;
            this.m_dragCursorY += dy;
            let target = this.UnprojectFromScreen(camera, this.m_dragCursorX, this.m_dragCursorY, this.m_dragDepth);
            if target.W < 0.5 {
                return;
            }
            let worldDelta = APM_Vector(target.X - this.m_dragStartWorld.X, target.Y - this.m_dragStartWorld.Y, target.Z - this.m_dragStartWorld.Z, 0.0);
            let modelDelta = APM_GizmoIkModelToOffset(marker.chain, APM_WorldToModelDirection(toWorld, worldDelta));
            APM_UiSetValue(7712u, ClampF(this.m_dragStartOffset.X + modelDelta.X * 100.0, -1000.0, 1000.0));
            APM_UiSetValue(7713u, ClampF(this.m_dragStartOffset.Y + modelDelta.Y * 100.0, -1000.0, 1000.0));
            APM_UiSetValue(7714u, ClampF(this.m_dragStartOffset.Z + modelDelta.Z * 100.0, -1000.0, 1000.0));
        } else if this.m_dragMode == APM_DragSwivel() {
            this.TurnElbowTowardsCursor(camera, toWorld, marker.swivelChain, dx, dy);
        } else if this.m_dragMode == APM_DragMove() {
            let joint = APM_PartWorldPosition(toWorld, marker.part);
            let jointScreen = joint.W > 0.5 ? this.ProjectToScreen(camera, joint) : APM_Vector(0.0, 0.0, 0.0, 0.0);
            let move = this.CursorToWorld(camera, jointScreen.W > 0.5 ? jointScreen.Z : 2.0, dx, dy);
            APM_GizmoMovePart(marker.part, APM_WorldToModelDirection(toWorld, move));
        } else if this.m_dragMode == APM_DragTwist() {
            this.TwistPart(camera, toWorld, marker.part, dx, dy);
        } else if this.m_dragMode == APM_DragEyeLook() {
            this.MoveEyeLook(camera, toWorld, dx, dy);
        } else if this.m_dragMode == APM_DragTrackball() {
            // Head: rotate as seen from the camera - left/right turns around the camera's vertical axis,
            // up/down tilts around its horizontal axis (the side facing the camera follows the cursor).
            if AbsF(dx) > 0.0 {
                APM_GizmoRotatePart(marker.part, APM_WorldToModelDirection(toWorld, camera.GetActiveCameraUp()), dx * this.DegreesPerCanvasUnit());
            }
            if AbsF(dy) > 0.0 {
                APM_GizmoRotatePart(marker.part, APM_WorldToModelDirection(toWorld, camera.GetActiveCameraRight()), dy * this.DegreesPerCanvasUnit());
            }
        } else {
            this.RotateTipTowardsCursor(camera, toWorld, marker.part, dx, dy, this.MinimumLever(marker));
        }
    }

    // Cursor movement on the canvas -> world movement on the plane facing the camera at a given depth.
    private func CursorToWorld(camera: ref<CameraSystem>, depth: Float, dx: Float, dy: Float) -> Vector4 {
        let meters = this.MetersPerCanvasUnit(camera, depth);
        let right = camera.GetActiveCameraRight();
        let up = camera.GetActiveCameraUp();
        return APM_Vector((right.X * dx - up.X * dy) * meters, (right.Y * dx - up.Y * dy) * meters, (right.Z * dx - up.Z * dy) * meters, 0.0);
    }

    // Pinned limb: turns the elbow / knee around the shoulder -> hand (hip -> foot) line so it follows the cursor.
    private func TurnElbowTowardsCursor(camera: ref<CameraSystem>, toWorld: Matrix, chain: Int32, dx: Float, dy: Float) {
        let aModel = APM_GizmoGetChainJoint(chain, 0);
        let bModel = APM_GizmoGetChainJoint(chain, 1);
        let cModel = APM_GizmoGetChainJoint(chain, 2);
        if aModel.W < 0.5 || bModel.W < 0.5 || cModel.W < 0.5 {
            return;
        }
        let a = Vector4.Transform(toWorld, APM_Vector(aModel.X, aModel.Y, aModel.Z, 1.0));
        let b = Vector4.Transform(toWorld, APM_Vector(bModel.X, bModel.Y, bModel.Z, 1.0));
        let c = Vector4.Transform(toWorld, APM_Vector(cModel.X, cModel.Y, cModel.Z, 1.0));
        // Swivel axis (A -> C) and the elbow's offset from it.
        let nx = c.X - a.X;
        let ny = c.Y - a.Y;
        let nz = c.Z - a.Z;
        let nLength = SqrtF(nx * nx + ny * ny + nz * nz);
        if nLength < 0.01 {
            return;
        }
        nx /= nLength;
        ny /= nLength;
        nz /= nLength;
        let px = b.X - a.X;
        let py = b.Y - a.Y;
        let pz = b.Z - a.Z;
        let along = px * nx + py * ny + pz * nz;
        let rx = px - nx * along;
        let ry = py - ny * along;
        let rz = pz - nz * along;
        let radius = SqrtF(rx * rx + ry * ry + rz * rz);
        if radius < 0.02 {
            return; // limb straight: no elbow direction to turn
        }
        // Direction the elbow moves for a positive swivel: axis x offset.
        let tx = (ny * rz - nz * ry) / radius;
        let ty = (nz * rx - nx * rz) / radius;
        let tz = (nx * ry - ny * rx) / radius;
        let bScreen = this.ProjectToScreen(camera, APM_Vector(b.X, b.Y, b.Z, 1.0));
        let move = this.CursorToWorld(camera, bScreen.W > 0.5 ? bScreen.Z : 2.0, dx, dy);
        let degrees = Rad2Deg((move.X * tx + move.Y * ty + move.Z * tz) / radius);
        this.m_dragSwivel += degrees;
        if this.m_dragSwivel > 180.0 {
            this.m_dragSwivel -= 360.0;
        }
        if this.m_dragSwivel < -180.0 {
            this.m_dragSwivel += 360.0;
        }
        APM_UiSetValue(7710u, Cast<Float>(chain));
        APM_UiSetValue(7715u, this.m_dragSwivel);
    }

    // Rotation around the bone itself: the side facing the camera follows the cursor (like rolling a pencil).
    // When the bone points at the camera, left/right turns it like a knob.
    private func TwistPart(camera: ref<CameraSystem>, toWorld: Matrix, part: Int32, dx: Float, dy: Float) {
        let joint = APM_PartWorldPosition(toWorld, part);
        let tipModel = APM_GizmoGetPartTip(part);
        if joint.W < 0.5 || tipModel.W < 0.5 {
            return;
        }
        let tip = Vector4.Transform(toWorld, APM_Vector(tipModel.X, tipModel.Y, tipModel.Z, 1.0));
        let ax = tip.X - joint.X;
        let ay = tip.Y - joint.Y;
        let az = tip.Z - joint.Z;
        let length = SqrtF(ax * ax + ay * ay + az * az);
        if length < 0.001 {
            return;
        }
        ax /= length;
        ay /= length;
        az /= length;
        let forward = camera.GetActiveCameraForward();
        let right = camera.GetActiveCameraRight();
        let up = camera.GetActiveCameraUp();
        // Movement of the bone's front surface for a positive twist: axis x (towards the camera).
        let sx = -(ay * forward.Z - az * forward.Y);
        let sy = -(az * forward.X - ax * forward.Z);
        let sz = -(ax * forward.Y - ay * forward.X);
        let screenX = sx * right.X + sy * right.Y + sz * right.Z;
        let screenY = -(sx * up.X + sy * up.Y + sz * up.Z);
        let screenLength = SqrtF(screenX * screenX + screenY * screenY);
        let degrees: Float;
        let axis = APM_Vector(ax, ay, az, 0.0);
        if screenLength > 0.35 {
            degrees = (dx * screenX + dy * screenY) / screenLength * this.DegreesPerCanvasUnit() * 1.5;
        } else {
            // Knob: around the bone pointing away from the camera, so dragging right turns it clockwise.
            if ax * forward.X + ay * forward.Y + az * forward.Z < 0.0 {
                axis = APM_Vector(-ax, -ay, -az, 0.0);
            }
            degrees = dx * this.DegreesPerCanvasUnit() * 1.5;
        }
        if AbsF(degrees) > 0.0001 {
            APM_GizmoRotatePart(part, APM_WorldToModelDirection(toWorld, axis), degrees);
        }
    }

    // Moves the point both eyes look at with the cursor and turns each eye by the same change of direction.
    // Moves the point both eyes look at with the cursor (on a plane facing the camera) and aims each eye at it,
    // so they converge on it.
    private func MoveEyeLook(camera: ref<CameraSystem>, toWorld: Matrix, dx: Float, dy: Float) {
        this.m_dragCursorX += dx;
        this.m_dragCursorY += dy;
        let target = this.UnprojectFromScreen(camera, this.m_dragCursorX, this.m_dragCursorY, this.m_dragDepth);
        if target.W < 0.5 {
            return;
        }
        let model = APM_WorldToModelPoint(toWorld, target);
        if this.IsSingleEye(this.m_dragMarker.part) {
            APM_GizmoAimPart(this.m_dragMarker.part, model);
        } else {
            APM_GizmoAimPart(this.m_leftEyePart, model);
            APM_GizmoAimPart(this.m_rightEyePart, model);
        }
        this.m_lookTarget = target;
        // The marker stays at this distance once dropped (from the eye, or from between the eyes).
        let from: Vector4;
        if this.IsSingleEye(this.m_dragMarker.part) {
            from = APM_PartWorldPosition(toWorld, this.m_dragMarker.part);
        } else {
            let left = APM_PartWorldPosition(toWorld, this.m_leftEyePart);
            let right = APM_PartWorldPosition(toWorld, this.m_rightEyePart);
            from = APM_Vector((left.X + right.X) * 0.5, (left.Y + right.Y) * 0.5, (left.Z + right.Z) * 0.5, MinF(left.W, right.W));
        }
        if from.W > 0.5 {
            this.m_eyeLookDistance = Vector4.Distance(APM_Vector(from.X, from.Y, from.Z, 0.0), APM_Vector(target.X, target.Y, target.Z, 0.0));
            this.m_eyeLookPinned = true;
            this.m_eyeLookPinForward = APM_Vector(0.0, 0.0, 0.0, 0.0);
        }
        APM_UiSetValue(7701u, Cast<Float>(this.m_dragMarker.part));
    }

    // Parts that turn like a ball under the cursor instead of pointing at it.
    private func IsTrackballPart(part: Int32) -> Bool {
        let label = APM_UiGetOptionLabel(7701u, part);
        return Equals(label, "Head");
    }

    // Meters per ink canvas unit at a given distance in front of the camera.
    private func MetersPerCanvasUnit(camera: ref<CameraSystem>, depth: Float) -> Float {
        let fov = camera.GetActiveCameraFOV();
        if fov < 1.0 || fov > 170.0 {
            fov = 60.0;
        }
        return 2.0 * depth * TanF(Deg2Rad(fov) * 0.5) / APM_ScreenHeight();
    }

    // Short bones are turned as if they were at least this long (meters), so they do not spin too fast: hands, feet,
    // toes and held items (3-8 cm) 30 cm; necks and lower back (7-8 cm) 15 cm. Fingers and palm keep their own
    // length: their tip follows the cursor (their markers only show close up).
    private func MinimumLever(marker: ref<APM_Marker>) -> Float {
        if marker.chain >= 0 {
            return 0.3;
        }
        let label = APM_UiGetOptionLabel(7701u, marker.part);
        if Equals(label, "Left toes") || Equals(label, "Right toes") || Equals(label, "Left hand prop") || Equals(label, "Right hand prop") {
            return 0.3;
        }
        if Equals(label, "Neck") || Equals(label, "Upper neck") || Equals(label, "Lower back") {
            return 0.15;
        }
        return 0.0;
    }

    // Rotates a part around its joint so that the point it points at (its child joint) follows the cursor.
    // minLever: a short bone's tip is treated as at least that far away (a calmer, finer turn, like the arms).
    private func RotateTipTowardsCursor(camera: ref<CameraSystem>, toWorld: Matrix, part: Int32, dx: Float, dy: Float, minLever: Float) {
        let joint = APM_PartWorldPosition(toWorld, part);
        let tipModel = APM_GizmoGetPartTip(part);
        if joint.W < 0.5 || tipModel.W < 0.5 {
            return;
        }
        let tip = Vector4.Transform(toWorld, APM_Vector(tipModel.X, tipModel.Y, tipModel.Z, 1.0));
        let tipScreen = this.ProjectToScreen(camera, APM_Vector(tip.X, tip.Y, tip.Z, 1.0));
        // Cursor movement in the world, on the plane facing the camera at the tip.
        let move = this.CursorToWorld(camera, tipScreen.W > 0.5 ? tipScreen.Z : 2.0, dx, dy);
        let mx = move.X;
        let my = move.Y;
        let mz = move.Z;
        let bx = tip.X - joint.X;
        let by = tip.Y - joint.Y;
        let bz = tip.Z - joint.Z;
        let lengthSq = bx * bx + by * by + bz * bz;
        if lengthSq < 0.0001 {
            return;
        }
        // Axis = bone x movement; angle = movement across the bone / bone length.
        let ax = by * mz - bz * my;
        let ay = bz * mx - bx * mz;
        let az = bx * my - by * mx;
        let axisLength = SqrtF(ax * ax + ay * ay + az * az);
        if axisLength < 0.000001 {
            return;
        }
        let length = SqrtF(lengthSq);
        let lever = MaxF(length, minLever);
        let degrees = Rad2Deg(axisLength / (length * lever));
        let axis = APM_Vector(ax / axisLength, ay / axisLength, az / axisLength, 0.0);
        APM_GizmoRotatePart(part, APM_WorldToModelDirection(toWorld, axis), degrees);
    }

    // --- Actions -------------------------------------------------------------------------------------------------

    private func StepRow(row: ref<APM_ValueRow>, direction: Float) {
        let value = ClampF(this.RowValue(row) + row.step * direction, row.minValue, row.maxValue);
        this.SetRowValue(row, value);
    }

    private func RunButton(button: ref<APM_Button>) {
        let id = button.id;
        let prefix = StrBeforeFirst(id, ":");
        let arg = StrAfterFirst(id, ":");
        let keepResetConfirm = false;
        let keepDeleteConfirm = false;
        let keepOverwriteConfirm = false;

        if Equals(id, "open") {
            this.SetOpen(true);
            return;
        }
        if Equals(prefix, "char") {
            this.StepCharacter(Equals(arg, "next") ? 1 : -1);
            return;
        }
        if Equals(StrBeforeFirst(id, ":"), "pick") {
            this.RunPickerButton(StrAfterFirst(id, ":"));
            return;
        }
        if this.m_pickerOpen && !StrBeginsWith(id, "prop:open") {
            this.ClosePicker();
        }
        // Clicking anything while typing a name confirms it first ("Rename" again just confirms).
        if this.m_renaming {
            this.FinishRename(true);
            if Equals(id, "rename") {
                APM_UiRequestRefresh();
                this.Refresh();
                return;
            }
        }
        if Equals(id, "hide") {
            this.SetOpen(false);
            return;
        }
        if Equals(id, "help") || Equals(id, "helpclose") {
            this.SetHelpOpen(Equals(id, "help") && !this.m_helpOpen);
            return;
        }
        if Equals(id, "opacity") {
            this.m_opacityStep = (this.m_opacityStep + 1) % 4;
            APM_PrefSet("OpacityStep", Cast<Float>(this.m_opacityStep));
            this.ApplyOpacity();
            return;
        }

        if Equals(id, "enabled") {
            APM_UiSetValue(7700u, APM_UiGetValue(7700u) > 0.5 ? 0.0 : 1.0);
        } else if Equals(prefix, "axes") {
            this.ToggleClassicAxes();
        } else if Equals(prefix, "classic") {
            this.SetClassicMode(StringToInt(arg));
        } else if Equals(id, "gizmos") {
            this.m_gizmosOn = !this.m_gizmosOn;
            APM_PrefSet("Gizmos", this.m_gizmosOn ? 1.0 : 0.0);
        } else if Equals(prefix, "group") {
            let groupIndex = StringToInt(arg);
            if groupIndex >= 0 && groupIndex < ArraySize(this.m_groupNames) {
                APM_UiSetValue(7701u, Cast<Float>(this.FirstPartOfGroup(this.m_groupNames[groupIndex])));
            }
        } else if Equals(prefix, "part") {
            let partSlot = StringToInt(arg);
            if partSlot >= 0 && partSlot < ArraySize(this.m_partIndices) && this.m_partIndices[partSlot] >= 0 {
                APM_UiSetValue(7701u, Cast<Float>(this.m_partIndices[partSlot]));
            }
        } else if Equals(prefix, "dec") || Equals(prefix, "inc") || Equals(prefix, "zero") {
            APM_HistoryCheckpoint();
            let row = this.FindRow(Cast<Uint32>(StringToInt(arg)));
            if IsDefined(row) {
                if Equals(prefix, "zero") {
                    this.SetRowValue(row, 0.0);
                } else {
                    this.StepRow(row, Equals(prefix, "inc") ? 1.0 : -1.0);
                }
            }
        } else if Equals(prefix, "ik") {
            let limb = StringToInt(arg);
            APM_UiSetValue(7710u, Cast<Float>(limb));
            // Also select that hand / foot above, so its rotation rows are at hand.
            let limbPart = this.PartIndex(APM_UiGetOptionLabel(7710u, limb));
            if limbPart >= 0 {
                APM_UiSetValue(7701u, Cast<Float>(limbPart));
            }
        } else if Equals(prefix, "pin") {
            APM_HistoryCheckpoint();
            // World keeps the hand / foot where it is now; body makes it follow the shoulder / hips.
            APM_UiSetValue(7711u, Cast<Float>(StringToInt(arg)));
        } else if Equals(id, "poses:open") || Equals(id, "poses:close") {
            this.m_posesOpen = Equals(id, "poses:open");
            APM_SessionSet("PosesOpen", this.m_posesOpen ? 1.0 : 0.0);
        } else if Equals(prefix, "prop") {
            this.RunPropButton(arg);
        } else if Equals(prefix, "mode") {
            let faceMode = Equals(arg, "face");
            if NotEquals(faceMode, this.m_faceMode) {
                this.m_faceMode = faceMode;
                APM_SessionSet("FaceMode", this.m_faceMode ? 1.0 : 0.0);
                this.m_selectedPose = ""; // the list switches between the poses and the expressions
                this.m_posePage = 0;
                this.ReloadPoses();
            }
        } else if Equals(id, "face:unlock") || Equals(id, "face:lock") {
            this.SetFaceUnlocked(Equals(id, "face:unlock"));
        } else if Equals(id, "face:linked") {
            this.m_faceRowPage = 0;
            this.m_faceLinked = !this.m_faceLinked;
            APM_FaceSetLinked(this.m_faceLinked);
            APM_PrefSet("FaceLinked", this.m_faceLinked ? 1.0 : 0.0);
        } else if Equals(id, "poses:folder") {
            let folderKind = this.LibraryKind();
            APM_OpenFolder(folderKind == 2 ? 1 : (folderKind == 1 ? 2 : 0), this.LibraryDiskFolder());
        } else if Equals(prefix, "lib") {
            if NotEquals(Equals(arg, "hands"), this.m_handLibrary) {
                this.ShowLibrary(Equals(arg, "hands"));
            }
        } else if Equals(prefix, "facerowreset") {
            APM_HistoryCheckpoint();
            let resetRow = this.FindRow(Cast<Uint32>(StringToInt(arg)));
            if IsDefined(resetRow) {
                APM_FaceResetPose(Cast<Int32>(resetRow.attribute) - 7800);
                if resetRow.pairAttribute != 0u {
                    APM_FaceResetPose(Cast<Int32>(resetRow.pairAttribute) - 7800);
                }
            }
        } else if Equals(prefix, "facezone") {
            let zone = 0;
            while zone < ArraySize(this.m_faceZones) {
                if Equals(this.m_faceZones[zone], arg) {
                    if zone != this.m_faceZone {
                        this.m_faceRowPage = 0;
                    }
                    this.m_faceZone = zone;
                    APM_SessionSet("FaceZone", Cast<Float>(zone));
                }
                zone += 1;
            }
        } else if Equals(prefix, "facepage") {
            this.m_faceRowPage += Equals(arg, "next") ? 1 : -1; // clamped by LayoutFace
        } else if Equals(id, "undo") {
            APM_Undo();
        } else if Equals(id, "redo") {
            APM_Redo();
        } else if Equals(prefix, "eyes") {
            let which = StringToInt(arg);
            let eyeTarget = which == 1 ? this.m_leftEyePart : (which == 2 ? this.m_rightEyePart : this.PartIndex("Eyes (both)"));
            if eyeTarget >= 0 {
                APM_UiSetValue(7701u, Cast<Float>(eyeTarget));
            }
        } else if Equals(id, "autoik") {
            this.m_autoIk = !this.m_autoIk;
            APM_SessionSet("AutoIk", this.m_autoIk ? 1.0 : 0.0);
        } else if Equals(prefix, "limits") {
            APM_SetLimits(!APM_AreLimitsOn());
        } else if Equals(id, "mirror") {
            APM_SetMirror(!APM_IsMirror());
        } else if Equals(id, "partreset") {
            this.ResetPart(Cast<Int32>(APM_UiGetValue(7701u)));
        } else if Equals(id, "resetall") {
            // The part shown: Face resets the expression, Body the pose and the items in hand.
            if this.m_confirmResetAll {
                if this.m_faceMode {
                    APM_FaceReset();
                } else {
                    APM_ResetBody(); // pose, IK, base pose and items in hand (one undo step)
                }
            } else {
                keepResetConfirm = true;
                this.SetStatus(this.m_faceMode ? APM_Text("APM-Msg-ResetFaceConfirm", "Click again to reset the face (the Photo Mode expression)") : APM_Text("APM-Msg-ResetBodyConfirm", "Click again to reset the body (pose, items in hand)"));
            }
        } else if Equals(prefix, "pose") {
            let index = this.m_posePage * APM_PosesPerPage() + StringToInt(arg);
            if index >= 0 && index < ArraySize(this.m_poseNames) {
                let entry = this.m_poseNames[index];
                if Equals(entry, "::up") {
                    this.OpenLibraryFolder(this.ParentFolder(this.LibraryFolder()));
                } else if StrEndsWith(entry, "/") {
                    this.OpenLibraryFolder(entry);
                } else {
                    this.m_selectedPose = entry;
                }
            }
        } else if Equals(id, "savenew") {
            let saveKind = this.LibraryKind();
            let saveHand = saveKind == 1 ? this.HandPoseTarget() : 0;
            if saveKind != 1 || saveHand > 0 {
                if StrBeginsWith(this.LibraryFolder(), "::") {
                    this.OpenLibraryFolder(""); // saved in the library's own folder, shown there
                }
                let saveFolder = this.LibraryDiskFolder(); // in the open folder
                let name = saveKind == 2 ? APM_FaceSaveNew(saveFolder) : (saveKind == 1 ? APM_SaveNewHandPose(saveFolder, saveHand == 2) : APM_SaveNewPose(saveFolder));
                this.ReloadPoses();
                if StrLen(name) > 0 {
                    this.m_selectedPose = name;
                    this.ShowPose(name);
                    this.StartRename(name); // type a name right away (Esc keeps the default one)
                } else {
                    this.SetStatus(APM_Text("APM-Msg-PoseSaveFailed", "Could not save the pose"));
                }
            }
        } else if Equals(id, "overwrite") {
            if StrLen(this.m_selectedPose) > 0 && !this.IsBuiltIn(this.m_selectedPose) && !this.m_confirmOverwrite {
                keepOverwriteConfirm = true; // the first click asks for confirmation
            } else if StrLen(this.m_selectedPose) > 0 && !this.IsBuiltIn(this.m_selectedPose) {
                if this.LibraryKind() == 2 {
                    if !APM_FaceSave(this.m_selectedPose) {
                        this.SetStatus(APM_Text("APM-Msg-ExpressionSaveFailed", "Could not save the expression"));
                    }
                } else if this.m_handLibrary {
                    let overwriteHand = this.HandPoseTarget();
                    if overwriteHand > 0 {
                        APM_SaveHandPose(this.m_selectedPose, overwriteHand == 2);
                    }
                } else {
                    this.SavePoseWithItems(this.m_selectedPose);
                }
            }
        } else if Equals(id, "rename") {
            if StrLen(this.m_selectedPose) > 0 && !this.IsBuiltIn(this.m_selectedPose) {
                this.StartRename(this.m_selectedPose);
            }
        } else if Equals(id, "load") {
            if this.IsBuiltIn(this.m_selectedPose) {
                this.LoadBuiltIn(this.m_selectedPose);
            } else if StrLen(this.m_selectedPose) > 0 {
                let loaded = true;
                if this.LibraryKind() == 2 {
                    loaded = APM_FaceLoad(this.m_selectedPose);
                } else if this.m_handLibrary {
                    let loadHand = this.HandPoseTarget();
                    if loadHand > 0 {
                        APM_HistoryCheckpoint();
                        loaded = APM_LoadHandPose(this.m_selectedPose, loadHand == 2);
                    }
                } else {
                    loaded = APM_LoadPose(this.m_selectedPose);
                }
                if !loaded {
                    this.SetStatus(APM_TextWith("APM-Msg-PoseLoadFailed", "Could not load {0}", this.m_selectedPose));
                }
            }
        } else if Equals(id, "delete") {
            if StrLen(this.m_selectedPose) > 0 && !this.IsBuiltIn(this.m_selectedPose) {
                if this.m_confirmDelete {
                    if this.LibraryKind() == 2 {
                        APM_FaceDelete(this.m_selectedPose);
                    } else if this.m_handLibrary {
                        APM_DeleteHandPose(this.m_selectedPose);
                    } else {
                        APM_DeletePose(this.m_selectedPose);
                    }
                    this.m_selectedPose = "";
                    this.ReloadPoses();
                } else {
                    keepDeleteConfirm = true;
                }
            }
        } else if Equals(id, "pageprev") {
            if this.m_posePage > 0 {
                this.m_posePage -= 1;
            }
        } else if Equals(id, "pagenext") {
            if this.m_posePage < this.PageCount() - 1 {
                this.m_posePage += 1;
            }
        }

        this.m_confirmResetAll = keepResetConfirm;
        this.m_confirmDelete = keepDeleteConfirm;
        this.m_confirmOverwrite = keepOverwriteConfirm;
        APM_UiRequestRefresh(); // keeps the Photo Mode menu items in sync
        this.Refresh();
    }

    // --- Characters -----------------------------------------------------------------------------------------------

    private func CharacterName(character: Int32) -> String {
        if character == 0 {
            return "V";
        }
        let puppet = APM_CharacterGetEntity(character) as gamePuppet;
        let name = "";
        if IsDefined(puppet) {
            name = APM_RecordName(puppet.GetRecordID());
            if StrLen(name) == 0 {
                name = APM_ReadableName(puppet.GetDisplayName());
            }
        }
        if StrLen(name) == 0 {
            return APM_TextWith("APM-Label-Character", "Character {0}", IntToString(character));
        }
        return name;
    }

    private func HasAddedCharacters() -> Bool {
        let c = 1;
        while c < 4 {
            if APM_CharacterIsPresent(c) {
                return true;
            }
            c += 1;
        }
        return false;
    }

    // The selector: the selected character's name, shown only while characters are added in the scene.
    private func RefreshCharacter() {
        let show = this.HasAddedCharacters();
        if IsDefined(this.m_sectionCharacter) && NotEquals(this.m_sectionCharacter.IsVisible(), show) {
            this.Layout(); // the selector row appears / disappears
        }
        let prev = this.FindButton("char:prev");
        let next = this.FindButton("char:next");
        if IsDefined(prev) {
            prev.root.SetVisible(show);
        }
        if IsDefined(next) {
            next.root.SetVisible(show);
        }
        if IsDefined(this.m_characterName) {
            this.m_characterName.SetVisible(show);
            this.m_characterName.SetText(this.CharacterName(this.m_character));
            this.m_characterName.SetTintColor(APM_CharacterHasEdits(this.m_character) ? APM_ColorAccent() : APM_ColorText());
        }
    }

    // Next / previous character in the scene (V, then the added ones).
    private func StepCharacter(direction: Int32) {
        let c = this.m_character;
        let i = 0;
        while i < 4 {
            c = (c + direction + 4) % 4;
            if c == 0 || APM_CharacterIsPresent(c) {
                this.SwitchCharacter(c);
                return;
            }
            i += 1;
        }
    }

    private func SwitchCharacter(character: Int32) {
        if character == this.m_character {
            return;
        }
        this.SaveCharacterProps(this.m_character);
        APM_CharacterSelect(character);
        this.m_character = APM_CharacterGetSelected();
        this.LoadCharacterProps(this.m_character);
        APM_UiRequestRefresh();
        this.Refresh();
    }

    // The plugin went back to V (the selected character left the scene) or Photo Mode reopened.
    private func FollowSelectedCharacter() {
        let selected = APM_CharacterGetSelected();
        if selected != this.m_character {
            this.m_character = selected;
            this.LoadCharacterProps(selected);
            this.Refresh();
            return;
        }
        // Another character took the selected one's slot: nothing is known about the items it holds.
        this.EnsureCharacterArrays();
        let puppet = APM_CharacterGetEntity(selected);
        if IsDefined(puppet) && IsDefined(this.m_charPuppets[selected]) && this.m_charPuppets[selected] != puppet {
            this.m_shownRight = 0ul;
            this.m_shownLeft = 0ul;
            this.m_hasPoseItemRight = false;
            this.m_hasPoseItemLeft = false;
            this.m_checkTimer = 0.0;
            this.m_poseItemDelay = 0.5;
        }
        this.m_charPuppets[selected] = puppet;
    }

    private func EnsureCharacterArrays() {
        if ArraySize(this.m_charShownRight) == 4 {
            return;
        }
        ArrayResize(this.m_charShownRight, 4);
        ArrayResize(this.m_charShownLeft, 4);
        ArrayResize(this.m_charPoseItemRight, 4);
        ArrayResize(this.m_charPoseItemLeft, 4);
        ArrayResize(this.m_charHasPoseItemRight, 4);
        ArrayResize(this.m_charHasPoseItemLeft, 4);
        ArrayResize(this.m_charPuppets, 4);
    }

    private func SaveCharacterProps(character: Int32) {
        this.EnsureCharacterArrays();
        this.m_charShownRight[character] = this.m_shownRight;
        this.m_charShownLeft[character] = this.m_shownLeft;
        this.m_charPoseItemRight[character] = this.m_poseItemRight;
        this.m_charPoseItemLeft[character] = this.m_poseItemLeft;
        this.m_charHasPoseItemRight[character] = this.m_hasPoseItemRight;
        this.m_charHasPoseItemLeft[character] = this.m_hasPoseItemLeft;
        this.m_charPuppets[character] = APM_CharacterGetEntity(character);
    }

    // Items on the newly selected character; nothing known if another character took that slot meanwhile.
    private func LoadCharacterProps(character: Int32) {
        this.EnsureCharacterArrays();
        let puppet = APM_CharacterGetEntity(character);
        let same = IsDefined(puppet) && IsDefined(this.m_charPuppets[character]) && this.m_charPuppets[character] == puppet;
        this.m_shownRight = same ? this.m_charShownRight[character] : 0ul;
        this.m_shownLeft = same ? this.m_charShownLeft[character] : 0ul;
        this.m_poseItemRight = this.m_charPoseItemRight[character];
        this.m_poseItemLeft = this.m_charPoseItemLeft[character];
        this.m_hasPoseItemRight = same && this.m_charHasPoseItemRight[character];
        this.m_hasPoseItemLeft = same && this.m_charHasPoseItemLeft[character];
        this.m_checkTimer = 0.0;
        this.m_poseItemDelay = 0.5;
    }

    private func ResetCharacterProps() {
        ArrayClear(this.m_charShownRight);
        ArrayClear(this.m_charShownLeft);
        ArrayClear(this.m_charPoseItemRight);
        ArrayClear(this.m_charPoseItemLeft);
        ArrayClear(this.m_charHasPoseItemRight);
        ArrayClear(this.m_charHasPoseItemLeft);
        ArrayClear(this.m_charPuppets);
        this.m_character = 0;
    }

    // The puppet of the character the panel works on.
    private func SelectedPuppet() -> ref<GameObject> {
        return APM_CharacterGetEntity(this.m_character) as GameObject;
    }

    // --- Translated texts ---------------------------------------------------------------------------------------

    private func L(key: String, english: String) -> String {
        let shown = APM_Text(key, english);
        ArrayPush(this.m_pendingKeys, key);
        ArrayPush(this.m_pendingEnglish, english);
        ArrayPush(this.m_pendingShown, shown);
        return shown;
    }

    private func LGroup(group: String) -> String {
        return this.L("APM-Group-" + APM_KeyName(group), group);
    }

    private func LPart(label: String) -> String {
        return this.L("APM-Part-" + APM_KeyName(label), label);
    }

    // Index of the queued text shown as aText (-1 when the text is not a translated one).
    private func TakePending(text: String) -> Int32 {
        let i = 0;
        while i < ArraySize(this.m_pendingShown) {
            if Equals(this.m_pendingShown[i], text) {
                return i;
            }
            i += 1;
        }
        return -1;
    }

    private func DropPending(index: Int32) {
        ArrayErase(this.m_pendingKeys, index);
        ArrayErase(this.m_pendingEnglish, index);
        ArrayErase(this.m_pendingShown, index);
    }

    private func RegisterText(widget: wref<inkText>, text: String) {
        if StrLen(text) == 0 {
            return;
        }
        let index = this.TakePending(text);
        if index >= 0 {
            ArrayPush(this.m_locTexts, widget);
            ArrayPush(this.m_locTextKeys, this.m_pendingKeys[index]);
            ArrayPush(this.m_locTextEnglish, this.m_pendingEnglish[index]);
            this.DropPending(index);
        }
    }

    private func RegisterTip(button: ref<APM_Button>, text: String) {
        if StrLen(text) == 0 {
            return;
        }
        let index = this.TakePending(text);
        if index >= 0 {
            this.TipL(button, this.m_pendingKeys[index], this.m_pendingEnglish[index]);
            this.DropPending(index);
        }
    }

    private func TipL(button: ref<APM_Button>, key: String, english: String) {
        button.tooltip = APM_Text(key, english);
        ArrayPush(this.m_locTips, button);
        ArrayPush(this.m_locTipKeys, key);
        ArrayPush(this.m_locTipEnglish, english);
    }

    // Photo Mode opened: texts in the current interface language.
    private func Retranslate() {
        let i = 0;
        while i < ArraySize(this.m_locTexts) {
            if IsDefined(this.m_locTexts[i]) {
                this.m_locTexts[i].SetText(APM_Text(this.m_locTextKeys[i], this.m_locTextEnglish[i]));
            }
            i += 1;
        }
        i = 0;
        while i < ArraySize(this.m_locTips) {
            this.m_locTips[i].tooltip = APM_Text(this.m_locTipKeys[i], this.m_locTipEnglish[i]);
            i += 1;
        }
        this.Refresh();
    }

    // --- Hover while the camera moves -----------------------------------------------------------------------------

    // Photo Mode's UI hidden (its root faded out): nothing of ours can be seen, so nothing may take the mouse.
    private func IsUiHidden() -> Bool {
        let root = this.m_controller.GetRootWidget();
        return IsDefined(root) && root.GetOpacity() < 0.1;
    }

    // A hover may take the mouse from the camera: not while a button is held for the camera, not with the UI hidden.
    private func CanTakeHover() -> Bool {
        if IsDefined(this.m_dragMarker) || IsDefined(this.m_dragRow) || IsDefined(this.m_dragHandle) {
            return true;
        }
        return !this.IsUiHidden() && !APM_IsLeftMouseDown() && !APM_IsRightMouseDown();
    }

    private func HasWaitingHover() -> Bool {
        return IsDefined(this.m_waitMarker) || IsDefined(this.m_waitButton) || this.m_waitPanel || this.m_waitRoot;
    }

    private func UpdateWaitingHover() {
        if this.IsUiHidden() && !IsDefined(this.m_dragMarker) && !IsDefined(this.m_dragRow) {
            // The UI was hidden under the cursor: give the mouse back to the camera until it is shown again.
            this.ParkHovers();
            return;
        }
        if !this.HasWaitingHover() || !this.CanTakeHover() {
            return;
        }
        if IsDefined(this.m_waitMarker) {
            this.m_waitMarker.isHovered = true;
            this.m_waitMarker = null;
        }
        if IsDefined(this.m_waitButton) {
            this.m_waitButton.isHovered = true;
            this.m_waitButton.UpdateLook();
            if StrLen(this.m_waitButton.tooltip) > 0 {
                this.m_tipButton = this.m_waitButton;
                this.m_tipTimer = 0.0;
            }
            this.m_waitButton = null;
        }
        if this.m_waitPanel {
            this.m_panelHovered = true;
            this.m_waitPanel = false;
        }
        if this.m_waitRoot {
            this.m_rootHovered = true;
            this.m_waitRoot = false;
        }
        this.SetHover(true);
    }

    // Moves the active hovers to the waiting ones and releases the mouse.
    private func ParkHovers() {
        let any = false;
        let i = 0;
        while i < ArraySize(this.m_markers) {
            if this.m_markers[i].isHovered {
                this.m_markers[i].isHovered = false;
                this.m_waitMarker = this.m_markers[i];
                any = true;
            }
            i += 1;
        }
        i = 0;
        while i < ArraySize(this.m_buttons) {
            if this.m_buttons[i].isHovered {
                this.m_buttons[i].isHovered = false;
                this.m_buttons[i].UpdateLook();
                this.m_waitButton = this.m_buttons[i];
                any = true;
            }
            i += 1;
        }
        if this.m_panelHovered {
            this.m_panelHovered = false;
            this.m_waitPanel = true;
            any = true;
        }
        if this.m_rootHovered {
            this.m_rootHovered = false;
            this.m_waitRoot = true;
            any = true;
        }
        if any {
            this.m_tipButton = null;
            this.SetHover(false);
        }
    }

    // --- Callbacks -----------------------------------------------------------------------------------------------

    protected cb func OnPanelHoverOver(e: ref<inkPointerEvent>) -> Bool {
        if !this.CanTakeHover() {
            this.m_waitPanel = true;
            return false;
        }
        this.m_panelHovered = true;
        this.SetHover(true);
    }

    protected cb func OnPanelHoverOut(e: ref<inkPointerEvent>) -> Bool {
        this.m_waitPanel = false;
        this.m_panelHovered = false;
        if !IsDefined(this.m_dragRow) && !IsDefined(this.m_dragMarker) {
            this.SetHover(false);
        }
    }

    protected cb func OnButtonHoverOver(e: ref<inkPointerEvent>) -> Bool {
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if !this.CanTakeHover() {
            this.m_waitButton = button;
            return false;
        }
        if IsDefined(button) {
            button.isHovered = true;
            button.UpdateLook();
            if StrLen(button.tooltip) > 0 {
                this.m_tipButton = button;
                this.m_tipTimer = 0.0;
            }
        }
        this.SetHover(true);
    }

    protected cb func OnButtonHoverOut(e: ref<inkPointerEvent>) -> Bool {
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if IsDefined(button) && IsDefined(this.m_waitButton) && Equals(this.m_waitButton.id, button.id) {
            this.m_waitButton = null;
        }
        if IsDefined(button) {
            button.isHovered = false;
            button.UpdateLook();
            if IsDefined(this.m_tipButton) && Equals(this.m_tipButton.id, button.id) {
                this.m_tipButton = null;
                if this.m_messageTimer <= 0.0 {
                    this.HideTip();
                }
            }
            if Equals(button.id, "open") {
                this.SetHover(false); // the open button lives outside the panel
            }
        }
    }

    protected cb func OnButtonPress(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") || this.IsUiHidden() {
            return false;
        }
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if !IsDefined(button) || !button.isEnabled {
            return false;
        }
        this.m_pressedButtonId = button.id;
        if Equals(StrBeforeFirst(button.id, ":"), "drag") {
            let row = this.FindRow(Cast<Uint32>(StringToInt(StrAfterFirst(button.id, ":"))));
            if IsDefined(row) {
                APM_HistoryCheckpoint();
                this.m_dragRow = row;
                this.m_dragValue = this.RowValue(row);
                this.SetHover(true);
            }
        }
    }

    protected cb func OnButtonRelease(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") || this.IsUiHidden() {
            return false;
        }
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if !IsDefined(button) || !button.isEnabled {
            return false;
        }
        if Equals(StrBeforeFirst(button.id, ":"), "drag") {
            return false; // handled by the drag logic
        }
        // Only a press that started on this same button clicks it: the end of a drag (slider, marker, camera) over
        // a button does nothing.
        if !Equals(this.m_pressedButtonId, button.id) || IsDefined(this.m_dragRow) || IsDefined(this.m_dragMarker) || IsDefined(this.m_dragHandle) {
            return false;
        }
        this.m_pressedButtonId = "";
        // A click ends the hover tooltip; its feedback (if any) shows above this button.
        this.m_lastButton = button;
        this.m_tipTimer = -1000.0;
        if this.m_messageTimer <= 0.0 {
            this.HideTip();
        }
        this.RunButton(button);
    }

    protected cb func OnRootHoverOver(e: ref<inkPointerEvent>) -> Bool {
        if !this.CanTakeHover() {
            this.m_waitRoot = true;
            return false;
        }
        this.m_rootHovered = true;
        this.SetHover(true);
    }

    protected cb func OnRootHoverOut(e: ref<inkPointerEvent>) -> Bool {
        this.m_waitRoot = false;
        this.m_rootHovered = false;
        if !IsDefined(this.m_dragMarker) && !this.m_panelHovered {
            this.SetHover(false);
        }
    }

    protected cb func OnMarkerHoverOver(e: ref<inkPointerEvent>) -> Bool {
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
        if !this.CanTakeHover() {
            this.m_waitMarker = marker;
            return false;
        }
        if IsDefined(marker) {
            marker.isHovered = true;
        }
        this.SetHover(true);
    }

    protected cb func OnMarkerHoverOut(e: ref<inkPointerEvent>) -> Bool {
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
        if IsDefined(marker) && this.m_waitMarker == marker {
            this.m_waitMarker = null;
        }
        if IsDefined(marker) {
            marker.isHovered = false;
        }
        if !IsDefined(this.m_dragMarker) && !this.m_panelHovered {
            this.SetHover(false);
        }
    }

    protected cb func OnMarkerPress(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") {
            return false;
        }
        this.m_pressedButtonId = "";
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
        if !IsDefined(marker) || !marker.isHovered {
            return false; // hover not taken (camera moving, UI hidden): the click belongs to the camera
        }
        let wasSelected = Cast<Int32>(APM_UiGetValue(7701u)) == marker.part;
        APM_UiSetValue(7701u, Cast<Float>(marker.part));
        let locked = APM_GizmoIsPartLocked(marker.part);
        // Move arrows on: their center, the selected part's marker, moves the part freely as in Direct; the eyes' look
        // marker (no arrows) always moves.
        this.m_centerDrag = !locked && this.m_classicMode == APM_ClassicMove() && (marker.kind == APM_MarkerKindEyeLook() || (wasSelected && (marker.canMove || marker.chain >= 0)));
        if locked || (this.m_classicMode != APM_ClassicOff() && !this.m_centerDrag) {
            // Locked by Photo Mode's "Look at camera", or the classic gizmo is on: selected (its values and its
            // rings / arrows show), not dragged.
            // Turned by Photo Mode's "Look at camera": selected (its values show), but not dragged.
            APM_UiRequestRefresh();
            this.Refresh();
            return true;
        }
        // Calibrate pointer -> canvas scale with the marker under the cursor.
        let pointer = e.GetScreenSpacePosition();
        this.m_lastPointer = pointer;
        this.m_useRealCursor = false;
        if pointer.X > 10.0 && pointer.Y > 10.0 && marker.screenX > 10.0 && marker.screenY > 10.0 {
            this.m_cursorScaleX = marker.screenX / pointer.X;
            this.m_cursorScaleY = marker.screenY / pointer.Y;
            this.m_useRealCursor = this.m_cursorScaleX > 0.3 && this.m_cursorScaleX < 6.0 && this.m_cursorScaleY > 0.3 && this.m_cursorScaleY < 6.0;
        }
        APM_HistoryCheckpoint();
        this.BeginMarkerDrag(marker, e.IsShiftDown(), e.IsControlDown());
        this.SetHover(true);
        APM_UiRequestRefresh();
        this.Refresh();
    }

    protected cb func OnGlobalPress(e: ref<inkPointerEvent>) -> Bool {
        this.m_shiftDown = e.IsShiftDown();
        this.m_controlDown = e.IsControlDown();
        return false;
    }

    protected cb func OnGlobalRelative(e: ref<inkPointerEvent>) -> Bool {
        this.m_shiftDown = e.IsShiftDown();
        this.m_controlDown = e.IsControlDown();
        let horizontal = e.IsAction(n"mouse_x");
        if !horizontal && !e.IsAction(n"mouse_y") {
            return false;
        }

        if IsDefined(this.m_dragHandle) {
            let handlePointer = e.GetScreenSpacePosition();
            let handleX = handlePointer.X - this.m_lastPointer.X;
            let handleY = handlePointer.Y - this.m_lastPointer.Y;
            if this.m_useRealCursor && (AbsF(handleX) > 0.01 || AbsF(handleY) > 0.01) {
                this.m_lastPointer = handlePointer;
                this.DragHandle(handleX * this.m_cursorScaleX, handleY * this.m_cursorScaleY);
            } else if !this.m_useRealCursor {
                let handleAmount = e.GetAxisData() * this.FallbackCanvasPerMouseUnit();
                if horizontal {
                    this.DragHandle(handleAmount, 0.0);
                } else {
                    this.DragHandle(0.0, -handleAmount);
                }
            }
            return false;
        }

        if IsDefined(this.m_dragMarker) {
            // Shift / Ctrl pressed or released mid-drag: carry on with the other action from where things are now.
            let wanted = this.DragModeFor(this.m_dragMarker, this.m_shiftDown, this.m_controlDown);
            if wanted != this.m_dragMode {
                this.BeginMarkerDrag(this.m_dragMarker, this.m_shiftDown, this.m_controlDown);
                if !IsDefined(this.m_dragMarker) {
                    return false;
                }
            }
            // Follow the real cursor when the event carries its position, else the raw mouse movement.
            let pointer = e.GetScreenSpacePosition();
            let movedX = pointer.X - this.m_lastPointer.X;
            let movedY = pointer.Y - this.m_lastPointer.Y;
            if this.m_useRealCursor && (AbsF(movedX) > 0.01 || AbsF(movedY) > 0.01) {
                this.m_lastPointer = pointer;
                this.DragMarker(this.m_dragMarker, movedX * this.m_cursorScaleX, movedY * this.m_cursorScaleY);
            } else if !this.m_useRealCursor {
                let amount = e.GetAxisData() * this.FallbackCanvasPerMouseUnit();
                if horizontal {
                    this.DragMarker(this.m_dragMarker, amount, 0.0);
                } else {
                    this.DragMarker(this.m_dragMarker, 0.0, -amount); // mouse up = canvas up
                }
            }
            return false;
        }

        let row = this.m_dragRow;
        if !IsDefined(row) || !horizontal {
            return false;
        }
        this.m_dragValue = ClampF(this.m_dragValue + e.GetAxisData() * row.dragScale, row.minValue, row.maxValue);
        this.SetRowValue(row, this.m_dragValue);
        // Rotations stop at the joint limit while dragging (the stored value is the limited one).
        let isFocus = row.attribute == 7704u && Equals(APM_UiGetOptionLabel(7701u, Cast<Int32>(APM_UiGetValue(7701u))), "Eyes (both)");
        if APM_AreLimitsOn() && row.attribute >= 7702u && row.attribute <= 7704u && !isFocus {
            this.m_dragValue = APM_UiGetValue(row.attribute);
        }
        row.value.label.SetText(this.FormatValue(row, this.m_dragValue));
    }

    protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
        this.m_shiftDown = e.IsShiftDown();
        this.m_controlDown = e.IsControlDown();
        if !e.IsAction(n"click") {
            return false;
        }
        this.m_clearPressedButton = true; // forgotten at the next update, whatever order the release callbacks run in
        if IsDefined(this.m_dragRow) || IsDefined(this.m_dragMarker) || IsDefined(this.m_dragHandle) {
            this.m_dragRow = null;
            this.m_dragMarker = null;
            this.m_dragHandle = null;
            this.m_dragMode = 0;
            this.m_centerDrag = false;
            APM_SetWheelCapture(false);
            if !this.m_panelHovered {
                this.SetHover(false);
            }
            APM_UiRequestRefresh();
            this.Refresh();
        }
    }
}

// --- Photo Mode integration ----------------------------------------------------------------------------------------

@addField(gameuiPhotoModeMenuController)
private let m_apmPoseEditor: ref<APM_PoseEditor>;

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    this.m_apmPoseEditor = new APM_PoseEditor();
    this.m_apmPoseEditor.Initialize(this);
    return result;
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnUninitialize() -> Bool {
    if IsDefined(this.m_apmPoseEditor) {
        this.m_apmPoseEditor.Uninitialize();
        this.m_apmPoseEditor = null;
    }
    return wrappedMethod();
}

// Same as hovering the game's own menu: the camera ignores the mouse while the cursor is over the panel.
// The NPC page of Photo Mode switched to another added character: its look-at options belong to that one.
// True when any character has edits, a loaded pose or items in hand: leaving Photo Mode then asks for confirmation,
// as it does for the game's own options.
public func APM_AnyCharacterHasEdits() -> Bool {
    let c = 0;
    while c < 4 {
        if APM_CharacterHasEdits(c) {
            return true;
        }
        c += 1;
    }
    return false;
}

// Leaving Photo Mode (without the confirmation, or once it is confirmed): the pose stops being applied before the game
// tears Photo Mode down, as users did by resetting their edits before leaving (exits with edits crashed for some).
@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnTryExitPhotomode() -> Bool {
    if APM_AnyCharacterHasEdits() {
        this.m_anyOptionChanged = true; // the game asks before leaving: the pose would be lost
    }
    if !this.m_anyOptionChanged {
        if IsDefined(this.m_apmPoseEditor) {
            this.m_apmPoseEditor.RestorePlayerHands();
        }
        APM_OnPhotoModeExit();
    }
    return wrappedMethod();
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnExitConfirm(data: ref<inkGameNotificationData>) -> Bool {
    let resultData = data as GenericMessageNotificationCloseData;
    if IsDefined(resultData) && Equals(resultData.result, GenericMessageNotificationResult.Confirm) {
        if IsDefined(this.m_apmPoseEditor) {
            this.m_apmPoseEditor.RestorePlayerHands();
        }
        APM_OnPhotoModeExit();
    }
    return wrappedMethod(data);
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnChangeAspectRatioRate(aspectRatioRate: Float) -> Bool {
    let result = wrappedMethod(aspectRatioRate);
    if IsDefined(this.m_apmPoseEditor) {
        this.m_apmPoseEditor.OnFrameChanged();
    }
    return result;
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnSetBlackBars(enabled: Bool) -> Bool {
    let result = wrappedMethod(enabled);
    if IsDefined(this.m_apmPoseEditor) {
        this.m_apmPoseEditor.OnFrameChanged();
    }
    return result;
}

@addMethod(gameuiPhotoModeMenuController)
public final func APM_SetHover(hover: Bool) {
    if Equals(hover, this.m_IsHoverOver) {
        return;
    }
    this.m_IsHoverOver = hover;
    this.OnHoverStateChanged(hover);
    this.SetCursorContext(hover ? n"Hover" : n"Default");
}

// The game's own menu takes the mouse from the camera the same way: not while a button is held to move the camera
// (the hidden cursor crossing the menu stopped the rotation); the hover is taken once the buttons are released.
@addField(gameuiPhotoModeMenuController)
private let m_apmMenuHoverWaiting: Bool;

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnMenuHovered(e: ref<inkPointerEvent>) -> Bool {
    if APM_IsLeftMouseDown() || APM_IsRightMouseDown() {
        this.m_apmMenuHoverWaiting = true;
        return false;
    }
    return wrappedMethod(e);
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnMenuHoverOut(e: ref<inkPointerEvent>) -> Bool {
    this.m_apmMenuHoverWaiting = false;
    return wrappedMethod(e);
}

@addMethod(gameuiPhotoModeMenuController)
public final func APM_UpdatePoseEditor(timeDelta: Float) {
    if this.m_apmMenuHoverWaiting && !APM_IsLeftMouseDown() && !APM_IsRightMouseDown() {
        this.m_apmMenuHoverWaiting = false;
        if !this.m_IsHoverOver {
            this.OnHoverStateChanged(true);
            this.SetCursorContext(n"Hover");
        }
        this.m_IsHoverOver = true;
    }
    if IsDefined(this.m_apmPoseEditor) {
        this.m_apmPoseEditor.Update(timeDelta);
    }
}

// --- Character names ------------------------------------------------------------------------------------------------

// A text usable as a name: trimmed, with letters, not a localization key ("" otherwise).
public func APM_ReadableName(text: String) -> String {
    let name = text;
    while StrLen(name) > 0 && Equals(StrLeft(name, 1), " ") {
        name = StrMid(name, 1);
    }
    while StrLen(name) > 0 && Equals(StrRight(name, 1), " ") {
        name = StrLeft(name, StrLen(name) - 1);
    }
    if StrLen(name) == 0 || StrBeginsWith(name, "LocKey") || Equals(StrUpper(name), StrLower(name)) {
        return "";
    }
    return name;
}

// Localized text of a localization key ("" when it has none).
public func APM_KeyText(key: CName) -> String {
    if !IsNameValid(key) {
        return "";
    }
    let text = GetLocalizedTextByKey(key);
    if Equals(text, NameToString(key)) {
        return "";
    }
    return APM_ReadableName(text);
}

// Name of a Photo Mode character from its TweakDB record: its display name (Photo Mode characters often inherit the
// player's, "V": that one counts as none), else a name read from the record id. "" when none is found.
public func APM_RecordName(id: TweakDBID) -> String {
    let record = TweakDBInterface.GetCharacterRecord(id);
    if IsDefined(record) {
        let player = TweakDBInterface.GetCharacterRecord(t"Character.Player_Puppet_Base");
        let playerName = IsDefined(player) ? player.DisplayName() : n"";
        let display = record.DisplayName();
        if !Equals(display, playerName) {
            let name = APM_KeyText(display);
            if StrLen(name) > 0 {
                return name;
            }
        }
        let full = record.FullDisplayName();
        if !Equals(full, playerName) {
            let name = APM_KeyText(full);
            if StrLen(name) > 0 {
                return name;
            }
        }
    }
    return APM_NameFromRecordId(TDBID.ToStringDEBUG(id));
}

// "Character.Judy_Photomode_Puppet" -> "Judy", "Character.aa_RiverWard_Puppet" -> "River Ward" ("" when the id is only
// a hash, without debug names).
public func APM_NameFromRecordId(id: String) -> String {
    if !StrBeginsWith(id, "Character.") {
        return "";
    }
    let name = StrAfterFirst(id, "Character.");
    // Two-letter lower case sort prefix of some character packs ("aa_", "ab_", ...).
    if StrLen(name) > 3 && Equals(StrMid(name, 2, 1), "_") && Equals(StrMid(name, 0, 2), StrLower(StrMid(name, 0, 2))) {
        name = StrMid(name, 3);
    }
    let suffixes: array<String> = ["_pmne", "_Photomode", "_photomode", "_PhotoMode", "_Puppet", "_puppet"];
    for suffix in suffixes {
        if StrContains(name, suffix) {
            name = StrBeforeFirst(name, suffix);
        }
    }
    name = StrReplaceAll(name, "_", " ");
    // Words written together get a space: "RiverWard" -> "River Ward".
    let result = "";
    let i = 0;
    while i < StrLen(name) {
        let c = StrMid(name, i, 1);
        if i > 0 {
            let previous = StrMid(name, i - 1, 1);
            if NotEquals(c, StrLower(c)) && NotEquals(previous, StrUpper(previous)) {
                result += " ";
            }
        }
        result += c;
        i += 1;
    }
    return APM_ReadableName(result);
}
