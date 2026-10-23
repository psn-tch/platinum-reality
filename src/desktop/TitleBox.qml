import QtQuick

// A Platinum title-bar box (close, zoom or WindowShade): the PlatinumWayland theme's 13 x 13
// drawings (titlebar/README.md). As in Mac OS 9.2.2 it tracks the mouse: it is drawn pressed in
// only while the button is held over it, and it acts only when released over it, so dragging off
// cancels.
Item {
    id: box
    required property string kind
    signal activated()
    readonly property bool down: area.pressed && area.containsMouse
    width: 13; height: 13
    Image {
        anchors.fill: parent
        source: "qrc:/desktop/src/desktop/titlebar/" + box.kind + (box.down ? "-pressed" : "-active") + ".svg"
        // Rendered at the screen's pixel size, crisp at 1.5x and 2x.
        sourceSize: Qt.size(Math.ceil(13 * Screen.devicePixelRatio), Math.ceil(13 * Screen.devicePixelRatio))
        smooth: false
    }
    MouseArea {
        id: area
        anchors.fill: parent
        // QtQuick's clicked fires only when released inside: Mac OS 9's tracking.
        onClicked: box.activated()
    }
}
