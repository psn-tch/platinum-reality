import QtQuick
import QtQuick.Controls

// Mouse & Trackpad, a panel of the Control Panels window: tracking speed and scrolling for mice
// and for trackpads, tap to click, and left-handed buttons. Saved in input.json (InputSettings);
// the session supervisor applies them. A setting for a device that is not connected waits for it.
Item {
    id: panel
    required property var settings
    objectName: "mouse-panel"
    readonly property string panelId: "mouse"
    readonly property string title: "Mouse & Trackpad"
    readonly property string glyph: "mouse"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property var pointer: settings.pointer
    readonly property var touchpad: settings.touchpad
    function edit() { if (host) host.show(panelId) }
    Column {
        id: body
        width: parent.width
        spacing: 10
        Row {
            spacing: 10
            StripGlyph { kind: "mouse"; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Mouse"; font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
        }
        Text { text: "Tracking Speed"; font.pixelSize: 12 }
        PlatinumSlider {
            objectName: "mouse-speed"
            width: 320; from: -1; to: 1; stepSize: 0.1
            lowLabel: "Slow"; highLabel: "Fast"
            value: panel.pointer.speed
            onCommitted: function(value) { panel.settings.set("pointer", "speed", Math.round(value * 10) / 10) }
        }
        PlatinumCheckBox {
            objectName: "mouse-natural"
            text: "Natural scrolling: content follows your fingers"
            checked: panel.pointer.natural === true
            onToggled: panel.settings.set("pointer", "natural", checked)
        }
        PlatinumCheckBox {
            objectName: "mouse-left-handed"
            text: "Left-handed: swap the left and right buttons"
            checked: panel.pointer.leftHanded === true
            onToggled: panel.settings.set("pointer", "leftHanded", checked)
        }
        Rectangle { width: parent.width; height: 1; color: "#999" }
        Row {
            spacing: 10
            StripGlyph { kind: "trackpad"; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Trackpad"; font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
        }
        Text { text: "Tracking Speed"; font.pixelSize: 12 }
        PlatinumSlider {
            objectName: "trackpad-speed"
            width: 320; from: -1; to: 1; stepSize: 0.1
            lowLabel: "Slow"; highLabel: "Fast"
            value: panel.touchpad.speed
            onCommitted: function(value) { panel.settings.set("touchpad", "speed", Math.round(value * 10) / 10) }
        }
        PlatinumCheckBox {
            objectName: "trackpad-tap"
            text: "Tap to click"
            checked: panel.touchpad.tap === true
            onToggled: panel.settings.set("touchpad", "tap", checked)
        }
        PlatinumCheckBox {
            objectName: "trackpad-natural"
            text: "Natural scrolling"
            checked: panel.touchpad.natural === true
            onToggled: panel.settings.set("touchpad", "natural", checked)
        }
        PlatinumCheckBox {
            objectName: "trackpad-typing"
            text: "Ignore the trackpad while typing"
            checked: panel.touchpad.disableWhileTyping === true
            onToggled: panel.settings.set("touchpad", "disableWhileTyping", checked)
        }
        Row {
            spacing: 10
            PlatinumButton {
                objectName: "mouse-defaults"; text: "Use Defaults"
                onClicked: { panel.settings.reset("pointer"); panel.settings.reset("touchpad") }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "Changes apply at once to every mouse and trackpad, including ones connected later."
        }
    }
}
