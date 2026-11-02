import QtQuick
import QtQuick.Controls

// Keyboard, a panel of the Control Panels window: Mac OS 9's Key Repeat Rate and Delay Until
// Repeat, with a field to try them. Saved in input.json (InputSettings); the session supervisor
// applies them to every keyboard, and applications receive them from the desktop.
Item {
    id: panel
    required property var settings
    objectName: "keyboard-panel"
    readonly property string panelId: "keyboard"
    readonly property string title: "Keyboard"
    readonly property string glyph: "keyboard"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property var keyboard: settings.keyboard
    function edit() { if (host) host.show(panelId) }
    Column {
        id: body
        width: parent.width
        spacing: 12
        Row {
            spacing: 10
            StripGlyph { kind: "keyboard"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: "Changes apply at once to every keyboard."
                font.pixelSize: 12; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text { text: "Key Repeat Rate"; font.pixelSize: 12; font.bold: true }
        PlatinumSlider {
            objectName: "keyboard-repeat-rate"
            width: 320; from: 2; to: 60; stepSize: 1
            lowLabel: "Slow"; highLabel: "Fast"
            value: panel.keyboard.repeatRate
            onCommitted: function(value) { panel.settings.set("keyboard", "repeatRate", Math.round(value)) }
        }
        Text { text: "Delay Until Repeat"; font.pixelSize: 12; font.bold: true }
        // Long on the left, as in Mac OS 9: the slider runs from a long delay to a short one.
        PlatinumSlider {
            objectName: "keyboard-repeat-delay"
            width: 320; from: -1500; to: -150; stepSize: 50
            lowLabel: "Long"; highLabel: "Short"
            value: -panel.keyboard.repeatDelay
            onCommitted: function(value) { panel.settings.set("keyboard", "repeatDelay", -Math.round(value)) }
        }
        Text {
            objectName: "keyboard-repeat-summary"
            font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
            text: "Repeats " + panel.keyboard.repeatRate + " times a second, after "
                  + (panel.keyboard.repeatDelay / 1000).toFixed(2) + " seconds."
        }
        Text { text: "Try it: hold down a key here."; font.pixelSize: 12 }
        TextField {
            objectName: "keyboard-try"
            width: 320; height: 28
            font.pixelSize: 12; selectByMouse: true
            color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
            background: FieldBackground {}
        }
        Row {
            spacing: 10
            PlatinumButton {
                objectName: "keyboard-defaults"; text: "Use Defaults"
                onClicked: panel.settings.reset("keyboard")
            }
        }
        Rectangle { width: parent.width; height: 1; color: "#999" }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "The Command key (⌘) is the Windows or Super key. Help ▸ Help Center ▸ Keyboard shortcuts lists them all."
        }
    }
}
