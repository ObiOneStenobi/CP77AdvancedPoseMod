// AdvancedPoseMod - pose editor panel shown over Photo Mode.
//
// A self-made ink panel (no external UI library): body part tabs, value rows that can be dragged with the mouse or
// stepped with -/+ buttons, IK controls and pose slots. It drives the same plugin state as the Photo Mode menu items
// (APM_UiSetValue / APM_UiGetValue with the 77xx attribute keys), so both stay in sync.

module AdvancedPoseMod.Ui

// --- Constants ----------------------------------------------------------------------------------------------------

// Layout is designed in 1920x1080 units and scaled to the 3840x2160 ink canvas.
public func APM_UiScale() -> Float {
    return 2.0;
}

public func APM_PanelWidth() -> Float {
    return 640.0;
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

// --- Button ----------------------------------------------------------------------------------------------------------

public class APM_Button extends IScriptable {
    public let id: String;
    public let root: wref<inkCanvas>;
    public let bg: wref<inkRectangle>;
    public let label: wref<inkText>;
    public let isSelected: Bool;
    public let isEnabled: Bool;
    public let isHovered: Bool;
    public let isMarked: Bool; // secondary highlight (e.g. a slot that holds a saved pose)

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

// --- Panel -------------------------------------------------------------------------------------------------------

public class APM_PoseEditor extends IScriptable {
    private let m_controller: wref<gameuiPhotoModeMenuController>;
    private let m_layer: wref<inkCanvas>;   // full-screen container, parent of everything below
    private let m_panel: wref<inkCanvas>;
    private let m_openButton: ref<APM_Button>;
    private let m_isOpen: Bool;
    private let m_dockRight: Bool;

    private let m_buttons: array<ref<APM_Button>>;
    private let m_rows: array<ref<APM_ValueRow>>;
    private let m_groupNames: array<String>;
    private let m_groupButtons: array<ref<APM_Button>>;
    private let m_partButtons: array<ref<APM_Button>>;
    private let m_partIndices: array<Int32>; // body part index shown by each part button (-1 = unused)
    private let m_partTitle: wref<inkText>;
    private let m_moveTitle: wref<inkText>;
    private let m_ikTitle: wref<inkText>;
    private let m_status: wref<inkText>;

    private let m_dragRow: ref<APM_ValueRow>;
    private let m_dragValue: Float;
    private let m_confirmResetAll: Bool;
    private let m_refreshTimer: Float;

    public func Initialize(controller: ref<gameuiPhotoModeMenuController>) {
        this.m_controller = controller;
        let root = controller.GetRootCompoundWidget();
        if !IsDefined(root) {
            return;
        }

        let layer = new inkCanvas();
        layer.SetName(n"APM_PoseEditorLayer");
        layer.SetAnchor(inkEAnchor.Fill);
        layer.Reparent(root);
        this.m_layer = layer;

        this.m_openButton = this.CreateButton(layer, "open", "Pose editor", 0.0, 0.0, 200.0, 40.0);
        this.BuildPanel();
        this.SetOpen(false);
        this.ApplyDock();

        controller.RegisterToGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
        controller.RegisterToGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
    }

    public func Uninitialize() {
        if IsDefined(this.m_controller) {
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelative", this, n"OnGlobalRelative");
            this.m_controller.UnregisterFromGlobalInputCallback(n"OnPostOnRelease", this, n"OnGlobalRelease");
        }
        this.m_dragRow = null;
    }

    public func Update(timeDelta: Float) {
        if !this.m_isOpen {
            return;
        }
        this.m_refreshTimer -= timeDelta;
        if this.m_refreshTimer <= 0.0 && !IsDefined(this.m_dragRow) {
            this.m_refreshTimer = 0.25;
            this.Refresh(); // picks up changes made through the Photo Mode menu
        }
    }

    // --- Construction ------------------------------------------------------------------------------------------------

    private func BuildPanel() {
        let s = APM_UiScale();
        let width = APM_PanelWidth();

        let panel = new inkCanvas();
        panel.SetName(n"APM_PoseEditor");
        panel.SetSize(width * s, 1000.0 * s);
        panel.SetInteractive(true);
        panel.Reparent(this.m_layer);
        panel.RegisterToCallback(n"OnHoverOver", this, n"OnPanelHoverOver");
        panel.RegisterToCallback(n"OnHoverOut", this, n"OnPanelHoverOut");
        this.m_panel = panel;

        let bg = new inkRectangle();
        bg.SetAnchor(inkEAnchor.Fill);
        bg.SetTintColor(APM_ColorDark());
        bg.SetOpacity(0.86);
        bg.SetInteractive(true);
        bg.Reparent(panel);

        let accent = new inkRectangle();
        accent.SetAnchor(inkEAnchor.TopFillHorizontaly);
        accent.SetSize(width * s, 3.0 * s);
        accent.SetTintColor(APM_ColorAccent());
        accent.Reparent(panel);

        // Header
        this.CreateText(panel, "Advanced pose", 16.0, 10.0, 260.0, 34.0, 24, APM_ColorAccent());
        this.CreateButton(panel, "enabled", "Pose off", 300.0, 10.0, 120.0, 34.0);
        this.CreateButton(panel, "dock", "<  >", 430.0, 10.0, 80.0, 34.0);
        this.CreateButton(panel, "hide", "Hide", 520.0, 10.0, 104.0, 34.0);

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
            let y = 56.0 + Cast<Float>(i / 4) * 40.0;
            ArrayPush(this.m_groupButtons, this.CreateButton(panel, "group:" + IntToString(i), this.m_groupNames[i], x, y, 146.0, 34.0));
            i += 1;
        }

        // Body parts of the selected group (up to six, two rows of three)
        i = 0;
        while i < 6 {
            let x = 16.0 + Cast<Float>(i % 3) * 203.0;
            let y = 146.0 + Cast<Float>(i / 3) * 40.0;
            ArrayPush(this.m_partButtons, this.CreateButton(panel, "part:" + IntToString(i), "", x, y, 197.0, 34.0));
            ArrayPush(this.m_partIndices, -1);
            i += 1;
        }

        // Rotation
        this.m_partTitle = this.CreateText(panel, "", 16.0, 234.0, 600.0, 28.0, 20, APM_ColorAccent());
        this.CreateRow(panel, 7702u, "Bend forward / back", 266.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(panel, 7703u, "Bend sideways", 304.0, -180.0, 180.0, 1.0, 0.2, "");
        this.CreateRow(panel, 7704u, "Twist", 342.0, -180.0, 180.0, 1.0, 0.2, "");

        // Translation (body only)
        this.m_moveTitle = this.CreateText(panel, "Move (cm)", 16.0, 386.0, 600.0, 28.0, 20, APM_ColorAccent());
        this.CreateRow(panel, 7705u, "Left / right", 418.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7706u, "Forward / back", 456.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7707u, "Up / down", 494.0, -100.0, 100.0, 0.5, 0.05, " cm");
        this.CreateButton(panel, "resetpart", "Reset part", 16.0, 536.0, 296.0, 34.0);

        // IK
        this.m_ikTitle = this.CreateText(panel, "IK: pin a hand or foot", 16.0, 584.0, 600.0, 28.0, 20, APM_ColorAccent());
        let limbs = APM_UiGetOptionCount(7710u);
        i = 0;
        while i < limbs {
            this.CreateButton(panel, "ik:" + IntToString(i), APM_UiGetOptionLabel(7710u, i), 16.0 + Cast<Float>(i) * 124.0, 616.0, 118.0, 34.0);
            i += 1;
        }
        this.CreateButton(panel, "pin", "Pin off", 512.0, 616.0, 112.0, 34.0);
        this.CreateRow(panel, 7712u, "Right / left", 658.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7713u, "Forward / back", 696.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7714u, "Up / down", 734.0, -80.0, 80.0, 0.5, 0.05, " cm");
        this.CreateRow(panel, 7715u, "Elbow / knee", 772.0, -180.0, 180.0, 1.0, 0.2, "");

        // Poses
        this.CreateText(panel, "Poses", 16.0, 818.0, 600.0, 28.0, 20, APM_ColorAccent());
        let slots = APM_UiGetSlotCount();
        i = 0;
        while i < slots {
            this.CreateButton(panel, "slot:" + IntToString(i), IntToString(i + 1), 16.0 + Cast<Float>(i) * 61.0, 850.0, 55.0, 34.0);
            i += 1;
        }
        this.CreateButton(panel, "save", "Save", 16.0, 892.0, 146.0, 34.0);
        this.CreateButton(panel, "load", "Load", 168.0, 892.0, 146.0, 34.0);
        this.CreateButton(panel, "resetall", "Reset all", 478.0, 892.0, 146.0, 34.0);
        this.m_status = this.CreateText(panel, "Drag a value with the mouse, or use - / +", 16.0, 940.0, 608.0, 28.0, 16, APM_ColorDimText());
    }

    // Returns a strong ref: returning a wref built from a local ref crashed the game when the caller ignored the result.
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
        label.SetFontSize(Cast<Int32>(18.0 * s));
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
        row.label = this.CreateText(parent, text, 16.0, y, 240.0, 34.0, 17, APM_ColorText());
        ArrayPush(row.buttons, this.CreateButton(parent, "dec:" + key, "-", 262.0, y, 48.0, 34.0));
        row.value = this.CreateButton(parent, "drag:" + key, "0", 314.0, y, 200.0, 34.0);
        ArrayPush(row.buttons, this.CreateButton(parent, "inc:" + key, "+", 518.0, y, 48.0, 34.0));
        ArrayPush(row.buttons, this.CreateButton(parent, "zero:" + key, "0", 572.0, y, 52.0, 34.0));
        ArrayPush(this.m_rows, row);
    }

    // --- State -----------------------------------------------------------------------------------------------------

    private func SetOpen(open: Bool) {
        this.m_isOpen = open;
        this.m_panel.SetVisible(open);
        this.m_openButton.root.SetVisible(!open);
        this.m_dragRow = null;
        if open {
            this.Refresh();
        } else {
            this.SetHover(false);
        }
    }

    private func ApplyDock() {
        let s = APM_UiScale();
        if this.m_dockRight {
            this.m_panel.SetAnchor(inkEAnchor.TopRight);
            this.m_panel.SetAnchorPoint(1.0, 0.0);
            this.m_panel.SetMargin(0.0, 70.0 * s, 24.0 * s, 0.0);
            this.m_openButton.root.SetAnchor(inkEAnchor.TopRight);
            this.m_openButton.root.SetAnchorPoint(1.0, 0.0);
            this.m_openButton.root.SetMargin(0.0, 70.0 * s, 24.0 * s, 0.0);
        } else {
            this.m_panel.SetAnchor(inkEAnchor.TopLeft);
            this.m_panel.SetAnchorPoint(0.0, 0.0);
            this.m_panel.SetMargin(24.0 * s, 70.0 * s, 0.0, 0.0);
            this.m_openButton.root.SetAnchor(inkEAnchor.TopLeft);
            this.m_openButton.root.SetAnchorPoint(0.0, 0.0);
            this.m_openButton.root.SetMargin(24.0 * s, 70.0 * s, 0.0, 0.0);
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

    public func Refresh() {
        if !IsDefined(this.m_panel) {
            return;
        }

        // Master switch
        let enabled = APM_UiGetValue(7700u) > 0.5;
        let enabledButton = this.FindButton("enabled");
        if IsDefined(enabledButton) {
            enabledButton.label.SetText(enabled ? "Pose on" : "Pose off");
        }
        this.SetButton("enabled", enabled, true);

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

        // Pose slots
        let currentSlot = Cast<Int32>(APM_UiGetValue(7720u));
        let slots = APM_UiGetSlotCount();
        i = 0;
        while i < slots {
            let slotButton = this.FindButton("slot:" + IntToString(i));
            if IsDefined(slotButton) {
                slotButton.isSelected = i == currentSlot;
                slotButton.isMarked = APM_UiIsSlotSaved(i);
                slotButton.UpdateLook();
            }
            i += 1;
        }
        let loadButton = this.FindButton("load");
        if IsDefined(loadButton) {
            loadButton.isEnabled = APM_UiIsSlotSaved(currentSlot);
            loadButton.UpdateLook();
        }
        let resetAllButton = this.FindButton("resetall");
        if IsDefined(resetAllButton) {
            resetAllButton.label.SetText(this.m_confirmResetAll ? "Confirm?" : "Reset all");
            resetAllButton.label.SetTintColor(this.m_confirmResetAll ? APM_ColorWarning() : APM_ColorText());
        }
    }

    private func SetStatus(text: String) {
        if IsDefined(this.m_status) {
            this.m_status.SetText(text);
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
        let resetConfirm = true;

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
            this.ApplyDock();
            return;
        }

        if Equals(id, "enabled") {
            APM_UiSetValue(7700u, APM_UiGetValue(7700u) > 0.5 ? 0.0 : 1.0);
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
        } else if Equals(prefix, "slot") {
            APM_UiSetValue(7720u, Cast<Float>(StringToInt(arg)));
        } else if Equals(id, "save") {
            APM_UiSetValue(7723u, 1.0);
            this.SetStatus("Pose saved to slot " + IntToString(Cast<Int32>(APM_UiGetValue(7720u)) + 1));
        } else if Equals(id, "load") {
            APM_UiSetValue(7724u, 1.0);
            this.SetStatus("Pose loaded from slot " + IntToString(Cast<Int32>(APM_UiGetValue(7720u)) + 1));
        } else if Equals(id, "resetpart") {
            APM_UiSetValue(7721u, 1.0);
        } else if Equals(id, "resetall") {
            if this.m_confirmResetAll {
                APM_UiSetValue(7722u, 1.0);
                this.SetStatus("All edits removed");
            } else {
                this.m_confirmResetAll = true;
                resetConfirm = false;
            }
        }

        if resetConfirm {
            this.m_confirmResetAll = false;
        }
        APM_UiRequestRefresh(); // keeps the Photo Mode menu items in sync
        this.Refresh();
    }

    // --- Callbacks -----------------------------------------------------------------------------------------------

    protected cb func OnPanelHoverOver(e: ref<inkPointerEvent>) -> Bool {
        this.SetHover(true);
    }

    protected cb func OnPanelHoverOut(e: ref<inkPointerEvent>) -> Bool {
        if !IsDefined(this.m_dragRow) {
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

    protected cb func OnGlobalRelative(e: ref<inkPointerEvent>) -> Bool {
        let row = this.m_dragRow;
        if !IsDefined(row) || !e.IsAction(n"mouse_x") {
            return false;
        }
        this.m_dragValue = ClampF(this.m_dragValue + e.GetAxisData() * row.dragScale, row.minValue, row.maxValue);
        APM_UiSetValue(row.attribute, this.m_dragValue);
        row.value.label.SetText(this.FormatValue(row, this.m_dragValue));
    }

    protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
        if IsDefined(this.m_dragRow) && e.IsAction(n"click") {
            this.m_dragRow = null;
            APM_UiRequestRefresh();
            this.Refresh();
        }
    }
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
