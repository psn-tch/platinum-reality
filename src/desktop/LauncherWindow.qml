import QtQuick
import QtQuick.Controls

// The Launcher, as 9.2.2's (captured in the reference VM, 2026-11-15): a window of large square
// buttons, each an icon over its name; one click opens the item. As 9.2.2's came from aliases in the
// Launcher Items folder, ours are the aliases (links) in the session profile's Launcher Items folder,
// added with File ▸ Add To Launcher or the Applications window's Add To Launcher (docs/launcher.md).
DesktopWindow {
    id: launcher
    objectName: "launcher-window"
    title: "Launcher"
    width: 560; height: 200
    resizable: true; zoomable: true; collapsible: true
    minimumSize: Qt.size(200, 110)
    required property var source
    property var items: []
    onRaised: refresh()
    function refresh() { if (source) items = source.launcherItems() }
    Connections { target: launcher.source; function onLauncherItemsChanged() { if (launcher.visible) launcher.refresh() } }
    Rectangle {
        anchors.fill: parent; anchors.margins: 6
        color: "white"; border.color: "#555"
        Flickable {
            id: view
            anchors.fill: parent; anchors.margins: 1; clip: true
            contentHeight: grid.implicitHeight + 16
            ScrollBar.vertical: PlatinumScrollBar {}
            Flow {
                id: grid
                x: 8; y: 8; width: view.width - 16; spacing: 8
                Repeater {
                    model: launcher.items
                    delegate: Item {
                        id: button
                        required property var modelData
                        required property int index
                        objectName: "launcher-item-" + modelData.name
                        width: 84; height: 74
                        readonly property bool pressed: press.pressed && press.containsMouse
                        Rectangle {
                            id: face
                            x: (parent.width - width) / 2; width: 48; height: 48
                            color: button.pressed ? "#888" : "#ddd"; border.color: "#222"
                            // Bevel: light top-left, dark bottom-right; reversed while pressed.
                            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: button.pressed ? "#555" : "white" }
                            Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: button.pressed ? "#555" : "white" }
                            Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: button.pressed ? "#bbb" : "#888" }
                            Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: button.pressed ? "#bbb" : "#888" }
                            Image {
                                id: appIcon
                                anchors.centerIn: parent; width: 32; height: 32
                                visible: status === Image.Ready
                                source: button.modelData.application && launcher.source ? launcher.source.applicationIcon(button.modelData.application, 32) : ""
                                opacity: button.modelData.exists ? 1 : 0.4
                            }
                            DesktopIcon {
                                anchors.centerIn: parent
                                visible: !appIcon.visible
                                kind: button.modelData.application ? "application" : button.modelData.folder ? "folder" : "file"
                                selected: button.pressed
                                personalFolder: launcher.source ? launcher.source.personalAssets : ""
                                opacity: button.modelData.exists ? 1 : 0.4
                            }
                        }
                        Text {
                            anchors.top: face.bottom; anchors.topMargin: 4; width: parent.width
                            horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
                            text: button.modelData.name; textFormat: Text.PlainText; font.pixelSize: 11
                            color: button.modelData.exists ? "black" : "#888"
                        }
                        MouseArea {
                            id: press
                            anchors.fill: face
                            onClicked: launcher.source.openLauncherItem(button.modelData.link)
                        }
                    }
                }
            }
        }
        // Centred in the pane, not in the Flickable's content, which is a 16 px strip while empty
        // (Lily Hewitt, 2026-11-16: the hint's first line was cut off above the pane).
        Text {
            objectName: "launcher-empty"
            visible: launcher.items.length === 0
            anchors.centerIn: parent; width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            text: "To add an application, choose File ▸ Add Application…. To add a file or folder, select it and choose File ▸ Add To Launcher."
            font.pixelSize: 11; color: "#777"
        }
    }
}
