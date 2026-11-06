import QtQuick
import QtQuick.Controls

// Key Caps, the desk accessory, laid out as 9.2.2's (captured in the reference VM, 2026-11-04):
// an extended keyboard of dark keys with white labels showing what each key types in the layout
// applications are given; modifier and function keys are blank. Hold Shift or Option and the keys
// show what they type then, dead keys (accents) outlined. A key held down is drawn pressed. What
// is typed, or clicked, appears in the strip above.
DesktopWindow {
    id: keyCaps
    objectName: "key-caps-window"
    title: "Key Caps"
    readonly property real unit: 28
    width: 23.75 * unit + 28; height: 7.6 * unit + 86
    required property var caps
    property bool shift: false
    property bool option: false
    property string typed: ""
    // Linux key codes held down now.
    property var held: ({})
    readonly property var labels: caps ? (caps.layout, caps.labels(shift, option)) : ({})
    readonly property var dead: caps ? (caps.layout, caps.deadKeys(shift, option)) : ({})
    // The numeric keypad types the same whatever the layout.
    readonly property var keypad: ({55: "*", 71: "7", 72: "8", 73: "9", 74: "-", 75: "4", 76: "5", 77: "6",
                                    78: "+", 79: "1", 80: "2", 81: "3", 82: "0", 83: ".", 98: "/", 117: "="})
    function type(text) { if (text) typed = (typed + text).slice(-120) }
    function hold(code, down) {
        if (code < 0) return
        const next = Object.assign({}, held)
        if (down) next[code] = true; else delete next[code]
        held = next
    }
    function codeFor(event) {
        if (event.nativeScanCode > 8) return event.nativeScanCode - 8
        // Without a scan code (tests), the key whose label is the text typed.
        for (const code in labels) if (labels[code] === event.text) return Number(code)
        return event.key === Qt.Key_Space ? 57 : event.key === Qt.Key_Shift ? 42 : event.key === Qt.Key_Alt ? 56 : -1
    }
    onVisibleChanged: if (!visible) { shift = false; option = false; held = ({}) }
    Keys.onShortcutOverride: function(event) {
        // Plain typing stays here; Command chords keep their usual meaning.
        if (!(event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)) && event.text.length > 0) event.accepted = true
    }
    Keys.onPressed: function(event) {
        shift = Boolean(event.modifiers & Qt.ShiftModifier) || event.key === Qt.Key_Shift
        option = Boolean(event.modifiers & Qt.AltModifier) || event.key === Qt.Key_Alt || event.key === Qt.Key_AltGr
        if (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)) return
        hold(codeFor(event), true)
        if (event.key === Qt.Key_Backspace) typed = typed.slice(0, -1)
        else if (event.text.length > 0 && event.text.charCodeAt(0) >= 32) type(event.text)
        event.accepted = true
    }
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Shift) shift = false
        else if (event.key === Qt.Key_Alt || event.key === Qt.Key_AltGr) option = false
        hold(codeFor(event), false)
        event.accepted = true
    }
    Rectangle {
        x: 2.5 * keyCaps.unit; width: parent.width - 5 * keyCaps.unit; height: 30
        color: "white"; border.color: "black"; border.width: 2
        Text {
            objectName: "key-caps-typed"
            anchors.fill: parent; anchors.margins: 6; verticalAlignment: Text.AlignVCenter
            text: keyCaps.typed; textFormat: Text.PlainText; font.pixelSize: 14; elide: Text.ElideLeft
        }
    }
    // [code, x, row, width, height] in keys; row 0 is the function keys.
    readonly property var layoutKeys: {
        const keys = [[1, 0, 0, 1, 1]]
        const fn = [[59, 2], [60, 3], [61, 4], [62, 5], [63, 6.5], [64, 7.5], [65, 8.5], [66, 9.5],
                    [67, 11], [68, 12], [87, 13], [88, 14], [99, 15.75], [70, 16.75], [119, 17.75]]
        for (const [code, x] of fn) keys.push([code, x, 0, 1, 1])
        const row = function(r, start, codes) { codes.forEach(function(c, i) { keys.push([c, start + i, r, 1, 1]) }) }
        row(1.5, 0, [41, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]); keys.push([14, 13, 1.5, 2, 1])
        keys.push([15, 0, 2.5, 1.5, 1]); row(2.5, 1.5, [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27]); keys.push([43, 13.5, 2.5, 1.5, 1])
        keys.push([58, 0, 3.5, 1.75, 1]); row(3.5, 1.75, [30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40]); keys.push([28, 12.75, 3.5, 2.25, 1])
        keys.push([42, 0, 4.5, 2.25, 1]); row(4.5, 2.25, [44, 45, 46, 47, 48, 49, 50, 51, 52, 53]); keys.push([54, 12.25, 4.5, 2.75, 1])
        for (const k of [[29, 0, 1.5], [56, 1.5, 1.25], [125, 2.75, 1.5], [57, 4.25, 6.25], [126, 10.5, 1.5], [100, 12, 1.25], [97, 13.25, 1.75]])
            keys.push([k[0], k[1], 5.5, k[2], 1])
        // Help/Insert, Home, Page Up; Delete, End, Page Down; the arrows.
        row(1.5, 15.75, [110, 102, 104]); row(2.5, 15.75, [111, 107, 109])
        keys.push([103, 16.75, 4.5, 1, 1]); row(5.5, 15.75, [105, 108, 106])
        // The keypad.
        row(1.5, 19.75, [69, 117, 98, 55]); row(2.5, 19.75, [71, 72, 73, 74]); row(3.5, 19.75, [75, 76, 77, 78])
        row(4.5, 19.75, [79, 80, 81]); keys.push([96, 22.75, 4.5, 1, 2]); keys.push([82, 19.75, 5.5, 2, 1]); keys.push([83, 21.75, 5.5, 1, 1])
        return keys
    }
    Item {
        id: board
        y: 44; width: 23.75 * keyCaps.unit; height: 6.5 * keyCaps.unit
        Repeater {
            model: keyCaps.layoutKeys
            Rectangle {
                id: key
                required property var modelData
                readonly property int code: modelData[0]
                readonly property bool down: keyCaps.held[code] === true || keyMouse.pressed
                    || (keyCaps.shift && (code === 42 || code === 54)) || (keyCaps.option && (code === 56 || code === 100))
                readonly property bool deadKey: keyCaps.dead[String(code)] !== undefined
                readonly property string label: deadKey ? keyCaps.dead[String(code)]
                    : keyCaps.keypad[code] !== undefined ? keyCaps.keypad[code] : (keyCaps.labels[String(code)] || "")
                objectName: "key-caps-key-" + code
                x: modelData[1] * keyCaps.unit; y: modelData[2] * keyCaps.unit
                width: modelData[3] * keyCaps.unit - 2; height: modelData[4] * keyCaps.unit - 2
                color: down ? "#bbb" : "#444"; border.color: "black"
                // Raised: light above and left, dark below and right; flat while pressed.
                Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: key.down ? "#999" : "#777" }
                Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: key.down ? "#999" : "#777" }
                Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: key.down ? "#ddd" : "#222" }
                Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: key.down ? "#ddd" : "#222" }
                // A dead key's accent sits in an outlined box.
                Rectangle {
                    visible: key.deadKey; anchors.centerIn: parent; width: 16; height: 16
                    color: "transparent"; border.color: key.down ? "black" : "white"
                }
                Text {
                    anchors.centerIn: parent
                    text: key.label; textFormat: Text.PlainText
                    font.pixelSize: 13; font.bold: true
                    color: key.down ? "black" : "white"
                }
                MouseArea {
                    id: keyMouse
                    anchors.fill: parent
                    onClicked: {
                        keyCaps.forceActiveFocus()
                        if (key.code === 14) keyCaps.typed = keyCaps.typed.slice(0, -1)
                        else if (key.code === 57) keyCaps.type(" ")
                        else if (key.code === 42 || key.code === 54) keyCaps.shift = !keyCaps.shift
                        else if (key.code === 56 || key.code === 100) keyCaps.option = !keyCaps.option
                        else if (!key.deadKey) keyCaps.type(key.label)
                    }
                }
            }
        }
    }
    Text {
        y: board.y + board.height + 8
        width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
        text: "Layout: " + (keyCaps.caps ? keyCaps.caps.layout.toUpperCase() : "") + ". Hold Shift or Option to see what the keys type with them."
    }
}
