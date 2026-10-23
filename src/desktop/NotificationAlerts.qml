import QtQuick

// Notification Manager alerts (docs/tray-and-notifications.md): small Platinum utility windows
// stacked at the top right below the menu bar. A click on the body chooses the sender's default
// action; the close box dismisses.
Item {
    id: stack
    required property var server
    objectName: "notification-alerts"
    width: 300
    height: column.height
    visible: alerts.count > 0
    Column {
        id: column
        width: parent.width
        spacing: 8
        Repeater {
            id: alerts
            model: stack.server ? stack.server.alerts : []
            delegate: Rectangle {
                id: alert
                required property var modelData
                objectName: "notification-" + modelData.id
                width: column.width
                height: 22 + Math.max(40, texts.height + 14) + (buttons.visible ? buttons.height + 8 : 0)
                color: "#ddd"
                border.color: "#222"
                Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
                Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
                Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}
                Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
                // Title bar: pinstripes, a close box and the sending application's name.
                Item {
                    id: titleBar
                    x: 3; y: 3; width: parent.width - 6; height: 16
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            x: 20; y: 2 + index * 3; width: titleBar.width - 24; height: 1; color: "#888"
                        }
                    }
                    Rectangle {
                        objectName: "notification-close-" + alert.modelData.id
                        x: 2; y: 2; width: 12; height: 12
                        color: closeMouse.pressed ? "#888" : "#ddd"; border.color: "#222"
                        Rectangle {x: 1; y: 1; width: 9; height: 1; color: closeMouse.pressed ? "#666" : "white"}
                        Rectangle {x: 1; y: 1; width: 1; height: 9; color: closeMouse.pressed ? "#666" : "white"}
                        MouseArea {id: closeMouse; anchors.fill: parent; onClicked: stack.server.dismiss(alert.modelData.id)}
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.min(titleBar.width - 48, application.implicitWidth + 12); height: 16; color: "#ddd"
                        Text {
                            id: application
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, titleBar.width - 60)
                            text: alert.modelData.application !== "" ? alert.modelData.application : "Notification"
                            textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 12
                        }
                    }
                }
                MouseArea {
                    objectName: "notification-body-" + alert.modelData.id
                    x: 3; y: 22; width: parent.width - 6; height: parent.height - 25
                    enabled: alert.modelData.hasDefault
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: stack.server.invoke(alert.modelData.id, "default")
                }
                Image {
                    x: 12; y: 28; width: 32; height: 32
                    source: alert.modelData.icon
                    visible: alert.modelData.icon !== ""
                    sourceSize: Qt.size(64, 64)
                    smooth: true; fillMode: Image.PreserveAspectFit
                }
                MenuGlyph {x: 20; y: 36; kind: "application"; visible: alert.modelData.icon === ""}
                Column {
                    id: texts
                    x: 54; y: 28; width: alert.width - 66
                    spacing: 3
                    Text {
                        width: parent.width
                        text: alert.modelData.summary
                        textFormat: Text.PlainText; font.pixelSize: 12; font.bold: true
                        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: alert.modelData.body
                        textFormat: Text.PlainText; font.pixelSize: 12
                        wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                    }
                }
                Row {
                    id: buttons
                    anchors.right: parent.right; anchors.rightMargin: 10
                    y: 22 + Math.max(40, texts.height + 14)
                    spacing: 8
                    visible: alert.modelData.actions.length > 0
                    Repeater {
                        model: alert.modelData.actions
                        delegate: PlatinumButton {
                            required property var modelData
                            objectName: "notification-action-" + modelData.key
                            text: modelData.label
                            implicitHeight: 22
                            onClicked: stack.server.invoke(alert.modelData.id, modelData.key)
                        }
                    }
                }
            }
        }
    }
}
