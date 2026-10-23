import QtQuick

// Mac OS 9's Info window (Finishing School FS-G01): modeless, bound to the one item it was opened
// for, read-only. Changing the selection never retargets it; facts Linux lacks read "Unavailable".
// The title bar moves it, the close box or Escape closes it, and nothing is ever written.
FocusScope {
    id: info
    required property var details
    signal closeRequested()
    signal raised()
    objectName: "info-" + details.key
    width: 330
    height: body.y + body.height + 14
    // Rows shown for this kind of item, in Mac OS 9's order.
    readonly property var rows: details.kind === "disk"
        ? [["Kind", details.kindLabel], ["Capacity", details.capacity], ["Available", details.available],
           ["Where", details.where], ["Created", details.created], ["Modified", details.modified]]
        : [["Kind", details.kindLabel]].concat(details.original !== undefined ? [["Original", details.original]]
                                                                              : [["Size", details.size]],
              [["Where", details.where], ["Created", details.created], ["Modified", details.modified]])
    Keys.onEscapePressed: info.closeRequested()
    Rectangle {
        anchors.fill: parent
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
    }
    MouseArea {
        // A click anywhere brings the window forward and gives it the keyboard.
        anchors.fill: parent
        onPressed: function(mouse) { info.raised(); info.forceActiveFocus(); mouse.accepted = false }
    }
    Item {
        id: titleBar
        objectName: "info-title-" + info.details.key
        x: 3; y: 3; width: parent.width - 6; height: 18
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 20; y: 3 + index * 3; width: titleBar.width - 24; height: 1
                color: info.activeFocus ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill: parent
            drag.target: info; drag.threshold: 2
            onPressed: { info.raised(); info.forceActiveFocus() }
        }
        Rectangle {
            objectName: "info-close-" + info.details.key
            x: 2; y: 3; width: 12; height: 12
            color: closeMouse.pressed ? "#888" : "#ddd"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: 9; height: 1; color: closeMouse.pressed ? "#666" : "white"}
            Rectangle {x: 1; y: 1; width: 1; height: 9; color: closeMouse.pressed ? "#666" : "white"}
            MouseArea {id: closeMouse; anchors.fill: parent; onClicked: info.closeRequested()}
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(titleBar.width - 48, title.implicitWidth + 12); height: 18; color: "#ddd"
            Text {
                id: title
                anchors.centerIn: parent
                width: Math.min(implicitWidth, titleBar.width - 60)
                text: info.details.name + " Info"
                textFormat: Text.PlainText; elide: Text.ElideMiddle; font.pixelSize: 12
            }
        }
    }
    Row {
        id: heading
        x: 14; y: 32; spacing: 10
        DesktopIcon {kind: info.details.kind; personalFolder: typeof manager !== "undefined" && manager ? manager.personalAssets : ""}
        Text {
            objectName: "info-name-" + info.details.key
            anchors.verticalCenter: parent.verticalCenter
            width: info.width - 90
            text: info.details.name; textFormat: Text.PlainText; elide: Text.ElideMiddle
            font.pixelSize: 12; font.bold: true
        }
    }
    Rectangle {id: rule; x: 10; y: heading.y + heading.height + 8; width: info.width - 20; height: 1; color: "#888"}
    Rectangle {x: 10; y: rule.y + 1; width: info.width - 20; height: 1; color: "white"}
    Grid {
        id: body
        x: 14; y: rule.y + 10
        columns: 2; columnSpacing: 8; rowSpacing: 5
        Repeater {
            // Label and value cells, row by row.
            model: [].concat.apply([], info.rows)
            Text {
                required property var modelData
                required property int index
                // Labels are right-aligned against their values, as in the Info window.
                width: index % 2 === 0 ? 70 : info.width - 106
                horizontalAlignment: index % 2 === 0 ? Text.AlignRight : Text.AlignLeft
                text: index % 2 === 0 ? modelData + ":" : String(modelData)
                objectName: index % 2 === 0 ? "" : "info-value-" + info.details.key + "-" + info.rows[Math.floor(index / 2)][0]
                textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; font.pixelSize: 12
                color: index % 2 === 0 ? "#333" : "#111"
            }
        }
    }
}
