import QtQuick
import QtQuick.Controls

// Note Pad, the desk accessory: eight pages of text with a turned-up corner at the bottom left. Click the folded flap for the next page, or the uncovered corner below it for the
// previous one; the page number is beside it. Pages are saved in the profile (NotePad).
DesktopWindow {
    id: notePad
    objectName: "note-pad-window"
    title: "Note Pad"
    width: 260; height: 300
    required property var store
    readonly property int page: store ? store.page : 0
    readonly property int pageCount: store ? store.pageCount : 8
    function turn(step) {
        if (!store) return
        store.setText(page, editor.text)
        store.page = (page + step + pageCount) % pageCount
    }
    onOpenedChanged: if (opened) editor.forceActiveFocus()
    property bool opened: visible
    // A page opens with the insertion point after its text, ready to add to it.
    function load() { editor.text = store ? store.text(page) : ""; editor.cursorPosition = editor.length }
    onPageChanged: load()
    Component.onCompleted: load()
    // The page, as 9.2.2's Note Pad draws it (reference VM, 2026-11-04): white, a scroll bar at the
    // right, the page number centred at the bottom, the turned-up corner at the bottom left, and
    // the edges of the pages beneath along the bottom.
    Rectangle {
        id: paper
        objectName: "note-pad-paper"
        x: -6; y: -10; width: parent.width + 12; height: parent.height + 10
        color: "white"; border.color: "black"
        Flickable {
            id: scroller
            x: 6; y: 4; width: paper.width - 30; height: paper.height - 40
            contentWidth: width; contentHeight: editor.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PlatinumScrollBar {
                parent: paper; x: paper.width - 17; y: 1; height: paper.height - 9
            }
            TextArea.flickable: TextArea {
                id: editor
                objectName: "note-pad-text"
                wrapMode: TextEdit.Wrap; selectByMouse: true; textFormat: TextEdit.PlainText
                font.pixelSize: 13; color: "black"
                selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"; selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
                leftPadding: 0; rightPadding: 0; topPadding: 4
                background: null
                onTextChanged: if (notePad.store && notePad.store.text(notePad.page) !== text) notePad.store.setText(notePad.page, text)
            }
        }
        // The edges of the pages beneath.
        Repeater {
            model: 3
            Rectangle {
                required property int index
                x: 1; y: paper.height - 8 + index * 2; width: paper.width - 18; height: 1; color: index === 1 ? "#888" : "black"
            }
        }
        // The turned-up corner: the flap (the page's back, shaded) above the fold, the next page
        // below it.
        Item {
            id: corner
            objectName: "note-pad-corner"
            x: 1; y: paper.height - 9 - size; width: size; height: size
            readonly property int size: 24
            Canvas {
                id: fold
                anchors.fill: parent
                onPaint: {
                    const c = getContext("2d"), s = width
                    c.clearRect(0, 0, s, s)
                    c.fillStyle = "#eeeeee"
                    c.beginPath(); c.moveTo(0, 0); c.lineTo(0, s); c.lineTo(s, s); c.closePath(); c.fill()
                    const shade = c.createLinearGradient(0, 0, s, 0)
                    shade.addColorStop(0, "#f4f4f4"); shade.addColorStop(1, "#777777")
                    c.fillStyle = shade
                    c.beginPath(); c.moveTo(0, 0); c.lineTo(s, s); c.lineTo(s, 0); c.closePath(); c.fill()
                    c.strokeStyle = "black"; c.lineWidth = 1
                    c.beginPath(); c.moveTo(0, 0.5); c.lineTo(s - 0.5, 0.5); c.lineTo(s - 0.5, s); c.stroke()
                    c.strokeStyle = "#555"
                    c.beginPath(); c.moveTo(0.5, 0.5); c.lineTo(s - 0.5, s - 0.5); c.stroke()
                }
                onAvailableChanged: if (available) requestPaint()
            }
            MouseArea {
                anchors.fill: parent
                // Above the fold (the flap): forward. Below it: back.
                onClicked: function(mouse) { notePad.turn(mouse.x >= mouse.y ? 1 : -1) }
            }
        }
        Text {
            objectName: "note-pad-page-number"
            anchors.horizontalCenter: scroller.horizontalCenter; y: paper.height - 30
            text: String(notePad.page + 1); textFormat: Text.PlainText
            font.pixelSize: 13
        }
    }
}
