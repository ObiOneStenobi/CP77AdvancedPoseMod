// AdvancedPoseMod - pose editor panel and gizmos shown over Photo Mode.
//
// A self-made ink panel (no external UI library): body part tabs, value rows that can be dragged with the mouse or
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
    return 984.0;
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
    public let group: String;
    public let root: wref<inkCanvas>;
    public let dot: wref<inkRectangle>;
    public let isHovered: Bool;
}

// World position of a body part (w = 1 when valid): V's world transform applied to the model-space bone position.
public func APM_PartWorldPosition(entity: ref<Entity>, part: Int32) -> Vector4 {
    let model = APM_GizmoGetPartPosition(part);
    if !IsDefined(entity) || model.W < 0.5 {
        return APM_Vector(0.0, 0.0, 0.0, 0.0);
    }
    let origin = entity.GetWorldPosition();
    let offset = Quaternion.Transform(entity.GetWorldOrientation(), APM_Vector(model.X, model.Y, model.Z, 0.0));
    return APM_Vector(origin.X + offset.X, origin.Y + offset.Y, origin.Z + offset.Z, 1.0);
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
    private let m_dockRight: Bool;
    private let m_opacityStep: Int32; // 0..3
    private let m_gizmosOn: Bool;
    private let m_panelHovered: Bool;

    private let m_buttons: array<ref<APM_Button>>;
    private let m_rows: array<ref<APM_ValueRow>>;
    private let m_groupNames: array<String>;
    private let m_groupButtons: array<ref<APM_Button>>;
    private let m_partButtons: array<ref<APM_Button>>;
    private let m_partIndices: array<Int32>; // body part index shown by each part button (-1 = unused)
    private let m_poseButtons: array<ref<APM_Button>>;
    private let m_poseNames: array<String>;
    private let m_posePage: Int32;
    private let m_selectedPose: String;
    private let m_partTitle: wref<inkText>;
    private let m_moveTitle: wref<inkText>;
    private let m_posesTitle: wref<inkText>;
    private let m_status: wref<inkText>;

    private let m_markers: array<ref<APM_Marker>>;
    private let m_dragMarker: ref<APM_Marker>;

    private let m_dragRow: ref<APM_ValueRow>;
    private let m_dragValue: Float;
    private let m_confirmResetAll: Bool;
    private let m_confirmDelete: Bool;
    private let m_refreshTimer: Float;

    public func Initialize(controller: ref<gameuiPhotoModeMenuController>) {
        this.m_controller = controller;
        let root = controller.GetRootCompoundWidget();
        if !IsDefined(root) {
            return;
        }

        this.m_dockRight = APM_PrefGet("DockRight", 0.0) > 0.5;
        this.m_opacityStep = Cast<Int32>(APM_PrefGet("OpacityStep", 1.0));
        this.m_gizmosOn = APM_PrefGet("Gizmos", 1.0) > 0.5;

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

        this.m_openButton = this.CreateButton(layer, "open", "Pose editor", 0.0, 0.0, 160.0, 32.0);
        this.BuildPanel();
        this.SetOpen(false);
        this.ApplyDock();
        this.ApplyOpacity();

        controller.RegisterToGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
        controller.RegisterToGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
    }

    public func Uninitialize() {
        if IsDefined(this.m_controller) {
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
        }
        this.m_dragRow = null;
        this.m_dragMarker = null;
    }

    public func Update(timeDelta: Float) {
        this.UpdateMarkers();
        if !this.m_isOpen {
            return;
        }
        this.m_refreshTimer -= timeDelta;
        if this.m_refreshTimer <= 0.0 && !IsDefined(this.m_dragRow) {
            this.m_refreshTimer = 0.25;
            this.Refresh(); // picks up changes made through the Photo Mode menu and the gizmos
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

        // Header
        this.CreateText(panel, "Advanced pose", 16.0, 8.0, 200.0, 32.0, 22, APM_ColorAccent());
        this.CreateButton(panel, "enabled", "Pose off", 214.0, 8.0, 96.0, 32.0);
        this.CreateButton(panel, "gizmos", "Gizmos", 316.0, 8.0, 88.0, 32.0);
        this.CreateButton(panel, "opacity", "100%", 410.0, 8.0, 64.0, 32.0);
        this.CreateButton(panel, "dock", "<  >", 480.0, 8.0, 56.0, 32.0);
        this.CreateButton(panel, "hide", "Hide", 542.0, 8.0, 82.0, 32.0);

        // Body part groups (two rows of four tabs)
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
            let y = 48.0 + Cast<Float>(i / 4) * 38.0;
            ArrayPush(this.m_groupButtons, this.CreateButton(panel, "group:" + IntToString(i), this.m_groupNames[i], x, y, 146.0, 32.0));
            i += 1;
        }

        // Body parts of the selected group (up to six, two rows of three)
        i = 0;
        while i < 6 {
            let x = 16.0 + Cast<Float>(i % 3) * 203.0;
            let y = 130.0 + Cast<Float>(i / 3) * 38.0;
            ArrayPush(this.m_partButtons, this.CreateButton(panel, "part:" + IntToString(i), "", x, y, 197.0, 32.0));
            ArrayPush(this.m_partIndices, -1);
            i += 1;
        }

        // Rotation
        this.m_partTitle = this.CreateText(panel, "", 16.0, 208.0, 600.0, 26.0, 19, APM_ColorAccent());
        this.CreateRow(panel, 7702u, "Bend forward / back", 236.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(panel, 7703u, "Bend sideways", 272.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(panel, 7704u, "Twist", 308.0, -180.0, 180.0, 1.0, 0.2, "");

        // Translation (body only)
        this.m_moveTitle = this.CreateText(panel, "Move (cm)", 16.0, 346.0, 600.0, 26.0, 19, APM_ColorAccent());
        this.CreateRow(panel, 7705u, "Left / right", 374.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7706u, "Forward / back", 410.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7707u, "Up / down", 446.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateButton(panel, "resetpart", "Reset part", 16.0, 486.0, 296.0, 32.0);
        this.CreateButton(panel, "resetall", "Reset all", 328.0, 486.0, 296.0, 32.0);

        // IK
        this.CreateText(panel, "IK: pin a hand or foot", 16.0, 526.0, 600.0, 26.0, 19, APM_ColorAccent());
        let limbs = APM_UiGetOptionCount(7710u);
        i = 0;
        while i < limbs {
            this.CreateButton(panel, "ik:" + IntToString(i), APM_UiGetOptionLabel(7710u, i), 16.0 + Cast<Float>(i) * 124.0, 556.0, 118.0, 32.0);
            i += 1;
        }
        this.CreateButton(panel, "pin", "Pin off", 512.0, 556.0, 112.0, 32.0);
        this.CreateRow(panel, 7712u, "Right / left", 594.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7713u, "Forward / back", 630.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7714u, "Up / down", 666.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7715u, "Elbow / knee", 702.0, -180.0, 180.0, 1.0, 0.2, "");

        // Pose library
        this.m_posesTitle = this.CreateText(panel, "Poses", 16.0, 744.0, 600.0, 26.0, 19, APM_ColorAccent());
        i = 0;
        while i < APM_PosesPerPage() {
            ArrayPush(this.m_poseButtons, this.CreateButton(panel, "pose:" + IntToString(i), "", 16.0, 774.0 + Cast<Float>(i) * 34.0, 420.0, 30.0));
            i += 1;
        }
        this.CreateButton(panel, "savenew", "Save new", 446.0, 774.0, 178.0, 30.0);
        this.CreateButton(panel, "overwrite", "Overwrite", 446.0, 808.0, 178.0, 30.0);
        this.CreateButton(panel, "load", "Load", 446.0, 842.0, 178.0, 30.0);
        this.CreateButton(panel, "delete", "Delete", 446.0, 876.0, 178.0, 30.0);
        this.CreateButton(panel, "pageprev", "<", 446.0, 910.0, 86.0, 30.0);
        this.CreateButton(panel, "pagenext", ">", 538.0, 910.0, 86.0, 30.0);
        this.m_status = this.CreateText(panel, "Drag a value or a gizmo with the mouse", 16.0, 948.0, 608.0, 26.0, 15, APM_ColorDimText());

        this.ReloadPoses();
    }

    private func BuildMarkers() {
        let s = APM_UiScale();
        let count = APM_UiGetOptionCount(7701u);
        let i = 0;
        while i < count {
            let label = APM_UiGetOptionLabel(7701u, i);
            // "Eyes (both)" sits on the left eye: keep only the single eyes on screen.
            if !Equals(label, "Eyes (both)") {
                let root = new inkCanvas();
                root.SetName(StringToName("APM_gizmo:" + IntToString(i)));
                root.SetAnchor(inkEAnchor.TopLeft);
                root.SetAnchorPoint(0.5, 0.5);
                root.SetSize(22.0 * s, 22.0 * s);
                root.SetInteractive(true);
                root.Reparent(this.m_gizmoLayer);

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
                marker.group = APM_UiGetOptionGroup(7701u, i);
                marker.root = root;
                marker.dot = dot;
                ArrayPush(this.m_markers, marker);
            }
            i += 1;
        }
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
        label.SetText(text);
        label.Reparent(parent);
        return label;
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
        label.SetText(text);
        label.Reparent(root);

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
        ArrayPush(row.buttons, this.CreateButton(parent, "inc:" + key, "+", 518.0, y, 48.0, 32.0));
        ArrayPush(row.buttons, this.CreateButton(parent, "zero:" + key, "0", 572.0, y, 52.0, 32.0));
        ArrayPush(this.m_rows, row);
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
            this.m_panelHovered = false;
            this.SetHover(false);
        }
    }

    private func ApplyDock() {
        let s = APM_UiScale();
        if this.m_dockRight {
            this.m_panel.SetAnchor(inkEAnchor.TopRight);
            this.m_panel.SetAnchorPoint(1.0, 0.0);
            this.m_panel.SetMargin(0.0, 60.0 * s, 24.0 * s, 0.0);
            this.m_openButton.root.SetAnchor(inkEAnchor.TopRight);
            this.m_openButton.root.SetAnchorPoint(1.0, 0.0);
            this.m_openButton.root.SetMargin(0.0, 60.0 * s, 24.0 * s, 0.0);
        } else {
            this.m_panel.SetAnchor(inkEAnchor.TopLeft);
            this.m_panel.SetAnchorPoint(0.0, 0.0);
            this.m_panel.SetMargin(24.0 * s, 60.0 * s, 0.0, 0.0);
            this.m_openButton.root.SetAnchor(inkEAnchor.TopLeft);
            this.m_openButton.root.SetAnchorPoint(0.0, 0.0);
            this.m_openButton.root.SetMargin(24.0 * s, 60.0 * s, 0.0, 0.0);
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

    private func SetHover(hover: Bool) {
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
        let text = FloatToStringPrec(value, 1);
        if Equals(text, "-0.0") {
            text = "0.0";
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

    private func SetStatus(text: String) {
        if IsDefined(this.m_status) {
            this.m_status.SetText(text);
        }
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

    // --- Refresh -------------------------------------------------------------------------------------------------

    public func Refresh() {
        if !IsDefined(this.m_panel) {
            return;
        }

        // Header
        let enabled = APM_UiGetValue(7700u) > 0.5;
        let enabledButton = this.FindButton("enabled");
        if IsDefined(enabledButton) {
            enabledButton.label.SetText(enabled ? "Pose on" : "Pose off");
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
            if Equals(APM_UiGetOptionGroup(7701u, i), group) {
                this.m_partIndices[slot] = i;
                let button = this.m_partButtons[slot];
                button.root.SetVisible(true);
                button.label.SetText(APM_ShortPartLabel(APM_UiGetOptionLabel(7701u, i)));
                button.isSelected = i == part;
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
        this.m_partTitle.SetText(APM_UiGetOptionLabel(7701u, part));

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
        this.m_moveTitle.SetOpacity(APM_UiIsAttributeEnabled(7705u) ? 1.0 : 0.35);
        let resetAllButton = this.FindButton("resetall");
        if IsDefined(resetAllButton) {
            resetAllButton.label.SetText(this.m_confirmResetAll ? "Click again to reset all" : "Reset all");
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
            i += 1;
        }
        let pinned = APM_UiGetValue(7711u) > 0.5;
        let pinButton = this.FindButton("pin");
        if IsDefined(pinButton) {
            pinButton.label.SetText(pinned ? "Pin on" : "Pin off");
        }
        this.SetButton("pin", pinned, true);

        // Pose library
        let first = this.m_posePage * APM_PosesPerPage();
        i = 0;
        while i < ArraySize(this.m_poseButtons) {
            let button = this.m_poseButtons[i];
            let index = first + i;
            if index < ArraySize(this.m_poseNames) {
                button.root.SetVisible(true);
                button.label.SetText(this.m_poseNames[index]);
                button.isSelected = Equals(this.m_poseNames[index], this.m_selectedPose);
                button.UpdateLook();
            } else {
                button.root.SetVisible(false);
            }
            i += 1;
        }
        this.m_posesTitle.SetText("Poses (" + IntToString(ArraySize(this.m_poseNames)) + ")   page " + IntToString(this.m_posePage + 1) + " / " + IntToString(this.PageCount()));
        let hasSelection = StrLen(this.m_selectedPose) > 0;
        this.SetButton("overwrite", false, hasSelection);
        this.SetButton("load", false, hasSelection);
        this.SetButton("delete", false, hasSelection);
        let deleteButton = this.FindButton("delete");
        if IsDefined(deleteButton) {
            deleteButton.label.SetText(this.m_confirmDelete ? "Confirm delete" : "Delete");
            if this.m_confirmDelete {
                deleteButton.label.SetTintColor(APM_ColorWarning());
            }
        }
        this.SetButton("pageprev", false, this.m_posePage > 0);
        this.SetButton("pagenext", false, this.m_posePage < this.PageCount() - 1);
    }

    // --- Gizmos --------------------------------------------------------------------------------------------------

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
        let cameraTransform: Transform;
        let hasCamera = camera.GetActiveCameraWorldTransform(cameraTransform);
        let forward = camera.GetActiveCameraForward();
        let entity = APM_GizmoGetEntity();
        if !IsDefined(entity) {
            this.m_gizmoLayer.SetVisible(false);
            return;
        }
        let selected = Cast<Int32>(APM_UiGetValue(7701u));
        let selectedGroup = APM_UiGetOptionGroup(7701u, selected);
        let s = APM_UiScale();

        let i = 0;
        while i < ArraySize(this.m_markers) {
            let marker = this.m_markers[i];
            let show = true;
            // Fingers only when a hand group is being edited, to keep the hand readable.
            if APM_IsHandGroup(marker.group) && !Equals(marker.group, selectedGroup) {
                show = false;
            }
            let world = APM_PartWorldPosition(entity, marker.part);
            if world.W < 0.5 {
                show = false;
            }
            if show && hasCamera {
                let toPoint = APM_Vector(world.X - cameraTransform.position.X, world.Y - cameraTransform.position.Y, world.Z - cameraTransform.position.Z, 0.0);
                if toPoint.X * forward.X + toPoint.Y * forward.Y + toPoint.Z * forward.Z <= 0.05 {
                    show = false; // behind the camera
                }
            }
            if show {
                let projected = camera.ProjectPoint(APM_Vector(world.X, world.Y, world.Z, 1.0));
                if AbsF(projected.X) > 1.05 || AbsF(projected.Y) > 1.05 {
                    show = false;
                } else {
                    let x = (projected.X + 1.0) * 0.5 * APM_ScreenWidth();
                    let y = (1.0 - projected.Y) * 0.5 * APM_ScreenHeight();
                    marker.root.SetMargin(x, y, 0.0, 0.0);
                }
            }
            marker.root.SetVisible(show);
            if show {
                let isSelected = marker.part == selected;
                let size = isSelected || marker.isHovered ? 14.0 : 10.0;
                marker.dot.SetSize(size * s, size * s);
                if isSelected {
                    marker.dot.SetTintColor(APM_ColorAccent());
                } else if marker.chain >= 0 {
                    marker.dot.SetTintColor(APM_ColorIk());
                } else {
                    marker.dot.SetTintColor(APM_ColorText());
                }
                marker.root.SetOpacity(isSelected || marker.isHovered ? 1.0 : 0.75);
            }
            i += 1;
        }
    }

    // Meters of movement per unit of mouse movement, growing with the distance from the camera.
    private func DragMetersPerUnit(part: Int32) -> Float {
        let player = this.m_controller.GetPlayerControlledObject();
        if !IsDefined(player) {
            return 0.002;
        }
        let camera = GameInstance.GetCameraSystem(player.GetGame());
        let cameraTransform: Transform;
        let world = APM_PartWorldPosition(APM_GizmoGetEntity(), part);
        if world.W < 0.5 || !camera.GetActiveCameraWorldTransform(cameraTransform) {
            return 0.002;
        }
        let distance = Vector4.Distance(APM_Vector(world.X, world.Y, world.Z, 0.0), APM_Vector(cameraTransform.position.X, cameraTransform.position.Y, cameraTransform.position.Z, 0.0));
        return 0.0012 * MaxF(distance, 0.3);
    }

    private func DragMarker(marker: ref<APM_Marker>, horizontal: Bool, amount: Float) {
        if marker.chain >= 0 {
            // Move the hand / foot in the camera plane.
            let player = this.m_controller.GetPlayerControlledObject();
            if !IsDefined(player) {
                return;
            }
            let camera = GameInstance.GetCameraSystem(player.GetGame());
            let axis = horizontal ? camera.GetActiveCameraRight() : camera.GetActiveCameraUp();
            let entity = APM_GizmoGetEntity();
            if !IsDefined(entity) {
                return;
            }
            let meters = amount * this.DragMetersPerUnit(marker.part);
            let worldDelta = APM_Vector(axis.X * meters, axis.Y * meters, axis.Z * meters, 0.0);
            let modelDelta = Quaternion.Transform(Quaternion.Conjugate(entity.GetWorldOrientation()), worldDelta);
            APM_GizmoMoveIk(marker.chain, modelDelta);
        } else {
            // Rotate the part: left/right bends sideways, up/down bends forward/back.
            let attribute = horizontal ? 7703u : 7702u;
            let value = ClampF(APM_UiGetValue(attribute) + amount * 0.3, -180.0, 180.0);
            APM_UiSetValue(attribute, value);
        }
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
        if Equals(id, "hide") {
            this.SetOpen(false);
            return;
        }
        if Equals(id, "dock") {
            this.m_dockRight = !this.m_dockRight;
            APM_PrefSet("DockRight", this.m_dockRight ? 1.0 : 0.0);
            this.ApplyDock();
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
            let row = this.FindRow(Cast<Uint32>(StringToInt(arg)));
            if IsDefined(row) {
                if Equals(prefix, "zero") {
                    APM_UiSetValue(row.attribute, 0.0);
                } else {
                    this.StepRow(row, Equals(prefix, "inc") ? 1.0 : -1.0);
                }
            }
        } else if Equals(prefix, "ik") {
            APM_UiSetValue(7710u, Cast<Float>(StringToInt(arg)));
        } else if Equals(id, "pin") {
            APM_UiSetValue(7711u, APM_UiGetValue(7711u) > 0.5 ? 0.0 : 1.0);
        } else if Equals(id, "resetpart") {
            APM_UiSetValue(7721u, 1.0);
        } else if Equals(id, "resetall") {
            if this.m_confirmResetAll {
                APM_UiSetValue(7722u, 1.0);
                this.SetStatus("All edits removed");
            } else {
                keepResetConfirm = true;
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
                this.SetStatus("Saved as " + name);
            } else {
                this.SetStatus("Could not save the pose");
            }
        } else if Equals(id, "overwrite") {
            if StrLen(this.m_selectedPose) > 0 {
                APM_SavePose(this.m_selectedPose);
                this.SetStatus("Saved over " + this.m_selectedPose);
            }
        } else if Equals(id, "load") {
            if StrLen(this.m_selectedPose) > 0 {
                let loaded = APM_LoadPose(this.m_selectedPose);
                this.SetStatus(loaded ? "Loaded " + this.m_selectedPose : "Could not load " + this.m_selectedPose);
            }
        } else if Equals(id, "delete") {
            if StrLen(this.m_selectedPose) > 0 {
                if this.m_confirmDelete {
                    APM_DeletePose(this.m_selectedPose);
                    this.SetStatus("Deleted " + this.m_selectedPose);
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

    // --- Callbacks -----------------------------------------------------------------------------------------------

    protected cb func OnPanelHoverOver(e: ref<inkPointerEvent>) -> Bool {
        this.m_panelHovered = true;
        this.SetHover(true);
    }

    protected cb func OnPanelHoverOut(e: ref<inkPointerEvent>) -> Bool {
        this.m_panelHovered = false;
        if !IsDefined(this.m_dragRow) && !IsDefined(this.m_dragMarker) {
            this.SetHover(false);
        }
    }

    protected cb func OnButtonHoverOver(e: ref<inkPointerEvent>) -> Bool {
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if IsDefined(button) {
            button.isHovered = true;
            button.UpdateLook();
        }
        this.SetHover(true);
    }

    protected cb func OnButtonHoverOut(e: ref<inkPointerEvent>) -> Bool {
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if IsDefined(button) {
            button.isHovered = false;
            button.UpdateLook();
            if Equals(button.id, "open") {
                this.SetHover(false); // the open button lives outside the panel
            }
        }
    }

    protected cb func OnButtonPress(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") {
            return false;
        }
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if !IsDefined(button) || !button.isEnabled {
            return false;
        }
        if Equals(StrBeforeFirst(button.id, ":"), "drag") {
            let row = this.FindRow(Cast<Uint32>(StringToInt(StrAfterFirst(button.id, ":"))));
            if IsDefined(row) {
                this.m_dragRow = row;
                this.m_dragValue = APM_UiGetValue(row.attribute);
                this.SetHover(true);
            }
        }
    }

    protected cb func OnButtonRelease(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") {
            return false;
        }
        let button = this.FindButtonByWidget(e.GetCurrentTarget());
        if !IsDefined(button) || !button.isEnabled {
            return false;
        }
        if Equals(StrBeforeFirst(button.id, ":"), "drag") {
            return false; // handled by the drag logic
        }
        this.RunButton(button);
    }

    protected cb func OnMarkerHoverOver(e: ref<inkPointerEvent>) -> Bool {
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
        if IsDefined(marker) {
            marker.isHovered = true;
            this.SetStatus(APM_UiGetOptionLabel(7701u, marker.part) + (marker.chain >= 0 ? ": drag to move (IK)" : ": drag to rotate"));
        }
        this.SetHover(true);
    }

    protected cb func OnMarkerHoverOut(e: ref<inkPointerEvent>) -> Bool {
        let marker = this.FindMarkerByWidget(e.GetCurrentTarget());
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
        if !IsDefined(marker) {
            return false;
        }
        APM_UiSetValue(7701u, Cast<Float>(marker.part));
        if marker.chain >= 0 {
            APM_UiSetValue(7710u, Cast<Float>(marker.chain));
        }
        this.m_dragMarker = marker;
        this.SetHover(true);
        APM_UiRequestRefresh();
        this.Refresh();
    }

    protected cb func OnGlobalRelative(e: ref<inkPointerEvent>) -> Bool {
        let horizontal = e.IsAction(n"mouse_x");
        if !horizontal && !e.IsAction(n"mouse_y") {
            return false;
        }

        if IsDefined(this.m_dragMarker) {
            this.DragMarker(this.m_dragMarker, horizontal, e.GetAxisData());
            return false;
        }

        let row = this.m_dragRow;
        if !IsDefined(row) || !horizontal {
            return false;
        }
        this.m_dragValue = ClampF(this.m_dragValue + e.GetAxisData() * row.dragScale, row.minValue, row.maxValue);
        APM_UiSetValue(row.attribute, this.m_dragValue);
        row.value.label.SetText(this.FormatValue(row, this.m_dragValue));
    }

    protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
        if !e.IsAction(n"click") {
            return false;
        }
        if IsDefined(this.m_dragRow) || IsDefined(this.m_dragMarker) {
            this.m_dragRow = null;
            this.m_dragMarker = null;
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
@addMethod(gameuiPhotoModeMenuController)
public final func APM_SetHover(hover: Bool) {
    if Equals(hover, this.m_IsHoverOver) {
        return;
    }
    this.m_IsHoverOver = hover;
    this.OnHoverStateChanged(hover);
    this.SetCursorContext(hover ? n"Hover" : n"Default");
}

@addMethod(gameuiPhotoModeMenuController)
public final func APM_UpdatePoseEditor(timeDelta: Float) {
    if IsDefined(this.m_apmPoseEditor) {
        this.m_apmPoseEditor.Update(timeDelta);
    }
}
