import QtQuick
import QtQuick.Controls

// One sticky note (StickyNotes model row). The desktop owns it, as it owns Control Panels: in front
// it is above application windows and has the keyboard; behind, it sits below them. Drag it by its
// top strip; double-click the strip, or click its box at the right, to roll it up to the strip (its
// first line shows there); drag the corner to resize; the close box deletes it (asking first when
// it has text). Right-click for colors, text size, Keep in Front and Delete Note….
FocusScope {
    id: note
    property var moveManager: typeof manager === "undefined" ? null : manager
    onFrontChanged: if (!front && moveManager) moveManager.finishShellWindowMove(note, false)
    required property var notes
    required property int noteId
    required property string noteText
    required property int noteX
    required property int noteY
    required property int noteWidth
    required property int noteHeight
    required property string noteColor
    required property string noteTextSize
    required property bool noteCollapsed
    required property bool noteFloating
    required property int noteOrder
    signal raised()
    // The close box, Command-W or Delete Note…: the desktop asks first when there is text.
    signal deleteRequested(int id, bool empty)
    signal newNoteRequested()
    objectName: "sticky-note-" + noteId
    x: noteX; y: noteY
    // The size as drawn: the grow box changes these, so the roll-up binding below stays intact.
    property int liveWidth: noteWidth
    property int liveHeight: noteHeight
    width: liveWidth
    height: noteCollapsed ? strip.height + 2 : liveHeight
    readonly property alias text: edit.text
    readonly property bool front: focusTracker.front
    // Body, then its darker shade (the front note's border, strip pattern and boxes), as 9.2.2's
    // Stickies draws them (measured in the reference VM, 2026-11-04: yellow, green, pink; purple,
    // blue and gray by the same rule).
    readonly property var palette: ({
        yellow: ["#ffff99", "#ffcc00"], blue: ["#ccffff", "#66cccc"], green: ["#ccffcc", "#66ff66"],
        pink: ["#ffcccc", "#ff9999"], purple: ["#ccccff", "#9999ff"], gray: ["#eeeeee", "#aaaaaa"]
    })
    readonly property var colors: palette[noteColor] || palette.yellow
    readonly property int fontSize: noteTextSize === "small" ? 11 : noteTextSize === "large" ? 16 : 13
    function bringForward() { notes.raise(noteId); raised() }
    ShellWindowFocus { id: focusTracker; window: note }
    // Command-W closes the note, Command-N makes another. The text field sees keys first and would
    // type the letter of a Command chord from a real keyboard (it only refuses Ctrl chords), so
    // the field hands them here before typing (Keys runs before the item); other Command letters
    // type nothing. Cut, Copy, Paste, Select All and Undo are the desktop's (ShellTextCommands).
    function commandKey(event) {
        const command = event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)
        if (!command)
            return
        if (event.key === Qt.Key_W) { event.accepted = true; note.deleteRequested(note.noteId, edit.text === "") }
        else if (event.key === Qt.Key_N && !(event.modifiers & Qt.ShiftModifier)) { event.accepted = true; note.newNoteRequested() }
        else if ((event.modifiers & Qt.MetaModifier) && event.text !== "") event.accepted = true
    }
    Keys.onPressed: function(event) { note.commandKey(event) }
    // Shadow, as for every Platinum window.
    Rectangle { z: -1; x: 3; y: 3; width: parent.width; height: parent.height; color: "#50000000" }
    // In front, a border in the note's shade; behind, the note alone, as in 9.2.2.
    Rectangle {
        anchors.fill: parent
        color: note.colors[0]; border.color: note.front ? note.colors[1] : "#888"
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            note.bringForward(); note.forceActiveFocus()
            if (mouse.button === Qt.RightButton) { menu.popup(); mouse.accepted = true }
            else mouse.accepted = false
        }
    }
    Rectangle {
        id: strip
        objectName: "sticky-strip-" + note.noteId
        x: 1; y: 1; width: parent.width - 2; height: 14
        color: note.colors[0]
        // The front note's strip: a fine checkerboard of the note's colour and its shade.
        Canvas {
            id: stripPattern
            anchors.fill: parent; visible: note.front
            onPaint: {
                const c = getContext("2d")
                c.fillStyle = note.colors[0]; c.fillRect(0, 0, width, height)
                c.fillStyle = note.colors[1]
                for (let y = 1; y < height - 1; ++y)
                    for (let x = (y % 2); x < width; x += 2) c.fillRect(x, y, 1, 1)
            }
            onWidthChanged: requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onAvailableChanged: if (available) requestPaint()
            Connections { target: note; function onColorsChanged() { stripPattern.requestPaint() } }
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                note.bringForward(); note.forceActiveFocus()
                if (mouse.button === Qt.RightButton) menu.popup()
                else if (note.moveManager) note.moveManager.beginShellWindowMove(note, mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: {
                if (note.moveManager) note.moveManager.finishShellWindowMove(note)
                if (note.x !== note.noteX || note.y !== note.noteY)
                    note.notes.move(note.noteId, Math.round(note.x), Math.round(note.y))
                note.x = Qt.binding(function() { return note.noteX })
                note.y = Qt.binding(function() { return note.noteY })
            }
            onCanceled: if (note.moveManager) note.moveManager.finishShellWindowMove(note, false)
            onDoubleClicked: note.notes.set(note.noteId, "collapsed", !note.noteCollapsed)
        }
        // The close box.
        Rectangle {
            objectName: "sticky-close-" + note.noteId
            // Only the front note shows its boxes; a click elsewhere first brings a note forward.
            x: 3; y: 2; width: 10; height: 10; visible: note.front
            color: closeMouse.pressed ? note.colors[1] : note.colors[0]; border.color: note.colors[1]
            MouseArea { id: closeMouse; anchors.fill: parent; onClicked: note.deleteRequested(note.noteId, edit.text === "") }
        }
        // Collapsed, the first line shows in the strip.
        Text {
            objectName: "sticky-title-" + note.noteId
            x: 18; width: parent.width - 36; anchors.verticalCenter: parent.verticalCenter
            visible: note.noteCollapsed
            text: edit.text.split("\n")[0]; textFormat: Text.PlainText; elide: Text.ElideRight
            font.pixelSize: 10; color: "#111"
        }
        // The WindowShade box: rolls the note up to its strip and down again.
        Rectangle {
            objectName: "sticky-shade-" + note.noteId
            x: parent.width - 13; y: 2; width: 10; height: 10; visible: note.front
            color: shadeMouse.pressed ? note.colors[1] : note.colors[0]; border.color: note.colors[1]
            Rectangle { x: 2; y: 4; width: 6; height: 1; color: note.colors[1] }
            MouseArea { id: shadeMouse; anchors.fill: parent; onClicked: note.notes.set(note.noteId, "collapsed", !note.noteCollapsed) }
        }
    }
    Flickable {
        id: page
        visible: !note.noteCollapsed
        x: 6; y: strip.y + strip.height + 4; width: parent.width - 12; height: parent.height - y - 14
        clip: true
        contentWidth: width; contentHeight: edit.contentHeight + 4
        boundsBehavior: Flickable.StopAtBounds
        function ensureVisible(r) {
            if (contentY >= r.y) contentY = r.y
            else if (contentY + height <= r.y + r.height) contentY = r.y + r.height - height
        }
        TextEdit {
            id: edit
            objectName: "sticky-text-" + note.noteId
            width: page.width
            focus: true
            wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; selectByMouse: true
            font.pixelSize: note.fontSize; color: "#111"
            selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"; selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
            onCursorRectangleChanged: page.ensureVisible(cursorRectangle)
            Keys.onPressed: function(event) { note.commandKey(event) }
            Component.onCompleted: text = note.noteText
            onTextChanged: note.notes.setText(note.noteId, text)
        }
    }
    // The grow box: drag to resize.
    Item {
        objectName: "sticky-grow-" + note.noteId
        visible: !note.noteCollapsed
        x: parent.width - 13; y: parent.height - 13; width: 12; height: 12
        // 9.2.2's: a step in the note's shade at the corner, shown on the note in front.
        Rectangle { visible: note.front; x: 5; y: 1; width: 6; height: 10; color: "transparent"; border.color: note.colors[1] }
        Rectangle { visible: note.front; x: 1; y: 5; width: 10; height: 6; color: "transparent"; border.color: note.colors[1] }
        Rectangle { visible: note.front; x: 6; y: 6; width: 4; height: 4; color: note.colors[0] }
        MouseArea {
            id: growMouse
            anchors.fill: parent; anchors.margins: -2
            cursorShape: Qt.SizeFDiagCursor
            property point start
            property size from
            onPressed: function(mouse) {
                note.bringForward(); note.forceActiveFocus()
                start = mapToItem(note.parent, mouse.x, mouse.y); from = Qt.size(note.width, note.height)
            }
            onPositionChanged: function(mouse) {
                const now = mapToItem(note.parent, mouse.x, mouse.y)
                note.liveWidth = Math.max(120, Math.min(2000, Math.round(from.width + now.x - start.x)))
                note.liveHeight = Math.max(80, Math.min(2000, Math.round(from.height + now.y - start.y)))
            }
            onReleased: note.notes.resize(note.noteId, note.liveWidth, note.liveHeight)
        }
    }
    Menu {
        id: menu
        objectName: "sticky-menu-" + note.noteId
        background: Rectangle { implicitWidth: 190; color: "#dddddd"; border.color: "#222" }
        component Choice: MenuItem {
            id: choice
            property bool marked: false
            contentItem: Text {
                leftPadding: 14; text: (choice.marked ? "✓ " : "   ") + choice.text; textFormat: Text.PlainText
                font.pixelSize: 12; color: !choice.enabled ? "#888" : choice.highlighted ? "white" : "#111"
            }
            background: Rectangle { color: choice.highlighted ? "#2c2994" : "transparent" }
        }
        Repeater {
            model: [["yellow", "Yellow"], ["blue", "Blue"], ["green", "Green"], ["pink", "Pink"], ["purple", "Purple"], ["gray", "Gray"]]
            Choice {
                required property var modelData
                objectName: "sticky-color-" + modelData[0]
                text: modelData[1]; marked: note.noteColor === modelData[0]
                onTriggered: note.notes.set(note.noteId, "color", modelData[0])
            }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#999" } }
        Repeater {
            model: [["small", "Small Text"], ["medium", "Medium Text"], ["large", "Large Text"]]
            Choice {
                required property var modelData
                objectName: "sticky-size-" + modelData[0]
                text: modelData[1]; marked: note.noteTextSize === modelData[0]
                onTriggered: note.notes.set(note.noteId, "textSize", modelData[0])
            }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#999" } }
        Choice {
            objectName: "sticky-floating"
            text: "Keep in Front"; marked: note.noteFloating
            onTriggered: note.notes.set(note.noteId, "floating", !note.noteFloating)
        }
        Choice { objectName: "sticky-new"; text: "New Note"; onTriggered: note.newNoteRequested() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#999" } }
        Choice { objectName: "sticky-delete"; text: "Delete Note…"; onTriggered: note.deleteRequested(note.noteId, edit.text === "") }
    }
}
