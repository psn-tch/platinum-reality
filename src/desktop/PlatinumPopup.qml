import QtQuick
import QtQuick.Controls

// A 9.2.2 pop-up menu button (as in the reference VM's Appearance panel, 2026-11-04): a raised box
// with the current choice (and its colour swatch, when it has one) and the double arrow at the
// right; clicking it opens the choices, the current one ticked. Choices: [{name, value, color}].
Item {
    id: popupButton
    property var choices: []
    property string current: ""
    signal chosen(string value)
    implicitWidth: 180; implicitHeight: 20
    width: implicitWidth; height: implicitHeight
    readonly property var currentChoice: choices.find(function(c) { return (c.value !== undefined ? c.value : c.name) === current }) || null
    Rectangle {
        anchors.fill: parent
        color: area.pressed ? "#999" : "#ddd"; border.color: popupButton.enabled ? "black" : "#888"
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: area.pressed ? "#777" : "white" }
        Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: area.pressed ? "#777" : "white" }
        Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888" }
        Rectangle {
            visible: Boolean(popupButton.currentChoice && popupButton.currentChoice.color)
            x: 6; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 12
            color: popupButton.currentChoice && popupButton.currentChoice.color ? popupButton.currentChoice.color : "transparent"
            border.color: "black"
        }
        Text {
            objectName: "popup-current"
            x: popupButton.currentChoice && popupButton.currentChoice.color ? 30 : 6
            width: parent.width - x - 24; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
            text: popupButton.currentChoice ? popupButton.currentChoice.name : popupButton.current
            textFormat: Text.PlainText; font.pixelSize: 12; font.bold: true
            color: popupButton.enabled ? "black" : "#888"
        }
        // The double arrow, set off by a line.
        Rectangle { x: parent.width - 19; y: 3; width: 1; height: parent.height - 6; color: "#888" }
        Text {
            x: parent.width - 16; width: 12; anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignHCenter; text: "⇕"; font.pixelSize: 11
            color: popupButton.enabled ? "black" : "#888"
        }
    }
    MouseArea { id: area; anchors.fill: parent; onClicked: menu.open() }
    Menu {
        id: menu
        objectName: popupButton.objectName + "-menu"
        y: popupButton.height; width: Math.max(popupButton.width, 160)
        padding: 1
        background: Rectangle { color: "#ddd"; border.color: "black" }
        Instantiator {
            model: popupButton.choices
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
            delegate: MenuItem {
                id: option
                required property var modelData
                readonly property string value: modelData.value !== undefined ? modelData.value : modelData.name
                objectName: popupButton.objectName + "-" + value
                implicitHeight: 22
                contentItem: Item {
                    Text { x: 0; anchors.verticalCenter: parent.verticalCenter; text: option.value === popupButton.current ? "✓" : ""
                           font.pixelSize: 12; color: option.highlighted ? "white" : "black" }
                    Rectangle {
                        visible: Boolean(option.modelData.color); x: 14; anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 12; color: option.modelData.color ? option.modelData.color : "transparent"; border.color: "black"
                    }
                    Text {
                        x: option.modelData.color ? 38 : 14; anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.name; textFormat: Text.PlainText; font.pixelSize: 12
                        color: option.highlighted ? "white" : "black"
                    }
                }
                background: Rectangle {
                    color: option.highlighted ? (typeof manager !== "undefined" && manager ? manager.appearance.accentColor : "#2c2994") : "transparent"
                }
                onTriggered: popupButton.chosen(value)
            }
        }
    }
}
