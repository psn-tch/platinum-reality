import QtQuick

// Mac OS 9's Clipboard window (Edit > Show Clipboard): modeless and read-only. A strip
// under the title bar names what the clipboard holds, "text" or "picture" (or "none"),
// and the contents follow, as in the 9.2.2 Finder and SimpleText. It updates as the
// clipboard changes. The title bar moves it; the close box or Escape closes it. Its
// zoom and collapse boxes are not drawn: they would be controls that do nothing.
FocusScope {
    id: window
    required property var clipboard
    signal closeRequested()
    signal raised()
    objectName: "clipboard-window"
    width: 560
    height: 240
    Keys.onEscapePressed: window.closeRequested()
    Rectangle {
        anchors.fill: parent
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
    }
    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) { window.raised(); window.forceActiveFocus(); mouse.accepted = false }
    }
    Item {
        id: titleBar
        x: 3; y: 3; width: parent.width - 6; height: 18
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 20; y: 3 + index * 3; width: titleBar.width - 24; height: 1
                color: window.activeFocus ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill: parent
            drag.target: window; drag.threshold: 2
            onPressed: { window.raised(); window.forceActiveFocus() }
        }
        Rectangle {
            objectName: "clipboard-close"
            x: 2; y: 3; width: 12; height: 12
            color: closeMouse.pressed ? "#888" : "#ddd"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: 9; height: 1; color: closeMouse.pressed ? "#666" : "white"}
            Rectangle {x: 1; y: 1; width: 1; height: 9; color: closeMouse.pressed ? "#666" : "white"}
            MouseArea {id: closeMouse; anchors.fill: parent; onClicked: window.closeRequested()}
        }
        Rectangle {
            anchors.centerIn: parent
            width: title.implicitWidth + 12; height: 18; color: "#ddd"
            Text {id: title; anchors.centerIn: parent; text: "Clipboard"; font.pixelSize: 12; font.bold: true}
        }
    }
    // As in 9.2.2 (checked in psn_tch's VM): the description sits in a grey strip with an inset
    // edge, above a white pane where the contents are indented.
    Rectangle {
        id: strip
        x: 3; y: titleBar.y + titleBar.height + 2; width: parent.width - 6; height: 20
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Text {
            id: description
            objectName: "clipboard-description"
            x: 8; anchors.verticalCenter: parent.verticalCenter
            text: "Clipboard contents: " + window.clipboard.kind
            textFormat: Text.PlainText; font.pixelSize: 11; font.weight: Font.Normal
        }
    }
    Rectangle {
        id: well
        x: 3; y: strip.y + strip.height - 1; width: parent.width - 6; height: parent.height - y - 3
        color: "white"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "#222"}
        Flickable {
            id: contents
            x: 22; y: 14; width: parent.width - 30; height: parent.height - y - 4
            clip: true
            contentWidth: window.clipboard.kind === "picture" ? picture.width : width
            contentHeight: window.clipboard.kind === "picture" ? picture.height : text.height
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: text
                objectName: "clipboard-text"
                visible: window.clipboard.kind === "text"
                width: contents.width
                text: window.clipboard.text
                textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
                font.pixelSize: 12; font.weight: Font.Normal
            }
            Image {
                id: picture
                objectName: "clipboard-picture"
                visible: window.clipboard.kind === "picture"
                source: window.clipboard.kind === "picture" ? window.clipboard.picture : ""
                asynchronous: true
            }
        }
    }
}
