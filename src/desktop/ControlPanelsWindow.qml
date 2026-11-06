import QtQuick
import QtQuick.Controls

// Control Panels: one modeless window for every control panel (psn_tch, 2026-11-01: the Control Strip is
// for quick changes; this is where things are set up properly). The panels are listed on the left and
// the chosen one is shown on the right. Each panel is a child Item with `panelId`, `title` and `glyph`;
// the window sets its `host`, `visible` and `shown`, so a panel only talks to its service while it is
// on screen. A panel's edit() opens this window at that panel.
FocusScope {
    id: window
    signal raised()
    signal closed()
    objectName: "control-panels-window"
    width: 780
    height: 560
    visible: false
    // The panel on screen: one of entries' ids.
    property string current: entries.length ? entries[0].id : ""
    // {id, title, glyph} for each panel, in the order they are declared.
    property var entries: []
    default property alias panels: stack.data
    readonly property var currentEntry: entries.find(function(e) { return e.id === window.current }) || null
    // The panel on screen; its `heading`, when it has one, names what it shows now (Add Bluetooth Device).
    property var currentPanel: null
    function show(id) {
        if (id && entries.some(function(e) { return e.id === id })) current = id
        visible = true
        raised()
        forceActiveFocus()
        contents.contentY = 0
    }
    function close() {
        visible = false
        if (typeof desktop !== "undefined" && desktop) desktop.forceActiveFocus()
        closed()
    }
    function updatePanels() {
        const found = []
        for (let i = 0; i < stack.children.length; ++i) {
            const panel = stack.children[i]
            if (panel.panelId === undefined) continue
            panel.host = window
            // A panel for hardware this computer doesn't have (Handheld) sets listed: false.
            if (panel.listed === false) { panel.visible = false; panel.shown = false; continue }
            found.push({id: panel.panelId, title: panel.title, glyph: panel.glyph})
            panel.visible = panel.panelId === window.current
            panel.shown = window.visible && panel.panelId === window.current
            if (panel.panelId === window.current) currentPanel = panel
        }
        if (JSON.stringify(found) !== JSON.stringify(entries)) entries = found
        // The panel on screen went away (hhd stopped): show the first one instead.
        if (found.length && !found.some(function(e) { return e.id === window.current })) current = found[0].id
    }
    // In front: it had the keyboard last (ShellWindowFocus).
    property alias front: focusTracker.front
    ShellWindowFocus { id: focusTracker; window: window }
    onCurrentChanged: { updatePanels(); contents.contentY = 0 }
    onVisibleChanged: updatePanels()
    Component.onCompleted: updatePanels()
    Keys.onEscapePressed: window.close()
    // Command-W (Super, or Ctrl as applications receive it) closes it, as any window.
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_W && (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier))) {
            event.accepted = true
            window.close()
        }
    }
    Keys.onUpPressed: list.decrementCurrentIndex()
    Keys.onDownPressed: list.incrementCurrentIndex()
    Rectangle {
        anchors.fill: parent
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
    }
    // Shadow, as for every Platinum window.
    Rectangle {z: -1; x: 3; y: 3; width: parent.width; height: parent.height; color: "#50000000"}
    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) { window.raised(); window.forceActiveFocus(); mouse.accepted = false }
    }
    Item {
        id: titleBar
        x: 3; y: 3; width: parent.width - 6; height: 18
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 20; y: 3 + index * 3; width: titleBar.width - 24; height: 1
                color: window.activeFocus ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill: parent
            drag.target: window; drag.threshold: 2
            onPressed: { window.raised(); window.forceActiveFocus() }
        }
        Rectangle {
            objectName: "control-panels-close"
            x: 2; y: 3; width: 12; height: 12
            color: closeMouse.pressed ? "#888" : "#ddd"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: 9; height: 1; color: closeMouse.pressed ? "#666" : "white"}
            Rectangle {x: 1; y: 1; width: 1; height: 9; color: closeMouse.pressed ? "#666" : "white"}
            MouseArea {id: closeMouse; anchors.fill: parent; onClicked: window.close()}
        }
        Rectangle {
            anchors.centerIn: parent
            width: titleText.implicitWidth + 12; height: 18; color: "#ddd"
            Text {id: titleText; anchors.centerIn: parent; text: "Control Panels"; font.pixelSize: 12; font.bold: true}
        }
    }
    // The panel list: a white inset well, the chosen panel highlighted.
    Rectangle {
        id: well
        x: 10; y: titleBar.y + titleBar.height + 8; width: 168; height: parent.height - y - 10
        color: "white"; border.color: "#222"
        ListView {
            id: list
            objectName: "control-panels-list"
            anchors.fill: parent; anchors.margins: 1
            clip: true
            model: window.entries
            currentIndex: window.entries.findIndex(function(e) { return e.id === window.current })
            onCurrentIndexChanged: if (currentIndex >= 0 && window.entries[currentIndex].id !== window.current) window.current = window.entries[currentIndex].id
            // The name is highlighted, not the icon, so the icon stays readable (as in a Finder list).
            delegate: Item {
                required property var modelData
                objectName: "control-panel-" + modelData.id
                width: ListView.view.width; height: 34
                Rectangle {
                    x: 34; y: 7; width: Math.min(parent.width - 38, label.implicitWidth + 8); height: 20
                    color: modelData.id === window.current ? (window.activeFocus ? "#333" : "#999") : "transparent"
                }
                StripGlyph {
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: 22; height: 22; kind: modelData.glyph
                }
                Text {
                    id: label
                    x: 38; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 44; elide: Text.ElideRight
                    text: modelData.title; textFormat: Text.PlainText; font.pixelSize: 12
                    color: modelData.id === window.current ? "white" : "black"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { window.raised(); window.forceActiveFocus(); window.current = modelData.id }
                }
            }
        }
    }
    // The chosen panel, its name above it, scrolling when it is taller than the window.
    Item {
        id: page
        x: well.x + well.width + 12; y: well.y; width: parent.width - x - 10; height: well.height
        Text {
            id: heading
            objectName: "control-panels-heading"
            text: window.currentPanel ? (window.currentPanel.heading || window.currentPanel.title) : ""
            font.pixelSize: 15; font.bold: true; textFormat: Text.PlainText
        }
        Rectangle {x: 0; y: heading.height + 6; width: parent.width; height: 1; color: "#888"}
        Rectangle {x: 0; y: heading.height + 7; width: parent.width; height: 1; color: "white"}
        Flickable {
            id: contents
            objectName: "control-panels-page"
            x: 0; y: heading.height + 16; width: parent.width - 14; height: parent.height - y
            clip: true
            contentWidth: width; contentHeight: stack.childrenRect.height + 8
            boundsBehavior: Flickable.StopAtBounds
            // Only when the panel is taller than the window.
            ScrollBar.vertical: PlatinumScrollBar {
                parent: page; x: page.width - width; y: contents.y; height: contents.height
                visible: contents.contentHeight > contents.height
            }
            Item {
                id: stack
                width: contents.width - 6
                onChildrenChanged: window.updatePanels()
            }
        }
    }
}
