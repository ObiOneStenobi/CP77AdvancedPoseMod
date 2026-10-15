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

public func APM_ScreenWidth() -> Float {
    return 3840.0;
}

public func APM_ScreenHeight() -> Float {
    return 2160.0;
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

// The face controls and saved expressions are not finished: hidden until a later version.
public func APM_FaceFeatureEnabled() -> Bool {
    return false;
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
    public let minValue: Float;
    public let maxValue: Float;
    public let step: Float;      // -/+ buttons
    public let dragScale: Float; // value change per unit of mouse movement
    public let unit: String;
    public let label: wref<inkText>;
    public let value: ref<APM_Button>;
    public let buttons: array<ref<APM_Button>>; // dec, inc, zero
}

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
    private let m_handPresets: wref<inkCanvas>; // preset row under the parts of a hand tab (and of the arm tab's hand) // body part index shown by each part button (-1 = unused)
    private let m_poseButtons: array<ref<APM_Button>>;
    private let m_poseNames: array<String>;
    private let m_posePage: Int32;
    private let m_selectedPose: String;
    private let m_partTitle: wref<inkText>;
    private let m_posesTitle: wref<inkText>;
    // Panel sections, stacked by Layout()
    private let m_sectionHeader: wref<inkCanvas>;
    private let m_sectionPart: wref<inkCanvas>;
    private let m_sectionRotation: wref<inkCanvas>;
    private let m_sectionMove: wref<inkCanvas>;
    private let m_sectionIk: wref<inkCanvas>;
    private let m_ikRows: wref<inkCanvas>;
    private let m_sectionPoses: wref<inkCanvas>;
    private let m_posesBody: wref<inkCanvas>;
    private let m_sectionItems: wref<inkCanvas>;
    private let m_sectionFace: wref<inkCanvas>;   // face controls and saved expressions (folds)
    private let m_faceBody: wref<inkCanvas>;
    private let m_faceOpen: Bool;
    private let m_faceInfo: wref<inkText>;
    private let m_faceZone: String;               // "Jaw", "Mouth", "Brows", "Eyelids", "Cheeks"
    private let m_faceRows: array<ref<APM_ValueRow>>; // one per face control (index = control)
    private let m_faceSelected: String;           // saved expression picked in the list
    private let m_faceConfirmDelete: Bool;
    private let m_posesOpen: Bool;
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
    private let m_lineLayer: wref<inkCanvas>;
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
    private let m_dragSwivel: Float;      // elbow / knee direction (degrees) while dragging it
    private let m_lookTarget: Vector4;    // world point the eyes look at while dragging it
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
    private let m_confirmResetAll: Bool;
    private let m_rightWasDown: Bool;
    private let m_confirmDelete: Bool;
    // Props: items put in V's hands
    private let m_propRight: Bool;            // the hand the picker works on
    private let m_shownRight: Uint64;         // items actually placed on the puppet
    private let m_shownLeft: Uint64;
    // Item the Photo Mode pose itself put in each hand (hand prop 0 = keep it, 1 = hand emptied on purpose)
    private let m_poseItemRight: ItemID;
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
        this.m_propRight = true;
        this.m_posesOpen = APM_SessionGet("PosesOpen", 0.0) > 0.5; // closed when the game starts
        this.m_faceOpen = APM_SessionGet("FaceOpen", 0.0) > 0.5;
        this.m_faceZone = "Mouth";
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

    public func Update(timeDelta: Float) {
        let active = APM_IsPhotoModeActive();
        if active && !this.m_wasActive {
            this.Retranslate();
        }
        this.m_wasActive = active;
        if active {
            this.FollowSelectedCharacter();
        }
        this.UpdateWaitingHover();
        this.UpdateTooltip(timeDelta);
        this.PublishCamera();
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
            this.CreateText(header, this.L("APM-Title", "Advanced pose"), 16.0, 8.0, 170.0, 32.0, 20, APM_ColorAccent());
        } else {
            // The game was updated and the mod could not hook into it: nothing would work.
            this.CreateText(header, this.L("APM-Msg-UnsupportedVersion", "Unsupported game version - see the log"), 16.0, 10.0, 260.0, 32.0, 13, APM_ColorWarning());
        }
        let enabledButton = this.CreateButton(header, "enabled", this.L("APM-Button-PoseOff", "Pose off"), 280.0, 8.0, 92.0, 32.0);
        this.TipL(enabledButton, "APM-Tip-PoseOnOff", "Turn your edits on V on / off");
        let gizmosButton = this.CreateButton(header, "gizmos", this.L("APM-Button-Gizmos", "Gizmos"), 376.0, 8.0, 92.0, 32.0);
        this.TipL(gizmosButton, "APM-Tip-Gizmos", "Show / hide the markers on V");
        let opacityButton = this.CreateButton(header, "opacity", "100%", 472.0, 8.0, 64.0, 32.0);
        this.TipL(opacityButton, "APM-Tip-Opacity", "Panel opacity");
        this.CreateIconButton(header, "help", "circle-help", 540.0, 8.0, this.L("APM-Tip-Help", "Info: markers and shortcuts"), 32.0);
        this.CreateIconButton(header, "hide", "x", 584.0, 8.0, this.L("APM-Tip-HidePanel", "Hide the panel"), 32.0);
        this.CreateIconButton(header, "undo", "undo-2", 16.0, 46.0, this.L("APM-Tip-Undo", "Undo (Ctrl+Z)"));
        this.CreateIconButton(header, "redo", "redo-2", 60.0, 46.0, this.L("APM-Tip-Redo", "Redo (Ctrl+Y)"));
        this.CreateIconButton(header, "mirror", "flip-vertical-2", 116.0, 46.0, this.L("APM-Tip-Mirror", "Mirror: while on, the limb you edit is copied to the other side and followed"));
        this.CreateIconButton(header, "limits:on", "lock", 160.0, 46.0, this.L("APM-Tip-LimitsOn", "Joint limits on: rotations stay natural. Click to unlock"));
        this.CreateIconButton(header, "limits:off", "lock-open", 160.0, 46.0, this.L("APM-Tip-LimitsOff", "Joint limits off: any rotation is allowed. Click to lock"));
        this.CreateIconButton(header, "resetall", "trash-2", 580.0, 46.0, this.L("APM-Tip-ResetAll", "Reset everything (click twice) - items in hand stay"));
        // Character selector: V and the characters added in the scene (shown only when there are some)
        let charPrev = this.CreateButton(header, "char:prev", "<", 252.0, 46.0, 44.0, 32.0);
        this.TipL(charPrev, "APM-Tip-CharacterPrev", "Previous character to pose");
        // The name fills a fixed box, so it stays centred between the arrows whatever its length.
        let charNameBox = new inkCanvas();
        charNameBox.SetAnchor(inkEAnchor.TopLeft);
        charNameBox.SetMargin(300.0 * APM_UiScale(), 46.0 * APM_UiScale(), 0.0, 0.0);
        charNameBox.SetSize(224.0 * APM_UiScale(), 32.0 * APM_UiScale());
        charNameBox.Reparent(header);
        let charName = this.CreateText(charNameBox, "V", 0.0, 0.0, 224.0, 32.0, 17, APM_ColorText());
        charName.SetAnchor(inkEAnchor.Fill);
        charName.SetMargin(0.0, 0.0, 0.0, 0.0);
        charName.SetHorizontalAlignment(textHorizontalAlignment.Center);
        charName.SetLetterCase(textLetterCase.OriginalCase);
        this.m_characterName = charName;
        let charNext = this.CreateButton(header, "char:next", ">", 528.0, 46.0, 44.0, 32.0);
        this.TipL(charNext, "APM-Tip-CharacterNext", "Next character to pose");
        this.CreateIconButton(header, "autoik", "magnet", 204.0, 46.0, this.L("APM-Tip-AutoIk", "Auto IK: dragging a hand / foot moves it with IK and pins it (Shift rotates it). Off: dragging rotates it, and IK is used only on limbs pinned in the IK row"));

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
        // Hand presets (hand tabs only)
        let presetRow = new inkCanvas();
        presetRow.SetName(n"APM_HandPresets");
        presetRow.SetAnchor(inkEAnchor.TopLeft);
        presetRow.SetMargin(0.0, 160.0 * APM_UiScale(), 0.0, 0.0);
        presetRow.SetSize(APM_PanelWidth() * APM_UiScale(), 36.0 * APM_UiScale());
        presetRow.Reparent(partSection);
        this.m_handPresets = presetRow;
        this.CreateText(presetRow, this.L("APM-Label-Preset", "Preset"), 16.0, 0.0, 90.0, 30.0, 15, APM_ColorDimText());
        let presetNames: array<String> = [this.L("APM-Preset-Open", "Open"), this.L("APM-Preset-Relaxed", "Relaxed"), this.L("APM-Preset-Fist", "Fist"), this.L("APM-Preset-Point", "Point"), this.L("APM-Preset-Grip", "Grip")];
        let presetTipKeys: array<String> = ["APM-Tip-PresetOpen", "APM-Tip-PresetRelaxed", "APM-Tip-PresetFist", "APM-Tip-PresetPoint", "APM-Tip-PresetGrip"];
        let presetTipTexts: array<String> = ["Straight fingers", "Slightly curled, at rest", "Closed fist", "Index finger pointing", "Fingers around a handle"];
        let p = 0;
        while p < 5 {
            let presetButton = this.CreateSmallButton(presetRow, "preset:" + IntToString(p), presetNames[p], 108.0 + Cast<Float>(p) * 103.0, 0.0, 99.0, 30.0);
            this.TipL(presetButton, presetTipKeys[p], presetTipTexts[p]);
            p += 1;
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
        this.m_posesTitle = this.CreateText(poses, this.L("APM-Title-Poses", "Poses"), 16.0, 6.0, 290.0, 28.0, 16, APM_ColorAccent());
        this.CreateSmallButton(poses, "neutral:t", this.L("APM-Button-TPose", "T pose"), 314.0, 6.0, 100.0, 28.0);
        this.CreateSmallButton(poses, "neutral:a", this.L("APM-Button-APose", "A pose"), 418.0, 6.0, 100.0, 28.0);
        this.CreateIconButton(poses, "poses:folder", "folder-open", 538.0, 6.0, this.L("APM-Tip-PosesFolder", "Open the folder of the saved poses (to share them; in full screen the game is minimized)"));
        this.CreateIconButton(poses, "poses:open", "chevron-down", 584.0, 6.0, this.L("APM-Tip-PosesShow", "Show the saved poses"));
        this.CreateIconButton(poses, "poses:close", "chevron-up", 584.0, 6.0, this.L("APM-Tip-PosesHide", "Hide the saved poses"));
        let posesBody = new inkCanvas();
        posesBody.SetName(n"APM_PosesBody");
        posesBody.SetAnchor(inkEAnchor.TopLeft);
        posesBody.SetMargin(0.0, 40.0 * s, 0.0, 0.0);
        posesBody.SetSize(width * s, 174.0 * s);
        posesBody.Reparent(poses);
        this.m_posesBody = posesBody;
        i = 0;
        while i < APM_PosesPerPage() {
            let poseButton = this.CreateButton(posesBody, "pose:" + IntToString(i), "", 16.0, Cast<Float>(i) * 34.0, 420.0, 30.0);
            poseButton.label.SetLetterCase(textLetterCase.OriginalCase);
            ArrayPush(this.m_poseButtons, poseButton);
            i += 1;
        }
        this.CreateButton(posesBody, "savenew", this.L("APM-Button-SaveNew", "Save new"), 446.0, 0.0, 178.0, 30.0);
        this.CreateButton(posesBody, "overwrite", this.L("APM-Button-Overwrite", "Overwrite"), 446.0, 34.0, 178.0, 30.0);
        this.CreateButton(posesBody, "load", this.L("APM-Button-Load", "Load"), 446.0, 68.0, 86.0, 30.0);
        this.CreateButton(posesBody, "rename", this.L("APM-Button-Rename", "Rename"), 538.0, 68.0, 86.0, 30.0);
        this.CreateButton(posesBody, "delete", this.L("APM-Button-Delete", "Delete"), 446.0, 102.0, 178.0, 30.0);
        this.CreateButton(posesBody, "pageprev", "<", 446.0, 136.0, 86.0, 30.0);
        this.CreateButton(posesBody, "pagenext", ">", 538.0, 136.0, 86.0, 30.0);

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

        // Face (folds): controls on top of the expression shown, and the saved expressions
        let face = this.CreateSection(n"APM_Face");
        this.m_sectionFace = face;
        this.AddSeparator(face);
        this.CreateText(face, this.L("APM-Title-Face", "Face"), 16.0, 6.0, 70.0, 28.0, 16, APM_ColorAccent());
        this.m_faceInfo = this.CreateText(face, "", 84.0, 8.0, 350.0, 26.0, 13, APM_ColorDimText());
        this.CreateIconButton(face, "face:unload", "x", 446.0, 6.0, this.L("APM-Tip-FaceUnload", "Back to the Photo Mode expression"));
        this.CreateIconButton(face, "face:folder", "folder-open", 492.0, 6.0, this.L("APM-Tip-FaceFolder", "Open the folder of the saved expressions (to share them; in full screen the game is minimized)"));
        this.CreateIconButton(face, "face:reset", "eraser", 538.0, 6.0, this.L("APM-Tip-FaceReset", "Reset the face: no changes, the Photo Mode expression"));
        this.CreateIconButton(face, "face:open", "chevron-down", 584.0, 6.0, this.L("APM-Tip-FaceShow", "Show the face controls"));
        this.CreateIconButton(face, "face:close", "chevron-up", 584.0, 6.0, this.L("APM-Tip-FaceHide", "Hide the face controls"));
        let faceBody = new inkCanvas();
        faceBody.SetName(n"APM_FaceBody");
        faceBody.SetAnchor(inkEAnchor.TopLeft);
        faceBody.SetMargin(0.0, 40.0 * s, 0.0, 0.0);
        faceBody.SetSize(width * s, 372.0 * s);
        faceBody.Reparent(face);
        this.m_faceBody = faceBody;
        let zones: array<String> = ["Jaw", "Mouth", "Brows", "Eyelids", "Cheeks"];
        let z = 0;
        while z < ArraySize(zones) {
            this.CreateButton(faceBody, "facezone:" + zones[z], this.LFaceZone(zones[z]), 16.0 + Cast<Float>(z) * 122.0, 0.0, 118.0, 32.0);
            z += 1;
        }
        let controlCount = APM_FaceControlCount();
        let c = 0;
        while c < controlCount {
            // Rows of a zone share the slots below the zone buttons.
            let zone = APM_FaceControlZone(c);
            let slot = 0;
            let k = 0;
            while k < c {
                if Equals(APM_FaceControlZone(k), zone) {
                    slot += 1;
                }
                k += 1;
            }
            let attribute = 7740u + Cast<Uint32>(c);
            if APM_FaceControlIsAngle(c) {
                this.CreateRow(faceBody, attribute, this.LFaceControl(APM_FaceControlLabel(c)), 40.0 + Cast<Float>(slot) * 36.0, -60.0, 60.0, 1.0, 0.2, "");
            } else {
                this.CreateRow(faceBody, attribute, this.LFaceControl(APM_FaceControlLabel(c)), 40.0 + Cast<Float>(slot) * 36.0, -3.0, 3.0, 0.1, 0.004, " cm");
            }
            ArrayPush(this.m_faceRows, this.m_rows[ArraySize(this.m_rows) - 1]);
            c += 1;
        }
        // Saved expressions
        this.CreateButton(faceBody, "facelist:prev", "<", 16.0, 300.0, 40.0, 30.0);
        let pickButton = this.CreateButton(faceBody, "facelist:next", "", 60.0, 300.0, 300.0, 30.0);
        pickButton.label.SetLetterCase(textLetterCase.OriginalCase);
        this.CreateButton(faceBody, "facelist:next", ">", 364.0, 300.0, 40.0, 30.0);
        this.CreateButton(faceBody, "facelist:load", this.L("APM-Button-Load", "Load"), 410.0, 300.0, 104.0, 30.0);
        this.CreateButton(faceBody, "facelist:delete", this.L("APM-Button-Delete", "Delete"), 520.0, 300.0, 104.0, 30.0);
        this.CreateButton(faceBody, "facelist:savenew", this.L("APM-Button-SaveNew", "Save new"), 16.0, 336.0, 300.0, 30.0);
        this.CreateButton(faceBody, "facelist:overwrite", this.L("APM-Button-Overwrite", "Overwrite"), 324.0, 336.0, 300.0, 30.0);

        this.BuildPicker();
        this.BuildHelp();
        this.BuildTooltip(); // last: drawn above the panel, the item list and the help
        this.ReloadPoses();
        this.ReloadExpressions();
        this.Layout();
    }

    // Stacks the visible sections and sizes the panel to them.
    private func Layout() {
        if !IsDefined(this.m_panel) {
            return;
        }
        let s = APM_UiScale();
        let y = 0.0;
        y = this.PlaceSection(this.m_sectionHeader, y, 80.0, true);
        let handTab = this.PresetHand() > 0;
        this.m_handPresets.SetVisible(handTab);
        y = this.PlaceSection(this.m_sectionPart, y, handTab ? 196.0 : 160.0, true);
        y = this.PlaceSection(this.m_sectionRotation, y, 144.0, true);
        y = this.PlaceSection(this.m_sectionMove, y, 144.0, APM_UiIsAttributeEnabled(7705u));
        let pinned = APM_UiGetValue(7711u) > 0.5;
        this.m_ikRows.SetVisible(pinned);
        y = this.PlaceSection(this.m_sectionIk, y, pinned ? 230.0 : 82.0, true);
        this.m_posesBody.SetVisible(this.m_posesOpen);
        y = this.PlaceSection(this.m_sectionPoses, y, this.m_posesOpen ? 218.0 : 40.0, true);
        y = this.PlaceSection(this.m_sectionItems, y, 82.0, true);
        this.m_faceBody.SetVisible(this.m_faceOpen);
        y = this.PlaceSection(this.m_sectionFace, y, this.m_faceOpen ? 412.0 : 40.0, APM_FaceFeatureEnabled());
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

    private func CreateRow(parent: wref<inkCompoundWidget>, attribute: Uint32, text: String, y: Float, minValue: Float, maxValue: Float, step: Float, dragScale: Float, unit: String) {
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
        ArrayPush(row.buttons, this.CreateButton(parent, "zero:" + key, "0", 572.0, y, 52.0, 32.0));
        ArrayPush(this.m_rows, row);
    }

    // --- Face ----------------------------------------------------------------------------------------------------

    private func RefreshFace() {
        let openButton = this.FindButton("face:open");
        let closeButton = this.FindButton("face:close");
        if IsDefined(openButton) && IsDefined(closeButton) {
            openButton.root.SetVisible(!this.m_faceOpen);
            closeButton.root.SetVisible(this.m_faceOpen);
        }
        let loaded = APM_FaceGetLoadedName();
        this.m_faceInfo.SetText(StrLen(loaded) > 0 ? "- " + loaded : APM_Text("APM-Label-PhotoModeExpression", "- Photo Mode expression"));
        let unload = this.FindButton("face:unload");
        if IsDefined(unload) {
            unload.root.SetVisible(StrLen(loaded) > 0);
        }
        let zones: array<String> = ["Jaw", "Mouth", "Brows", "Eyelids", "Cheeks"];
        let z = 0;
        while z < ArraySize(zones) {
            this.SetButton("facezone:" + zones[z], Equals(zones[z], this.m_faceZone), true);
            z += 1;
        }
        let c = 0;
        while c < ArraySize(this.m_faceRows) {
            let row = this.m_faceRows[c];
            let visible = Equals(APM_FaceControlZone(c), this.m_faceZone);
            row.label.SetVisible(visible);
            row.value.root.SetVisible(visible);
            let b = 0;
            while b < ArraySize(row.buttons) {
                row.buttons[b].root.SetVisible(visible);
                b += 1;
            }
            // Changed controls stand out
            row.label.SetTintColor(APM_FaceIsEdited(c) ? APM_ColorAccent() : APM_ColorText());
            c += 1;
        }
        let count = APM_FaceListCount();
        let pick = this.FindButton("facelist:next");
        if IsDefined(pick) {
            pick.label.SetText(count == 0 ? APM_Text("APM-Label-NoExpressions", "No saved expressions") : this.m_faceSelected);
        }
        let hasSelection = count > 0 && StrLen(this.m_faceSelected) > 0;
        this.SetButton("facelist:load", false, hasSelection);
        this.SetButton("facelist:overwrite", false, hasSelection);
        this.SetButton("facelist:delete", false, hasSelection);
        let deleteButton = this.FindButton("facelist:delete");
        if IsDefined(deleteButton) {
            deleteButton.label.SetText(this.m_faceConfirmDelete ? APM_Text("APM-Button-Confirm", "Confirm") : APM_Text("APM-Button-Delete", "Delete"));
            if this.m_faceConfirmDelete {
                deleteButton.label.SetTintColor(APM_ColorWarning());
            }
        }
    }

    private func ReloadExpressions() {
        APM_FaceReloadList();
        let count = APM_FaceListCount();
        let found = false;
        let i = 0;
        while i < count {
            if Equals(APM_FaceListName(i), this.m_faceSelected) {
                found = true;
            }
            i += 1;
        }
        if !found {
            this.m_faceSelected = count > 0 ? APM_FaceListName(0) : "";
        }
    }

    private func StepExpression(direction: Int32) {
        let count = APM_FaceListCount();
        if count == 0 {
            return;
        }
        let index = 0;
        let i = 0;
        while i < count {
            if Equals(APM_FaceListName(i), this.m_faceSelected) {
                index = i;
            }
            i += 1;
        }
        index = (index + direction + count) % count;
        this.m_faceSelected = APM_FaceListName(index);
        this.m_faceConfirmDelete = false;
    }

    private func RunFaceListButton(action: String) {
        if Equals(action, "prev") || Equals(action, "next") {
            this.StepExpression(Equals(action, "prev") ? -1 : 1);
        } else if Equals(action, "load") {
            if !APM_FaceLoad(this.m_faceSelected) {
                this.SetStatus(APM_Text("APM-Msg-ExpressionLoadFailed", "Could not load the expression"));
            }
        } else if Equals(action, "savenew") {
            let name = APM_FaceSaveNew();
            if StrLen(name) == 0 {
                this.SetStatus(APM_Text("APM-Msg-ExpressionSaveFailedFace", "Could not save the expression (is V's face visible?)"));
            } else {
                this.ReloadExpressions();
                this.m_faceSelected = name;
            }
        } else if Equals(action, "overwrite") {
            if !APM_FaceSave(this.m_faceSelected) {
                this.SetStatus(APM_Text("APM-Msg-ExpressionSaveFailed", "Could not save the expression"));
            }
        } else if Equals(action, "delete") {
            if !this.m_faceConfirmDelete {
                this.m_faceConfirmDelete = true;
                this.SetStatus(APM_TextWith("APM-Msg-ClickAgainDelete", "Click again to delete {0}", this.m_faceSelected));
                return;
            }
            APM_FaceDelete(this.m_faceSelected);
            this.ReloadExpressions();
        }
        this.m_faceConfirmDelete = false;
    }

    // --- State -----------------------------------------------------------------------------------------------------

    private func SetOpen(open: Bool) {
        this.m_isOpen = open;
        this.m_panel.SetVisible(open);
        this.m_openButton.root.SetVisible(!open);
        this.m_dragRow = null;
        if open {
            this.ReloadPoses();
            this.ReloadExpressions();
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
        let root = this.m_controller.GetRootCompoundWidget();
        if IsDefined(root) && IsDefined(this.m_layer) {
            root.ReorderChild(this.m_layer, 0);
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
                // "Click again" for Reset all / Delete lasts as long as its message.
                if this.m_confirmResetAll || this.m_confirmDelete {
                    this.m_confirmResetAll = false;
                    this.m_confirmDelete = false;
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

    private func ReloadPoses() {
        ArrayClear(this.m_poseNames);
        let count = APM_GetPoseCount();
        let i = 0;
        while i < count {
            ArrayPush(this.m_poseNames, APM_GetPoseName(i));
            i += 1;
        }
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
            APM_SetHandProp(right, hasPose ? 1ul : 0ul);
            return;
        }
        if Equals(action, "restore") {
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
        transactions.RemoveItemFromSlot(puppet, slot, !isPoseItem);
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
        }
        let placed = transactions.AddItemToSlot(puppet, slot, itemID, true, null, ERenderingPlane.RPl_Scene, false, true);
        APM_LogInfo("Items: " + GetLocalizedItemNameByCName(itemRecord.DisplayName()) + (placed ? " placed in the " : " could not be placed in the ") + (right ? "right" : "left") + " hand");
        this.m_checkTimer = 3.0;
        this.m_checkRight = right;
        this.m_checkRecord = record;
        return true;
    }

    // --- Pose names ----------------------------------------------------------------------------------------------

    private func StartRename(name: String) {
        if this.m_pickerOpen {
            this.ClosePicker();
        }
        if !APM_TextEntryBegin(name) {
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
        if !confirm || Equals(text, this.m_renameFrom) {
            return;
        }
        let renamed = APM_RenamePose(this.m_renameFrom, text);
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
                row.value.label.SetText(this.FormatValue(row, APM_UiGetValue(row.attribute)));
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
            resetAllButton.tooltip = this.m_confirmResetAll ? APM_Text("APM-Tip-ResetAllConfirm", "Click again to reset everything") : APM_Text("APM-Tip-ResetAll", "Reset everything (click twice) - items in hand stay");
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
                let isRenamed = this.m_renaming && Equals(this.m_poseNames[index], this.m_renameFrom);
                button.label.SetText(isRenamed ? APM_TextEntryGetText() + "_" : this.m_poseNames[index]);
                button.isSelected = Equals(this.m_poseNames[index], this.m_selectedPose);
                button.isMarked = this.m_renaming && Equals(this.m_poseNames[index], this.m_renameFrom);
                button.UpdateLook();
            } else {
                button.root.SetVisible(false);
            }
            i += 1;
        }
        let base = APM_GetBasePoseName();
        this.m_posesTitle.SetText(APM_Text("APM-Title-Poses", "Poses") + " " + IntToString(this.m_posePage + 1) + "/" + IntToString(this.PageCount())
            + (StrLen(base) > 0 ? "  - " + base : ""));
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
        this.SetButton("overwrite", false, hasSelection);
        this.SetButton("load", false, hasSelection);
        this.RefreshProps();
        this.RefreshFace();
        this.SetButton("mirror", APM_IsMirror(), true);
        this.SetButton("undo", false, APM_CanUndo());
        this.SetButton("redo", false, APM_CanRedo());
        this.SetButton("rename", this.m_renaming, hasSelection);
        this.SetButton("delete", false, hasSelection);
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
        // Closest points of the two lines of sight.
        let wx = left.X - right.X;
        let wy = left.Y - right.Y;
        let wz = left.Z - right.Z;
        let b = gl.X * gr.X + gl.Y * gr.Y + gl.Z * gr.Z;
        let d = gl.X * wx + gl.Y * wy + gl.Z * wz;
        let e = gr.X * wx + gr.Y * wy + gr.Z * wz;
        let denom = 1.0 - b * b;
        if denom > 0.00001 {
            let sl = (b * e - d) / denom;
            let sr = (e - b * d) / denom;
            if sl > 0.15 && sr > 0.15 && sl < 4.0 && sr < 4.0 {
                return APM_Vector((left.X + gl.X * sl + right.X + gr.X * sr) * 0.5, (left.Y + gl.Y * sl + right.Y + gr.Y * sr) * 0.5, (left.Z + gl.Z * sl + right.Z + gr.Z * sr) * 0.5, 1.0);
            }
        }
        let distance = this.LookDistance();
        return APM_Vector((left.X + right.X) * 0.5 + (gl.X + gr.X) * 0.5 * distance, (left.Y + right.Y) * 0.5 + (gl.Y + gr.Y) * 0.5 * distance, (left.Z + right.Z) * 0.5 + (gl.Z + gr.Z) * 0.5 * distance, 1.0);
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
                let eyeDistance = this.LookDistance();
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
                let isDetail = (marker.kind == APM_MarkerKindFingerDetail() || marker.kind == APM_MarkerKindFinger()) && !marker.isProp;
                let size = isDetail ? 7.0 : 10.0;
                if isSelected || marker.isHovered {
                    size += 4.0;
                }
                marker.dot.SetTintColor(color);
                if IsDefined(marker.halo) {
                    marker.halo.SetVisible(marker.chain >= 0 && APM_GizmoIsIkActive(marker.chain));
                }
                marker.root.SetOpacity(locked ? 0.5 : (isSelected || marker.isHovered ? 1.0 : 0.75));
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
        // Choosing "Eyes (both)" or a single eye in the Head tab switches the eye markers.
        if selected == this.m_leftEyePart || selected == this.m_rightEyePart {
            this.SetEyesTogether(false);
        } else if Equals(APM_UiGetOptionLabel(7701u, selected), "Eyes (both)") {
            this.SetEyesTogether(true);
        }
        this.UpdateRootMarker(camera, toWorld, selected);
        this.UpdateHint();
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
        if !IsDefined(this.m_dragMarker) && !this.m_shiftDown && !this.m_controlDown && !APM_IsAltDown() && marker.kind != APM_MarkerKindEyeLook() {
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
    private func UpdateMarkerLines(camera: ref<CameraSystem>, toWorld: Matrix, marker: ref<APM_Marker>, world: Vector4, color: HDRColor, strong: Bool) -> Float {
        let s = APM_UiScale();
        let thickness = (strong ? 2.5 : 1.5) * s;
        let opacity = strong ? 0.9 : 0.45;
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
        this.DrawWorldLine(camera, marker.parentLine, APM_Vector(parent.X, parent.Y, parent.Z, parentModel.W), world, APM_ColorText(), 1.5 * s, 0.35);

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

    // Perspective projection of a world point to the 3840x2160 ink canvas (X, Y); W = 1 when on screen.
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

    // Degrees of rotation per canvas unit dragged (the ink canvas is 3840 units wide).
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
    private func DragModeFor(marker: ref<APM_Marker>, shift: Bool, control: Bool) -> Int32 {
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
            let look = this.IsSingleEye(marker.part) ? this.SingleEyePoint(toWorld, marker.part, this.LookDistance()) : this.EyeLookPoint(toWorld);
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
        let value = ClampF(APM_UiGetValue(row.attribute) + row.step * direction, row.minValue, row.maxValue);
        APM_UiSetValue(row.attribute, value);
    }

    private func RunButton(button: ref<APM_Button>) {
        let id = button.id;
        let prefix = StrBeforeFirst(id, ":");
        let arg = StrAfterFirst(id, ":");
        let keepResetConfirm = false;
        let keepDeleteConfirm = false;

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
                if Equals(prefix, "zero") && row.attribute >= 7740u && row.attribute < 7800u {
                    APM_FaceResetControl(Cast<Int32>(row.attribute) - 7740); // back to the expression's own value
                } else if Equals(prefix, "zero") {
                    APM_UiSetValue(row.attribute, 0.0);
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
        } else if Equals(id, "face:open") || Equals(id, "face:close") {
            this.m_faceOpen = Equals(id, "face:open");
            APM_SessionSet("FaceOpen", this.m_faceOpen ? 1.0 : 0.0);
            if this.m_faceOpen {
                this.ReloadExpressions();
            }
        } else if Equals(id, "face:folder") {
            APM_OpenFolder(1);
        } else if Equals(id, "poses:folder") {
            APM_OpenFolder(0);
        } else if Equals(id, "face:reset") {
            APM_FaceReset();
        } else if Equals(id, "face:unload") {
            APM_FaceUnload();
        } else if Equals(prefix, "facezone") {
            this.m_faceZone = arg;
        } else if Equals(prefix, "facelist") {
            this.RunFaceListButton(arg);
        } else if Equals(id, "undo") {
            APM_Undo();
        } else if Equals(id, "redo") {
            APM_Redo();
        } else if Equals(prefix, "preset") {
            let presetHand = this.PresetHand();
            if presetHand > 0 {
                APM_HistoryCheckpoint();
                APM_ApplyHandPreset(presetHand == 2, StringToInt(arg));
            }
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
        } else if Equals(prefix, "neutral") {
            let armsDown = Equals(arg, "a");
            if !APM_SetNeutralPose(armsDown) {
                this.SetStatus(APM_Text("APM-Msg-NeutralUnavailable", "Neutral pose not available"));
            }
        } else if Equals(id, "partreset") {
            this.ResetPart(Cast<Int32>(APM_UiGetValue(7701u)));
        } else if Equals(id, "resetall") {
            if this.m_confirmResetAll {
                APM_UiSetValue(7722u, 1.0);
            } else {
                keepResetConfirm = true;
                this.SetStatus(APM_Text("APM-Msg-ResetAllConfirm", "Click again to reset everything (items in hand stay)"));
            }
        } else if Equals(prefix, "pose") {
            let index = this.m_posePage * APM_PosesPerPage() + StringToInt(arg);
            if index >= 0 && index < ArraySize(this.m_poseNames) {
                this.m_selectedPose = this.m_poseNames[index];
            }
        } else if Equals(id, "savenew") {
            let name = APM_SaveNewPose();
            this.ReloadPoses();
            if StrLen(name) > 0 {
                this.m_selectedPose = name;
                this.ShowPose(name);
                this.StartRename(name); // type a name right away (Esc keeps the default one)
            } else {
                this.SetStatus(APM_Text("APM-Msg-PoseSaveFailed", "Could not save the pose"));
            }
        } else if Equals(id, "overwrite") {
            if StrLen(this.m_selectedPose) > 0 {
                APM_SavePose(this.m_selectedPose);
            }
        } else if Equals(id, "rename") {
            if StrLen(this.m_selectedPose) > 0 {
                this.StartRename(this.m_selectedPose);
            }
        } else if Equals(id, "load") {
            if StrLen(this.m_selectedPose) > 0 {
                let loaded = APM_LoadPose(this.m_selectedPose);
                if !loaded {
                    this.SetStatus(APM_TextWith("APM-Msg-PoseLoadFailed", "Could not load {0}", this.m_selectedPose));
                }
            }
        } else if Equals(id, "delete") {
            if StrLen(this.m_selectedPose) > 0 {
                if this.m_confirmDelete {
                    APM_DeletePose(this.m_selectedPose);
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
        APM_UiRequestRefresh(); // keeps the Photo Mode menu items in sync
        this.Refresh();
    }

    // --- Characters -----------------------------------------------------------------------------------------------

    private func CharacterName(character: Int32) -> String {
        if character == 0 {
            return "V";
        }
        let puppet = APM_CharacterGetEntity(character) as GameObject;
        let name = IsDefined(puppet) ? puppet.GetDisplayName() : "";
        if StrLen(name) == 0 || StrBeginsWith(name, "LocKey") {
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

    private func LFaceZone(zone: String) -> String {
        return this.L("APM-FaceZone-" + APM_KeyName(zone), zone);
    }

    private func LFaceControl(label: String) -> String {
        return this.L("APM-FaceControl-" + APM_KeyName(label), label);
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
        if IsDefined(this.m_dragMarker) || IsDefined(this.m_dragRow) {
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
        if Equals(StrBeforeFirst(button.id, ":"), "drag") {
            let row = this.FindRow(Cast<Uint32>(StringToInt(StrAfterFirst(button.id, ":"))));
            if IsDefined(row) {
                APM_HistoryCheckpoint();
                this.m_dragRow = row;
                this.m_dragValue = APM_UiGetValue(row.attribute);
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
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
        if !IsDefined(marker) || !marker.isHovered {
            return false; // hover not taken (camera moving, UI hidden): the click belongs to the camera
        }
        APM_UiSetValue(7701u, Cast<Float>(marker.part));
        if APM_GizmoIsPartLocked(marker.part) {
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
        APM_UiSetValue(row.attribute, this.m_dragValue);
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
        if IsDefined(this.m_dragRow) || IsDefined(this.m_dragMarker) {
            this.m_dragRow = null;
            this.m_dragMarker = null;
            this.m_dragMode = 0;
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
        APM_OnPhotoModeExit();
    }
    return wrappedMethod();
}

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnExitConfirm(data: ref<inkGameNotificationData>) -> Bool {
    let resultData = data as GenericMessageNotificationCloseData;
    if IsDefined(resultData) && Equals(resultData.result, GenericMessageNotificationResult.Confirm) {
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
