import QtQuick
import QtQuick.Controls

// A Platinum slider, drawn as Mac OS 9.2.2 draws one (measured in the reference VM's General
// Controls, Mouse and Keyboard panels, 2026-11-04): a grooved track (#aaa between black edges,
// a white line under it), a thumb in the Appearance colour that points down at its tick marks,
// and the labels centred under the ticks. `committed` fires once a change is finished: on
// release, or a moment after the last arrow key or wheel step, so a setting is saved once
// rather than on every step.
Slider {
    id: slider
    property string lowLabel: ""
    property string highLabel: ""
    // Tick marks under the track (0: none). With tickLabels, one label per tick ("" for none);
    // otherwise lowLabel and highLabel sit under the two ends.
    property int tickCount: 0
    property var tickLabels: []
    signal committed(real value)
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    readonly property var sounds: typeof manager !== "undefined" && manager ? manager.interfaceSounds : null
    readonly property bool graphite: typeof manager !== "undefined" && manager && manager.appearance
                                     ? manager.appearance.accent === "graphite" : false
    readonly property bool labelled: lowLabel !== "" || highLabel !== "" || tickLabels.length > 0
    readonly property int thumbWidth: 15
    readonly property int tickTop: 19
    implicitWidth: 260
    implicitHeight: labelled ? 40 : tickCount > 0 ? 26 : 20
    wheelEnabled: true
    onPressedChanged: {
        if (pressed) { if (sounds) sounds.play("sltp"); later.stop() }
        else committed(value)
    }
    onMoved: if (!pressed) later.restart()
    Timer { id: later; interval: 300; onTriggered: slider.committed(slider.value) }
    // The centre of the thumb at a position from 0 to 1, in the slider's coordinates.
    function centreAt(position) {
        return leftPadding + position * (availableWidth - thumbWidth) + thumbWidth / 2
    }
    background: Item {
        x: 0; y: 0; width: slider.width; height: slider.height
        // The groove: black top and bottom edges, #aaa inside, a white line below.
        Rectangle {
            x: slider.leftPadding; y: 4; width: slider.availableWidth; height: 5; radius: 2
            color: slider.enabled ? "#aaa" : "#ccc"; border.color: slider.enabled ? "#222" : "#999"
        }
        Rectangle { x: slider.leftPadding + 2; y: 9; width: slider.availableWidth - 4; height: 1; color: "white" }
        Repeater {
            model: slider.tickCount
            Rectangle {
                required property int index
                x: Math.round(slider.centreAt(slider.tickCount > 1 ? index / (slider.tickCount - 1) : 0.5))
                y: slider.tickTop; width: 1; height: 6
                color: slider.enabled ? "black" : "#999"
            }
        }
        Repeater {
            model: slider.tickLabels.length > 0 ? slider.tickLabels
                 : [slider.lowLabel, slider.highLabel]
            Text {
                required property string modelData
                required property int index
                readonly property real at: slider.tickLabels.length > 0
                    ? (slider.tickLabels.length > 1 ? index / (slider.tickLabels.length - 1) : 0.5) : index
                // Centred under its tick, but never past the slider's ends.
                x: Math.max(0, Math.min(slider.width - implicitWidth,
                                        slider.centreAt(at) - implicitWidth / 2))
                y: slider.tickCount > 0 ? 27 : 22
                text: modelData; textFormat: Text.PlainText
                font.pixelSize: 11; color: slider.enabled ? "black" : "#888"
            }
        }
    }
    handle: Canvas {
        id: thumb
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: 0; width: slider.thumbWidth; height: 16
        // Blue-violet by default, grey with the Graphite appearance; darker while held.
        readonly property var tones: slider.graphite
            ? (slider.pressed ? ["#bbb", "#888", "#555", "#222"] : ["#ddd", "#aaa", "#777", "#333"])
            : (slider.pressed ? ["#aaf", "#77d", "#449", "#226"] : ["#ccf", "#99f", "#66c", "#339"])
        onTonesChanged: requestPaint()
        onEnabledChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"), w = width, h = height, shoulder = 9
            c.clearRect(0, 0, w, h)
            const outline = function() {
                c.beginPath()
                c.moveTo(0.5, 0.5); c.lineTo(w - 0.5, 0.5); c.lineTo(w - 0.5, shoulder)
                c.lineTo(w / 2, h - 0.5); c.lineTo(0.5, shoulder); c.closePath()
            }
            const t = slider.enabled ? tones : ["#eee", "#ddd", "#bbb", "#999"]
            outline(); c.fillStyle = t[1]; c.fill()
            // Light top-left edge, dark bottom-right edges.
            c.fillStyle = t[0]; c.fillRect(1, 1, w - 2, 1); c.fillRect(1, 1, 1, shoulder - 1)
            c.strokeStyle = t[2]; c.lineWidth = 1
            c.beginPath(); c.moveTo(w - 1.5, 1.5); c.lineTo(w - 1.5, shoulder); c.lineTo(w / 2, h - 2); c.stroke()
            // Three grips, each with a light line on its left.
            for (const x of [5, 7, 9]) {
                c.fillStyle = t[0]; c.fillRect(x - 1, 3, 1, 6)
                c.fillStyle = t[3]; c.fillRect(x, 3, 1, 6)
            }
            outline(); c.strokeStyle = slider.enabled ? "black" : "#999"; c.stroke()
        }
        Rectangle {
            x: -3; y: -3; width: parent.width + 6; height: parent.height + 6
            color: "transparent"; border.color: "#555"; visible: slider.activeFocus
        }
    }
}
