import QtQuick
import QtQuick.Controls

// Keyboard, a panel of the Control Panels window: Mac OS 9's Keyboard Layouts list, Key Repeat Rate
// and Delay Until Repeat, with a field to try them. Saved in input.json (InputSettings); the session
// supervisor applies them to every keyboard, and applications receive them from the desktop. With
// more than one layout checked, the keyboard menu in the menu bar switches between them.
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
    function toggleLayout(id, on) {
        const next = keyboard.layouts.filter(function(l) { return l !== id })
        if (on) next.push(id)
        // Kept in the list's order, so the keyboard menu lists them as the panel does.
        const order = settings.layoutChoices.map(function(c) { return c.id })
        next.sort(function(a, b) { return order.indexOf(a) - order.indexOf(b) })
        if (next.length > 0) settings.set("keyboard", "layouts", next)
    }
    // Laid out as 9.2.2's Keyboard control panel: the Keyboard Layouts box, then one Key Repeat
    // box, its two sliders side by side.
    Column {
        id: body
        width: parent.width
        spacing: 14
        PlatinumGroup {
            id: layoutsBox
            title: "Keyboard Layouts"
            width: Math.min(parent.width, 480)
            Text {
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
                text: "Selected keyboard layouts will display in the keyboard menu when more than one is selected."
            }
            Rectangle {
                width: parent.width; height: 5 * 20 + 2
                color: "white"; border.color: "#333"
                ListView {
                    id: layoutList
                    objectName: "keyboard-layouts"
                    x: 1; y: 1; width: parent.width - 2 - 16; height: parent.height - 2
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    model: panel.settings.layoutChoices
                    // The layout in use first in view, as 9.2.2 opens the list.
                    Component.onCompleted: positionViewAtIndex(Math.max(0, panel.settings.layoutChoices.findIndex(
                        function(c) { return c.id === panel.keyboard.layout })), ListView.Beginning)
                    ScrollBar.vertical: PlatinumScrollBar {
                        parent: layoutList.parent
                        x: layoutList.x + layoutList.width; y: 1; height: layoutList.height
                    }
                    delegate: PlatinumCheckBox {
                        required property var modelData
                        objectName: "keyboard-layout-" + modelData.id
                        x: 4; width: layoutList.width - 8; height: 20; implicitHeight: 20
                        text: modelData.name
                        checked: panel.keyboard.layouts.indexOf(modelData.id) >= 0
                        // At least one layout stays checked; its box is dimmed, as in 9.2.2.
                        enabled: !(checked && panel.keyboard.layouts.length === 1)
                        onToggled: panel.toggleLayout(modelData.id, checked)
                    }
                }
            }
            Text {
                objectName: "keyboard-layout-in-use"
                font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: "In use: " + panel.settings.layoutName(panel.keyboard.layout)
            }
            Text {
                visible: panel.keyboard.layouts.length > 1
                font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: "Switch with the keyboard menu or Command-Option-Space."
            }
        }
        PlatinumGroup {
            id: repeat
            title: "Key Repeat"
            width: Math.min(parent.width, 480)
            Row {
                spacing: 24
                readonly property real half: (repeat.width - 2 * repeat.padding - spacing) / 2
                Column {
                    spacing: 6
                    Text { text: "Key Repeat Rate"; font.pixelSize: 12; textFormat: Text.PlainText }
                    PlatinumSlider {
                        objectName: "keyboard-repeat-rate"
                        width: parent.parent.half; from: 2; to: 60; stepSize: 1
                        tickCount: 5; lowLabel: "Slow"; highLabel: "Fast"
                        value: panel.keyboard.repeatRate
                        onCommitted: function(value) { panel.settings.set("keyboard", "repeatRate", Math.round(value)) }
                    }
                }
                Column {
                    spacing: 6
                    Text { text: "Delay Until Repeat"; font.pixelSize: 12; textFormat: Text.PlainText }
                    // Long on the left, as in Mac OS 9: the slider runs from a long delay to a short one.
                    PlatinumSlider {
                        objectName: "keyboard-repeat-delay"
                        width: parent.parent.half; from: -1500; to: -150; stepSize: 50
                        tickCount: 5; lowLabel: "Long"; highLabel: "Short"
                        value: -panel.keyboard.repeatDelay
                        onCommitted: function(value) { panel.settings.set("keyboard", "repeatDelay", -Math.round(value)) }
                    }
                }
            }
            Text {
                objectName: "keyboard-repeat-summary"
                font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: "Repeats " + panel.keyboard.repeatRate + " times a second, after "
                      + (panel.keyboard.repeatDelay / 1000).toFixed(2) + " seconds."
            }
        }
        PlatinumGroup {
            title: "Try It"
            width: Math.min(parent.width, 480)
            Text { text: "Hold down a key here:"; font.pixelSize: 12 }
            TextField {
                objectName: "keyboard-try"
                width: parent.width; height: 28
                font.pixelSize: 12; selectByMouse: true
                color: "black"; selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"; selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
                background: FieldBackground {}
            }
        }
        Row {
            spacing: 10
            PlatinumButton {
                objectName: "keyboard-defaults"; text: "Use Defaults"
                onClicked: panel.settings.reset("keyboard")
            }
        }
        Text {
            width: Math.min(parent.width, 480); wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "Changes apply at once to every keyboard. The Command key (⌘) is the Windows or Super key. "
                  + "Help ▸ Help Center ▸ Keyboard shortcuts lists them all."
        }
    }
}
