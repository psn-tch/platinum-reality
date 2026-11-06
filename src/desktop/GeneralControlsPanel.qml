import QtQuick
import QtQuick.Controls

// General Controls, a panel of the Control Panels window, laid out as 9.2.2's (captured in the
// reference VM, 2026-11-04): whether the desktop's icons show while another application is in
// front, how many times a chosen menu item blinks, and how fast the insertion point blinks in
// the desktop's own text fields. Saved in appearance.json (AppearanceSettings.general).
Item {
    id: panel
    required property var settings
    objectName: "general-controls-panel"
    readonly property string panelId: "general"
    readonly property string title: "General Controls"
    readonly property string glyph: "general"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property var general: settings.general
    function edit() { if (host) host.show(panelId) }
    Column {
        id: body
        width: Math.min(parent.width, 500)
        spacing: 14
        PlatinumGroup {
            title: "Desktop"
            PlatinumCheckBox {
                objectName: "general-show-desktop"
                text: "Show Desktop when in background"
                checked: panel.general.showDesktop === true
                onToggled: panel.settings.setGeneralOption("showDesktop", checked)
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: "When this is off, the icons on the desktop are hidden while another application is in front."
            }
        }
        Row {
            spacing: 14
            readonly property real half: (body.width - spacing) / 2
            PlatinumGroup {
                title: "Menu Blinking"
                width: parent.half
                // 9.2.2: Off, 1, 2 and 3 blinks, three by default.
                PlatinumSlider {
                    objectName: "general-menu-blinks"
                    width: parent.width; from: 0; to: 3; stepSize: 1; snapMode: Slider.SnapAlways
                    tickCount: 4; tickLabels: ["Off", "1", "2", "3"]
                    value: panel.general.menuBlinks
                    onCommitted: function(value) { panel.settings.setGeneralOption("menuBlinks", Math.round(value)) }
                }
            }
            PlatinumGroup {
                title: "Insertion Point Blinking"
                width: parent.half
                PlatinumSlider {
                    objectName: "general-caret-blink"
                    width: parent.width; from: 0; to: 2; stepSize: 1; snapMode: Slider.SnapAlways
                    tickCount: 3; tickLabels: ["Slow", "", "Fast"]
                    value: panel.general.caretBlink
                    onCommitted: function(value) { panel.settings.setGeneralOption("caretBlink", Math.round(value)) }
                }
            }
        }
        PlatinumGroup {
            title: "Try It"
            Text { text: "Watch the insertion point blink here:"; font.pixelSize: 12 }
            TextField {
                objectName: "general-try"
                width: parent.width; height: 28
                font.pixelSize: 12; selectByMouse: true
                color: "black"; selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"; selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
                background: FieldBackground {}
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "Menu blinking applies to the desktop’s own menus and the Control Strip’s; applications keep their own menus and insertion point."
        }
    }
}
