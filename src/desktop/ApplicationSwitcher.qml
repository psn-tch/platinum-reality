import QtQuick

// The Application Switcher, torn off the Application menu (drag down past the open menu), as in
// Mac OS 9.2.2 (captured in the reference VM, 2026-11-04): a small floating palette titled
// "Applications" with a row for each running application, its icon and name; the one in front
// is drawn pressed in. Click a row to switch to it. Drag the title strip to move the palette;
// the close box puts it away. Where it is, and whether it is shown, is kept in appearance.json.
Item {
    id: switcher
    objectName: "application-switcher"
    required property var sessionManager
    required property var settings
    // [{id, name, icon, active, hidden}] in menu order.
    property var applications: []
    signal chosen(var id)
    readonly property rect screenRect: sessionManager.primaryDisplayRect
    readonly property int rowHeight: 24
    width: 158; height: 20 + Math.max(1, applications.length) * rowHeight
    visible: settings.switcher.visible
    // Kept on the screen, even when the screen got smaller.
    x: screenRect.x + Math.max(0, Math.min(settings.switcher.x, screenRect.width - width))
    y: screenRect.y + Math.max(sessionManager.menuBarHeight, Math.min(settings.switcher.y, screenRect.height - height))
    Rectangle { z: -1; x: 1; y: 1; width: parent.width; height: parent.height; color: "black" }
    Rectangle {
        anchors.fill: parent; color: "#ccc"; border.color: "black"
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "white" }
        Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: "white" }
        Rectangle { x: parent.width-2; y: 2; width: 1; height: parent.height-3; color: "#999" }
        Rectangle { x: 2; y: parent.height-2; width: parent.width-3; height: 1; color: "#999" }
    }
    // The title strip: a dotted texture either side of the title, the close box at the left.
    Item {
        id: strip
        x: 0; y: 1; width: parent.width; height: 14
        Item {
            id: texture
            objectName: "application-switcher-title-texture"
            anchors.fill: parent
            // Native rectangles keep the raised dots sharp at fractional scale.
            // Reference: white/#777 pairs on a three-pixel grid.
            Repeater {
                model: 3
                Item {
                    required property int index
                    x: 14; y: 3 + index * 3; width: texture.width - 16; height: 2
                    Repeater {
                        model: Math.floor(parent.width / 3)
                        Item {
                            required property int index
                            x: index * 3; width: 2; height: 2
                            Rectangle { width: 1; height: 1; color: "white" }
                            Rectangle { x: 1; y: 1; width: 1; height: 1; color: "#777" }
                        }
                    }
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent; width: title.implicitWidth + 8; height: parent.height; color: "#ccc"
            Text { id: title; anchors.centerIn: parent; text: "Applications"; font.family:switcher.settings.fontFamily;font.pixelSize: 12; font.bold: true }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) {
                switcher.sessionManager.beginShellWindowMove(switcher, mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: {
                switcher.sessionManager.finishShellWindowMove(switcher)
                switcher.settings.setSwitcher(true, Math.max(0, Math.round(switcher.x - switcher.screenRect.x)),
                                             Math.max(0, Math.round(switcher.y - switcher.screenRect.y)))
                switcher.x = Qt.binding(function() { return switcher.screenRect.x + Math.max(0,
                    Math.min(switcher.settings.switcher.x, switcher.screenRect.width-switcher.width)) })
                switcher.y = Qt.binding(function() { return switcher.screenRect.y + Math.max(switcher.sessionManager.menuBarHeight,
                    Math.min(switcher.settings.switcher.y, switcher.screenRect.height-switcher.height)) })
            }
            onCanceled: switcher.sessionManager.finishShellWindowMove(switcher, false)
        }
        Rectangle {
            objectName: "application-switcher-close"
            x: 2; y: 2; width: 10; height: 10; border.color: "black"
            color: closeMouse.pressed ? "#888" : "#ddd"
            Rectangle { x: 1; y: 1; width: 7; height: 1; color: closeMouse.pressed ? "#666" : "white" }
            Rectangle { x: 1; y: 1; width: 1; height: 7; color: closeMouse.pressed ? "#666" : "white" }
            MouseArea {
                id: closeMouse; anchors.fill: parent
                onClicked: switcher.settings.setSwitcher(false, switcher.settings.switcher.x, switcher.settings.switcher.y)
            }
        }
    }
    // The rows, in a dark well.
    Rectangle {
        x: 3; y: 15; width: parent.width - 6; height: parent.height - 18
        color: "black"
        Rectangle {x:-1;y:-1;width:parent.width+1;height:1;color:"#999"}
        Rectangle {x:-1;y:0;width:1;height:parent.height;color:"#999"}
        Rectangle {x:parent.width;y:0;width:1;height:parent.height;color:"white"}
        Rectangle {x:0;y:parent.height;width:parent.width+1;height:1;color:"white"}
        // Each row placed by its index, not by a Column: a Column lays new rows out only before
        // the next frame, so a click just after the list changed could reach the row above.
        Item {
            x: 1; y: 1; width: parent.width - 2; height: parent.height - 2
            Repeater {
                model: switcher.applications
                Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "application-switcher-" + modelData.name
                    y: index * switcher.rowHeight
                    width: parent.width; height: switcher.rowHeight
                    readonly property bool down: modelData.active || rowMouse.pressed
                    color: down ? "#888" : "#ccc"
                    Rectangle { width: parent.width-1; height: 1; color: row.down ? "#111" : "#666" }
                    Rectangle { width: 1; height: parent.height-1; color: row.down ? "#111" : "#666" }
                    Rectangle { x:parent.width-1;y:1;width:1;height:parent.height-1;color:row.down?"#444":"#333" }
                    Rectangle { x:1;y:parent.height-1;width:parent.width-1;height:1;color:row.down?"#444":"#333" }
                    Rectangle { x:1;y:1;width:parent.width-3;height:1;color:row.down?"#555":"white" }
                    Rectangle { x:1;y:1;width:1;height:parent.height-3;color:row.down?"#555":"white" }
                    Rectangle { x:parent.width-2;y:2;width:1;height:parent.height-4;color:row.down?"#aaa":"#888" }
                    Rectangle { x:2;y:parent.height-2;width:parent.width-4;height:1;color:row.down?"#aaa":"#888" }
                    MenuGlyph {
                        objectName:"application-switcher-icon-"+row.modelData.name
                        x: 4; anchors.verticalCenter: parent.verticalCenter
                        source:row.modelData.icon || ""
                        kind: row.modelData.name === "Finder" ? "disk" : "application"
                        opacity: row.modelData.hidden ? 0.5 : 1
                    }
                    Text {
                        x: 26; width: parent.width - 30; anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight
                        font.family:switcher.settings.fontFamily;font.pixelSize: 12; color: row.modelData.hidden ? "#555" : "black"
                    }
                    MouseArea { id: rowMouse; anchors.fill: parent; onClicked: switcher.chosen(row.modelData.id) }
                }
            }
        }
    }
}
