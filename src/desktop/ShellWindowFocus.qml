import QtQuick
import QtQuick.Controls

// Whether a desktop-owned window (Control Panels, a sticky note) is in front: it had the keyboard
// last. An alert opened over it takes the keyboard for a while; when it closes, or when the item
// that had focus disappears, the keyboard comes back to the window, as on a Mac. Anything else
// taking the keyboard (the desktop, an application, another such window) puts it behind.
// The desktop gives the keyboard back to the front one (Desktop.qml, onFocusDesktop).
Item {
    id: tracker
    required property Item window
    property bool front: false
    visible: false
    readonly property var focusedItem: Window.activeFocusItem
    function contains(item) {
        for (let i = item; i; i = i.parent) if (i === tracker.window) return true
        return false
    }
    function inPopup(item) {
        for (let i = item; i; i = i.parent) if (i === Overlay.overlay) return true
        return false
    }
    onFocusedItemChanged: {
        const item = focusedItem
        if (!window.visible) { front = false; return }
        if (contains(item)) front = true
        else if (item && item !== Window.contentItem && !inPopup(item)) front = false
        else if (front) Qt.callLater(function() {
            const now = tracker.focusedItem
            if (tracker.window.visible && tracker.front && (!now || now === tracker.Window.contentItem))
                tracker.window.forceActiveFocus()
        })
    }
    onFrontChanged: if (front && typeof desktop !== "undefined" && desktop) desktop.frontShellWindow = window
}
