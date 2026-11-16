import QtQuick
import QtQuick.Controls

// Control Strip, a panel of the Control Panels window, laid out as 9.2.2's (captured 2026-11-15):
// Show/Hide with Show Control Strip, Hide Control Strip and Hot key to show/hide, the current hot key
// (Command-Control-S, as 9.2.2's default; defining another is not offered) and, beside them, a picture
// of the strip. 9.2.2's Font Settings are left out: the strip's menus use the desktop's menu font.
Item {
    id: panel
    required property var settings
    objectName: "control-strip-panel"
    readonly property string panelId: "control-strip"
    readonly property string title: "Control Strip"
    readonly property string glyph: "general"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    readonly property string mode: !settings ? "show" : settings.controlStripHotKey ? "hotkey"
                                    : settings.controlStripVisible ? "show" : "hide"
    Column {
        id: body
        width: parent.width
        spacing: 12
        PlatinumGroup {
            title: "Show/Hide"; width: parent.width; spacing: 4
            Row {
                width: parent.width
                Column {
                    width: parent.width - stripPicture.width
                    PlatinumRadioButton { objectName: "control-strip-show"; text: "Show Control Strip"; autoExclusive: false
                                          checked: panel.mode === "show"; font.bold: true
                                          onClicked: { checked = Qt.binding(function() { return panel.mode === "show" }); panel.settings.setControlStripMode("show") } }
                    PlatinumRadioButton { objectName: "control-strip-hide"; text: "Hide Control Strip"; autoExclusive: false
                                          checked: panel.mode === "hide"; font.bold: true
                                          onClicked: { checked = Qt.binding(function() { return panel.mode === "hide" }); panel.settings.setControlStripMode("hide") } }
                    PlatinumRadioButton { objectName: "control-strip-hotkey"; text: "Hot key to show/hide"; autoExclusive: false
                                          checked: panel.mode === "hotkey"; font.bold: true
                                          onClicked: { checked = Qt.binding(function() { return panel.mode === "hotkey" }); panel.settings.setControlStripMode("hotkey") } }
                }
                // The strip's end, as 9.2.2 pictured it beside the choices.
                Row {
                    id: stripPicture
                    spacing: 0
                    Repeater {
                        model: ["sound", "monitors", "battery"]
                        delegate: Rectangle { required property string modelData; width: 26; height: 22; color: "#ddd"; border.color: "#555"
                                              StripGlyph { anchors.centerIn: parent; width: 16; height: 16; kind: parent.modelData } }
                    }
                    Rectangle { width: 10; height: 22; color: "#ccc"; border.color: "#555" }
                }
            }
            PlatinumButton { x: 40; text: "Define hot key…"; enabled: false; implicitWidth: 150 }
            PlatinumGroup {
                title: "Current hot key"; width: parent.width - 40; x: 20; padding: 6
                Text { objectName: "control-strip-current-hotkey"; width: parent.width; horizontalAlignment: Text.AlignHCenter
                       text: "command + control + s"; font.pixelSize: 12; color: panel.mode === "hotkey" ? "black" : "#777" }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
            text: "With Hot key to show/hide, press Command-Control-S to show or hide the strip. The strip's menus " +
                  "use the menu font chosen in Appearance."
        }
    }
}
