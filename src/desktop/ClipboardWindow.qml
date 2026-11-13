import QtQuick

// Mac OS 9's Clipboard window (Edit > Show Clipboard): modeless and read-only. A strip
// under the title bar names what the clipboard holds, "text" or "picture" (or "none"),
// and the contents follow, as in the 9.2.2 Finder and SimpleText. It updates as the
// clipboard changes. The title bar moves it; the close box or Escape closes it. Its
// zoom and collapse boxes are not drawn: they would be controls that do nothing.
// Recent Clippings (Lily Hewitt, 2026-11-12: "copy multiple things and be able to switch between
// them") lists the last copies on the left, newest first; clicking one makes it the clipboard again.
FocusScope {
    id: window
    property var moveManager: typeof manager === "undefined" ? null : manager
    onActiveFocusChanged: if (!activeFocus && moveManager) moveManager.finishShellWindowMove(window, false)
    // On workspaces like an application's window (WorkspaceMember.qml); hidden while away.
    readonly property Item workspaceWindow: window
    readonly property WorkspaceMember workspaceMember: WorkspaceMember {window: workspaceWindow; name: "Clipboard"}
    opacity: workspaceMember.away ? 0 : 1
    enabled: !workspaceMember.away
    required property var clipboard
    signal closeRequested()
    signal raised()
    objectName: "clipboard-window"
    width: 740
    height: 260
    readonly property int listWidth: 200
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
            onPressed: function(mouse) {
                window.raised(); window.forceActiveFocus()
                if (window.moveManager) window.moveManager.beginShellWindowMove(window, mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: if (window.moveManager) window.moveManager.finishShellWindowMove(window)
            onCanceled: if (window.moveManager) window.moveManager.finishShellWindowMove(window, false)
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
    // As in 9.2.2 (checked in Lily Hewitt's VM): the description sits in a grey strip with an inset
    // edge, above a white pane where the contents are indented.
    Rectangle {
        id: listStrip
        x: 3; y: titleBar.y + titleBar.height + 2; width: window.listWidth; height: 20
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Text {
            x: 8; anchors.verticalCenter: parent.verticalCenter
            text: "Recent Clippings"; textFormat: Text.PlainText; font.pixelSize: 11; font.bold: true
        }
    }
    Rectangle {
        id: listWell
        x: 3; y: listStrip.y + listStrip.height - 1; width: window.listWidth; height: parent.height - y - 3
        color: "white"; border.color: "#222"
        ListView {
            id: clippings
            objectName: "clipboard-history"
            x: 1; y: 1; width: parent.width - 2; height: parent.height - 2
            clip: true; boundsBehavior: Flickable.StopAtBounds
            model: window.clipboard.history || []
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "clipboard-history-" + index
                width: clippings.width; height: 26
                color: modelData.current ? "#ccccff" : rowMouse.pressed ? "#e5e5ef" : "white"
                Image {
                    id: thumbnail
                    x: 6; anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20; fillMode: Image.PreserveAspectFit
                    visible: row.modelData.kind === "picture" && source !== ""
                    source: row.modelData.thumbnail || ""
                }
                Text {
                    x: thumbnail.visible ? 32 : 8; width: parent.width - x - 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label; textFormat: Text.PlainText; elide: Text.ElideRight
                    font.pixelSize: 11; font.bold: row.modelData.current
                }
                Rectangle {x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#eee"}
                MouseArea {
                    id: rowMouse; anchors.fill: parent
                    onPressed: function(mouse) { window.raised(); window.forceActiveFocus(); }
                    onClicked: window.clipboard.restore(row.index)
                }
            }
            Text {
                visible: clippings.count === 0
                x: 8; y: 8; width: parent.width - 16; wrapMode: Text.Wrap
                text: "Nothing copied yet."; color: "#666"; font.pixelSize: 11; textFormat: Text.PlainText
            }
        }
    }
    Rectangle {
        id: strip
        x: listStrip.x + listStrip.width + 4; y: titleBar.y + titleBar.height + 2; width: parent.width - x - 3; height: 20
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
        x: strip.x; y: strip.y + strip.height - 1; width: strip.width; height: parent.height - y - 3
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
