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
    // [{id, name, active, hidden}] in menu order.
    property var applications: []
    signal chosen(var id)
    readonly property rect screenRect: sessionManager.primaryDisplayRect
    readonly property int rowHeight: 22
    width: 160; height: 18 + 8 + Math.max(1, applications.length) * rowHeight
    visible: settings.switcher.visible
    property point dragStart
    property point origin
    property bool dragging: false
    property point dragPosition
    // Kept on the screen, even when the screen got smaller.
    x: dragging ? dragPosition.x : screenRect.x + Math.max(0, Math.min(settings.switcher.x, screenRect.width - width))
    y: dragging ? dragPosition.y : screenRect.y + Math.max(26, Math.min(settings.switcher.y, screenRect.height - height))
    Rectangle { z: -1; x: 2; y: 2; width: parent.width; height: parent.height; color: "#50000000" }
    Rectangle {
        anchors.fill: parent; color: "#ddd"; border.color: "black"
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "white" }
        Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: "white" }
    }
    // The title strip: a dotted texture either side of the title, the close box at the left.
    Item {
        id: strip
        x: 2; y: 2; width: parent.width - 4; height: 14
        Canvas {
            anchors.fill: parent
            onPaint: {
                const c = getContext("2d")
                c.clearRect(0, 0, width, height)
                c.fillStyle = "#999"
                for (let y = 2; y < height - 2; y += 2)
                    for (let x = 16 + (y / 2) % 2; x < width - 2; x += 2) c.fillRect(x, y, 1, 1)
            }
            onAvailableChanged: if (available) requestPaint()
            onWidthChanged: requestPaint()
        }
        Rectangle {
            anchors.centerIn: parent; width: title.implicitWidth + 8; height: parent.height; color: "#ddd"
            Text { id: title; anchors.centerIn: parent; text: "Applications"; font.pixelSize: 11; font.bold: true }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) {
                switcher.dragStart = mapToItem(switcher.parent, mouse.x, mouse.y)
                switcher.origin = Qt.point(switcher.x, switcher.y); switcher.dragPosition = switcher.origin
            }
            onPositionChanged: function(mouse) {
                const at = mapToItem(switcher.parent, mouse.x, mouse.y)
                switcher.dragPosition = Qt.point(switcher.origin.x + at.x - switcher.dragStart.x,
                                                switcher.origin.y + at.y - switcher.dragStart.y)
                switcher.dragging = true
            }
            onReleased: {
                if (!switcher.dragging) return
                switcher.settings.setSwitcher(true, Math.max(0, Math.round(switcher.x - switcher.screenRect.x)),
                                             Math.max(0, Math.round(switcher.y - switcher.screenRect.y)))
                switcher.dragging = false
            }
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
        x: 4; y: 19; width: parent.width - 8; height: parent.height - 23
        color: "#222"
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
                    width: parent.width; height: switcher.rowHeight - (modelData === switcher.applications[switcher.applications.length - 1] ? 2 : 0)
                    readonly property bool down: modelData.active || rowMouse.pressed
                    color: down ? "#888" : "#ccc"
                    Rectangle { width: parent.width; height: 1; color: row.down ? "#555" : "white" }
                    Rectangle { width: 1; height: parent.height; color: row.down ? "#555" : "white" }
                    Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: row.down ? "#aaa" : "#888" }
                    MenuGlyph {
                        x: 4; anchors.verticalCenter: parent.verticalCenter
                        kind: row.modelData.name === "Finder" ? "disk" : "application"
                        opacity: row.modelData.hidden ? 0.5 : 1
                    }
                    Text {
                        x: 26; width: parent.width - 30; anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight
                        font.pixelSize: 12; color: row.modelData.hidden ? "#555" : "black"
                    }
                    MouseArea { id: rowMouse; anchors.fill: parent; onClicked: switcher.chosen(row.modelData.id) }
                }
            }
        }
    }
}
