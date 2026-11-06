import QtQuick
import QtQuick.Controls

// Scrapbook, the desk accessory: text and pictures kept from the clipboard. Edit ▸ Paste
// (Command-V) puts what is on the clipboard in front of the item shown; Copy (Command-C) puts the
// item back on the clipboard; Cut does both; Clear (Delete) removes it. The scroll bar, or the left
// and right arrow keys, move through the items. Saved in the profile (Scrapbook).
DesktopWindow {
    id: scrapbook
    objectName: "scrapbook-window"
    title: "Scrapbook"
    width: 420; height: 360
    required property var store
    required property var clipboard
    property int index: 0
    readonly property int count: store ? store.count : 0
    // The item shown: {kind, text, picture, size, width, height}, or {} when empty.
    readonly property var item: store && count > 0 ? (revision, store.item(index)) : ({})
    property int revision: 0
    Connections { target: scrapbook.store; function onChanged() { scrapbook.revision++; scrapbook.index = Math.min(scrapbook.index, Math.max(0, scrapbook.count - 1)) } }
    readonly property bool clipboardHasSomething: clipboard && (clipboard.kind === "text" || clipboard.kind === "picture")
    function show(step) { if (count > 0) index = (index + step + count) % count }
    function paste() {
        if (!store || !clipboardHasSomething) return false
        const added = clipboard.kind === "picture" ? store.addPicture(clipboard.picture, index)
                                                   : store.addText(clipboard.text, index)
        return added
    }
    function copy() {
        if (!clipboard || !item.kind) return false
        if (item.kind === "text") { clipboard.copyText(item.text); return true }
        return clipboard.copyImage(item.picture)
    }
    function clear() { if (store && item.kind) store.remove(index) }
    function describeSize(bytes) {
        return bytes < 1024 ? bytes + " bytes" : bytes < 1024 * 1024 ? Math.round(bytes / 1024) + "K"
                            : (bytes / 1024 / 1024).toFixed(1) + " MB"
    }
    // Keep the desktop's own Command shortcuts away while Scrapbook has the keyboard.
    Keys.onShortcutOverride: function(event) {
        const command = event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)
        if (command && event.key >= Qt.Key_A && event.key <= Qt.Key_Z && event.key !== Qt.Key_W) event.accepted = true
    }
    Keys.onPressed: function(event) {
        const modifiers = event.modifiers & ~Qt.KeypadModifier
        const command = modifiers === Qt.MetaModifier || modifiers === Qt.ControlModifier
        if (command && event.key === Qt.Key_V) { paste(); event.accepted = true }
        else if (command && event.key === Qt.Key_C) { copy(); event.accepted = true }
        else if (command && event.key === Qt.Key_X) { if (copy()) clear(); event.accepted = true }
        else if (modifiers === Qt.NoModifier && (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)) { clear(); event.accepted = true }
        else if (modifiers === Qt.NoModifier && event.key === Qt.Key_Left) { show(-1); event.accepted = true }
        else if (modifiers === Qt.NoModifier && event.key === Qt.Key_Right) { show(1); event.accepted = true }
    }
    // The desktop's Edit menu acts on Scrapbook while it is in front (Desktop.qml editAccessory).
    readonly property var editCommands: ({cut: Boolean(item.kind), copy: Boolean(item.kind),
                                          paste: clipboardHasSomething && count < 100, clear: Boolean(item.kind)})
    function edit(command) {
        if (command === "paste") paste()
        else if (command === "copy") copy()
        else if (command === "cut") { if (copy()) clear() }
        else if (command === "clear") clear()
    }
    // Laid out as 9.2.2's Scrapbook (captured in the reference VM, 2026-11-04): the item in an inset
    // white well, a scroll bar to move through the items, and Item, Type and Size below it.
    Column {
        anchors.fill: parent; spacing: 8
        Rectangle {
            id: view
            objectName: "scrapbook-view"
            width: parent.width; height: parent.height - bar.height - info.height - 16
            color: "white"; clip: true
            // Inset: dark above and left, light below and right.
            Rectangle { width: parent.width; height: 2; color: "#555" }
            Rectangle { width: 2; height: parent.height; color: "#555" }
            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: "#eee" }
            Rectangle { x: parent.width - 1; width: 1; height: parent.height; color: "#eee" }
            Text {
                visible: scrapbook.count === 0
                anchors.centerIn: parent; width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: "#666"; font.pixelSize: 12
                textFormat: Text.PlainText
                text: "The Scrapbook is empty. Copy some text or a picture, then choose Edit ▸ Paste (Command-V) to keep it here."
            }
            Flickable {
                visible: scrapbook.item.kind === "text"
                anchors.fill: parent; anchors.margins: 10
                contentWidth: width; contentHeight: shownText.implicitHeight; clip: true
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    id: shownText
                    objectName: "scrapbook-text"
                    width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 13
                    text: scrapbook.item.kind === "text" ? scrapbook.item.text : ""
                }
            }
            Image {
                objectName: "scrapbook-picture"
                visible: scrapbook.item.kind === "picture"
                anchors.fill: parent; anchors.margins: 10
                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                source: scrapbook.item.kind === "picture" ? scrapbook.item.picture : ""
                sourceSize: Qt.size(800, 800)
            }
        }
        // One scroll box position an item.
        PlatinumScrollBar {
            id: bar
            objectName: "scrapbook-scroll"
            orientation: Qt.Horizontal
            width: parent.width
            enabled: scrapbook.count > 1
            // A small box of fixed size, as 9.2.2 draws it here, whatever the number of items.
            size: 0
            stepSize: scrapbook.count > 1 ? 1 / (scrapbook.count - 1) : 1
            position: scrapbook.count > 1 ? scrapbook.index / (scrapbook.count - 1) : 0
            // A drag or an arrow moves the box; the item follows once it has moved.
            function follow() {
                if (scrapbook.count < 2) return
                const at = Math.max(0, Math.min(scrapbook.count - 1, Math.round(position * (scrapbook.count - 1))))
                if (at !== scrapbook.index) scrapbook.index = at
            }
            onPositionChanged: Qt.callLater(follow)
        }
        Rectangle {
            id: info
            width: parent.width; height: 52; color: "white"
            Rectangle { width: parent.width; height: 2; color: "#555" }
            Rectangle { width: 2; height: parent.height; color: "#555" }
            Grid {
                x: 8; y: 5; columns: 2; columnSpacing: 6; rowSpacing: 1
                Text { text: "Item:"; font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignRight; width: 40 }
                Text {
                    objectName: "scrapbook-position"; font.pixelSize: 11; textFormat: Text.PlainText
                    text: scrapbook.count ? (scrapbook.index + 1) + " of " + scrapbook.count : "none"
                }
                Text { text: "Type:"; font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignRight; width: 40 }
                Text {
                    objectName: "scrapbook-type"; font.pixelSize: 11; textFormat: Text.PlainText
                    text: !scrapbook.item.kind ? "" : scrapbook.item.kind === "picture" ? "picture" : "text"
                }
                Text { text: "Size:"; font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignRight; width: 40 }
                Text {
                    objectName: "scrapbook-size"; font.pixelSize: 11; textFormat: Text.PlainText
                    text: !scrapbook.item.kind ? "" : scrapbook.describeSize(scrapbook.item.size)
                          + (scrapbook.item.kind === "picture" ? ", " + scrapbook.item.width + " × " + scrapbook.item.height + " pixels" : "")
                }
            }
        }
    }
}
