import QtQuick
import "../../src/desktop" as Platinum

Rectangle {
    id: root
    width: 920; height: 740; color: "#718d99"
    QtObject {
        id: session
        objectName: "fixture-session"
        property rect primaryDisplayRect: Qt.rect(0, 24, root.width, root.height-24)
        property int activeTokens: 0
        property int nextToken: 0
        readonly property bool shellModal: activeTokens > 0
        function beginShellModal() { activeTokens++; return ++nextToken }
        function endShellModal(token) { if (token) activeTokens-- }
    }
    Rectangle {
        width: parent.width; height: 24; color: "#eee"; border.color: "#333"
        Text { x: 20; anchors.verticalCenter: parent.verticalCenter; text: "Sound control panel — development preview"; font.pixelSize: 12 }
    }
    Platinum.SoundPanel { id: panel; sessionManager: session; service: audioService }
    Component.onCompleted: Qt.callLater(panel.edit)
}
