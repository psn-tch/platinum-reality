import QtQuick

// A titled group box, as Mac OS 9.2.2's control panels group their settings (measured in the
// reference VM's General Controls, 2026-11-04): an etched frame, a #888 line with a white line
// inside it, and a bold title set into its top edge. Children are laid out in a Column inside.
Item {
    id: group
    property string title: ""
    property int padding: 12
    property alias spacing: inner.spacing
    default property alias content: inner.data
    width: parent ? parent.width : 400
    implicitHeight: inner.y + inner.implicitHeight + padding
    height: implicitHeight
    readonly property real frameTop: title !== "" ? Math.round(heading.implicitHeight / 2) : 0
    Rectangle {
        x: 0; y: group.frameTop; width: parent.width - 1; height: parent.height - group.frameTop - 1
        color: "transparent"; border.color: "#888"
    }
    Rectangle {
        x: 1; y: group.frameTop + 1; width: parent.width - 1; height: parent.height - group.frameTop - 1
        color: "transparent"; border.color: "white"
        z: -1
    }
    // The title interrupts the top edge: the panel colour behind it hides the lines.
    Rectangle {
        visible: group.title !== ""
        x: 8; y: 0; width: heading.implicitWidth + 8; height: heading.implicitHeight
        color: "#ddd"
        Text {
            id: heading
            x: 4; text: group.title; textFormat: Text.PlainText
            font.pixelSize: 13; font.bold: true
        }
    }
    Column {
        id: inner
        x: group.padding; y: group.frameTop + (group.title !== "" ? heading.implicitHeight / 2 + 8 : group.padding)
        width: group.width - 2 * group.padding
        spacing: 8
    }
}
