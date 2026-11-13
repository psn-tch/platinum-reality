import QtQuick
import QtQuick.Controls

// Apple Menu Options, a panel of the Control Panels window, laid out as 9.2.2's (captured in the
// reference VM, 2026-11-12): Submenus On or Off, each with a small picture of a menu, and
// Remember recently used items. Ours remembers applications only (there are no recent documents or
// servers to count), from 1 to 99, and remembering off records nothing new. Saved in
// appearance.json (AppearanceSettings.appleMenu); the Apple menu follows it at once.
Item {
    id: panel
    required property var settings
    objectName: "apple-menu-options-panel"
    readonly property string panelId: "apple-menu-options"
    readonly property string title: "Apple Menu Options"
    readonly property string glyph: "general"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property var options: settings.appleMenu
    function edit() { if (host) host.show(panelId) }
    // Our own small menus, as 9.2.2's beside each choice: one with a submenu beside it, one without.
    component MenuPicture: Item {
        property bool withSubmenu: false
        width: 34; height: 26
        Rectangle { x: 0; y: 0; width: 18; height: 26; color: "white"; border.color: "#222" }
        Repeater {
            model: 5
            Rectangle { required property int index; x: 3; y: 4 + index * 4; width: 12; height: 1; color: "#666" }
        }
        Rectangle { visible: parent.withSubmenu; x: 3; y: 7; width: 12; height: 3; color: "#99f" }
        Rectangle { visible: parent.withSubmenu; x: 16; y: 6; width: 18; height: 16; color: "white"; border.color: "#222" }
        Repeater {
            model: 3
            Rectangle { required property int index; visible: parent.withSubmenu; x: 19; y: 10 + index * 4; width: 12; height: 1; color: "#666" }
        }
    }
    Column {
        id: body
        width: Math.min(parent.width, 460)
        spacing: 14
        PlatinumGroup {
            title: "Submenus"
            Row {
                spacing: 36
                Row {
                    spacing: 10
                    PlatinumRadioButton {
                        objectName: "apple-menu-submenus-on"
                        autoExclusive: false
                        text: "On"
                        anchors.verticalCenter: parent.verticalCenter
                        checked: panel.options.submenus !== false
                        onClicked: { checked = Qt.binding(function() { return panel.options.submenus !== false })
                                     panel.settings.setAppleMenuOption("submenus", true) }
                    }
                    MenuPicture { withSubmenu: true }
                }
                Row {
                    spacing: 10
                    PlatinumRadioButton {
                        objectName: "apple-menu-submenus-off"
                        autoExclusive: false
                        text: "Off"
                        anchors.verticalCenter: parent.verticalCenter
                        checked: panel.options.submenus === false
                        onClicked: { checked = Qt.binding(function() { return panel.options.submenus === false })
                                     panel.settings.setAppleMenuOption("submenus", false) }
                    }
                    MenuPicture { withSubmenu: false }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: "With submenus off, Control Panels opens its window and Recent Applications opens Applications."
            }
        }
        PlatinumGroup {
            title: "Recent Items"
            PlatinumCheckBox {
                objectName: "apple-menu-remember"
                text: "Remember recently used items:"
                font.bold: true
                checked: panel.options.rememberRecent !== false
                onToggled: {
                    panel.settings.setAppleMenuOption("rememberRecent", checked)
                    checked = Qt.binding(function() { return panel.options.rememberRecent !== false })
                }
            }
            Row {
                spacing: 12
                leftPadding: 24
                Text {
                    text: "Applications:"; font.pixelSize: 12; font.bold: true; textFormat: Text.PlainText
                    width: 110; anchors.verticalCenter: parent.verticalCenter
                    color: count.enabled ? "#111" : "#888"
                }
                TextField {
                    id: count
                    objectName: "apple-menu-recent-applications"
                    width: 48; height: 26
                    enabled: panel.options.rememberRecent !== false
                    font.pixelSize: 12; selectByMouse: true; horizontalAlignment: TextInput.AlignRight
                    validator: IntValidator { bottom: 1; top: 99 }
                    color: "black"
                    selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"
                    selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
                    background: FieldBackground {}
                    text: String(panel.options.recentApplications || 10)
                    onEditingFinished: {
                        const value = parseInt(text)
                        if (value >= 1 && value <= 99) panel.settings.setAppleMenuOption("recentApplications", value)
                        text = Qt.binding(function() { return String(panel.options.recentApplications || 10) })
                    }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: panel.options.rememberRecent === false
                      ? "Recent Applications is left out of the Apple menu, and nothing new is remembered."
                      : "The Apple menu's Recent Applications lists this many, newest first."
            }
        }
    }
}
