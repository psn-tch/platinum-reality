import QtQuick

// Original four-function desk accessory. Immediate arithmetic, no expression evaluator.
DesktopWindow {
    id: calculator
    objectName: "calculator-window"
    title: "Calculator"
    width: 4 * (30 + 6) - 6 + 16; height: 30 + 5 * (24 + 6) + 36
    blackFrame: true
    closeOnEscape: false
    signal copyRequested(string text)
    property string display: "0"
    property real accumulator: 0
    property string operation: ""
    property string repeatOperation: ""
    property real repeatOperand: 0
    property bool haveRepeat: false
    property bool waiting: false
    property bool fresh: true
    readonly property bool failed: display === "Error"

    function clear() {
        display="0"; accumulator=0; operation=""; repeatOperation=""
        repeatOperand=0; haveRepeat=false; waiting=false; fresh=true
    }
    function result(value) {
        if (!Number.isFinite(value)) {
            clear(); display="Error"; return false
        }
        // Bound display noise from binary floating point (for example 0.1 + 0.2).
        display=String(value===0 ? 0 : Number(value.toPrecision(15)))
        return true
    }
    function calculate(left, right, op) {
        if (op === "+") return left+right
        if (op === "-") return left-right
        if (op === "*") return left*right
        return right===0 ? NaN : left/right
    }
    function copyResult() { if (!failed) copyRequested(display) }
    function press(key) {
        if (key === "C") { clear(); return }
        if (/^[0-9.]$/.test(key)) {
            if (failed) clear()
            if (fresh || waiting) { display="0"; fresh=false; waiting=false }
            haveRepeat=false
            if (key === ".") { if (display.indexOf(".")<0) display+="."; return }
            if (display === "0") display=key
            else if (display === "-0") display="-"+key
            else if (display.replace(/[^0-9]/g,"").length<15) display+=key
            return
        }
        if (failed) return
        if (key === "backspace") {
            if (!fresh && !waiting) {
                display=display.slice(0,-1)
                if (display === "" || display === "-") display="0"
            }
        } else if (key === "sign") {
            if (waiting) { display="0"; waiting=false }
            display=display[0]==="-" ? display.slice(1) : "-"+display
            fresh=false; haveRepeat=false
        } else if (key === "%") {
            let value=Number(display)/100
            if (operation === "+" || operation === "-") value*=accumulator
            if (result(value)) { waiting=false; fresh=true; haveRepeat=false }
        } else if (["+","-","*","/"].indexOf(key)>=0) {
            if (operation!=="" && !waiting) {
                if (!result(calculate(accumulator,Number(display),operation))) return
            }
            accumulator=Number(display); operation=key; waiting=true; fresh=false; haveRepeat=false
        } else if (key === "=") {
            if (operation!=="") {
                const operand=waiting ? accumulator : Number(display)
                const op=operation
                if (!result(calculate(accumulator,operand,op))) return
                repeatOperand=operand; repeatOperation=op; haveRepeat=true; operation=""
            } else if (haveRepeat && !result(calculate(Number(display),repeatOperand,repeatOperation))) return
            fresh=true; waiting=false
        }
    }
    // Keep desktop file shortcuts away from a focused calculator. Global app
    // cycling and screenshot chords retain their normal shortcut route.
    Keys.onShortcutOverride: function(event) {
        const command=event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)
        if ((command && ((event.key>=Qt.Key_A && event.key<=Qt.Key_Z) || event.key===Qt.Key_Backspace)) ||
            (event.key===Qt.Key_F2 && event.modifiers===Qt.NoModifier))
            event.accepted=true
    }
    Keys.onEscapePressed: function(event) { clear(); event.accepted=true }
    Keys.onPressed: function(event) {
        event.accepted=true
        if (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)) {
            if (!(event.modifiers & (Qt.AltModifier | Qt.ShiftModifier))) {
                if (event.key===Qt.Key_C) copyResult()
            }
            return
        }
        if (event.modifiers & Qt.AltModifier) return
        if (event.key===Qt.Key_Return || event.key===Qt.Key_Enter || event.key===Qt.Key_Equal) press("=")
        else if (event.key===Qt.Key_Backspace) press("backspace")
        else if (event.key===Qt.Key_Delete || event.key===Qt.Key_Clear || event.key===Qt.Key_C) clear()
        else if (/^[0-9.+*/%=-]$/.test(event.text)) press(event.text)
    }
    // The desktop's Edit menu: Copy the display, Paste a number into it (9.2.2's Calculator).
    readonly property var editCommands: ({copy: !failed, paste: true, cut: false, clear: true})
    function edit(command) {
        if (command === "copy") copyResult()
        else if (command === "clear") clear()
        else if (command === "paste" && typeof manager !== "undefined" && manager && manager.clipboard)
            for (const character of String(manager.clipboard.text).trim()) if (/^[0-9.+*\/=-]$/.test(character)) press(character)
    }
    // Laid out as 9.2.2's Calculator (reference VM, 2026-11-04): the display, then C = / *,
    // 7 8 9 -, 4 5 6 +, 1 2 3 and a tall =, 0 (wide) and . The ± and % keys are on the keyboard.
    readonly property int keyWidth: 30
    readonly property int keyHeight: 24
    readonly property int gap: 6
    Rectangle {
        x: -6; y: -6; width: 4 * (calculator.keyWidth + calculator.gap) - calculator.gap; height: 26
        color: "white"; border.color: "black"
        Text {
            objectName:"calculator-display"
            anchors.fill:parent; anchors.margins:4
            text:calculator.display; textFormat:Text.PlainText
            horizontalAlignment:Text.AlignRight; verticalAlignment:Text.AlignVCenter
            font.pixelSize:calculator.display.length>13?11:14; elide:Text.ElideLeft
        }
    }
    Item {
        x: -6; y: 30
        Repeater {
            // [key, label, column, row, columns wide, rows tall]
            model:[["C","C",0,0,1,1],["=","=",1,0,1,1],["/","/",2,0,1,1],["*","*",3,0,1,1],
                   ["7","7",0,1,1,1],["8","8",1,1,1,1],["9","9",2,1,1,1],["-","-",3,1,1,1],
                   ["4","4",0,2,1,1],["5","5",1,2,1,1],["6","6",2,2,1,1],["+","+",3,2,1,1],
                   ["1","1",0,3,1,1],["2","2",1,3,1,1],["3","3",2,3,1,1],["enter","=",3,3,1,2],
                   ["0","0",0,4,2,1],[".",".",2,4,1,1]]
            Rectangle {
                required property var modelData
                readonly property string key: modelData[0] === "enter" ? "=" : modelData[0]
                objectName:"calculator-key-"+modelData[0]
                x: modelData[2] * (calculator.keyWidth + calculator.gap)
                y: modelData[3] * (calculator.keyHeight + calculator.gap)
                width: modelData[4] * calculator.keyWidth + (modelData[4] - 1) * calculator.gap
                height: modelData[5] * calculator.keyHeight + (modelData[5] - 1) * calculator.gap
                color: keyMouse.pressed ? "#888" : "white"; border.color: "black"
                // A key's shadow below and to the right, as 9.2.2 draws them.
                Rectangle { z: -1; x: 2; y: 2; width: parent.width; height: parent.height; color: "black" }
                Text { anchors.centerIn:parent; text:modelData[1]; textFormat:Text.PlainText; font.pixelSize:12; color: keyMouse.pressed ? "white" : "black" }
                MouseArea {
                    id:keyMouse; anchors.fill:parent
                    onPressed:{calculator.raised();calculator.forceActiveFocus()}
                    onClicked:calculator.press(parent.key)
                }
            }
        }
    }
}
