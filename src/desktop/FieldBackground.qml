import QtQuick

// A shell text field's frame: white with a dark outline, and while the field has the keyboard
// Mac OS 9's two-pixel focus ring, the same as PlatinumStyle draws for application text
// fields. The room for the ring is always kept, so nothing moves when focus does.
Rectangle {
    readonly property bool focused: parent ? parent.activeFocus : false
    color: focused ? "#666699" : "transparent"
    Rectangle {anchors.fill: parent; anchors.margins: 2; color: "white"; border.color: "#555"}
}
