import QtQuick
import QtQuick.Controls

// A Platinum slider (the track and grooved thumb of the Sound and Quick Controls sliders), with
// labels at its ends. `committed` fires once a change is finished: on release, or a moment after
// the last arrow key or wheel step, so a setting is saved once rather than on every step.
Slider {
    id: slider
    property string lowLabel: ""
    property string highLabel: ""
    signal committed(real value)
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    readonly property var sounds: typeof manager !== "undefined" && manager ? manager.interfaceSounds : null
    implicitWidth: 260; implicitHeight: lowLabel || highLabel ? 40 : 26
    wheelEnabled: true
    onPressedChanged: {
        if (pressed) { if (sounds) sounds.play("sltp"); later.stop() }
        else committed(value)
    }
    onMoved: if (!pressed) later.restart()
    Timer { id: later; interval: 300; onTriggered: slider.committed(slider.value) }
    background: Item {
        x: slider.leftPadding; y: 0; width: slider.availableWidth; height: slider.height
        Rectangle {
            y: 10; width: parent.width; height: 6
            color: slider.enabled ? "#aaa" : "#ccc"; border.color: slider.enabled ? "#666" : "#999"
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "#777"; visible: slider.enabled }
        }
        Text { y: 26; text: slider.lowLabel; font.pixelSize: 11; color: "#444" }
        Text { y: 26; anchors.right: parent.right; text: slider.highLabel; font.pixelSize: 11; color: "#444" }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: 2; width: 12; height: 22
        color: slider.pressed ? "#aaa" : "#ddd"; border.color: slider.enabled ? "#333" : "#999"
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "white" }
        Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: "white" }
        Rectangle { x: 4; y: 5; width: 1; height: 11; color: "#888" }
        Rectangle { x: 7; y: 5; width: 1; height: 11; color: "#888" }
        Rectangle { anchors.fill: parent; anchors.margins: -3; color: "transparent"; border.color: "#555"; visible: slider.activeFocus }
    }
}
