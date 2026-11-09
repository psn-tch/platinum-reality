import QtQuick

// A Finder pop-up window's chrome, measured in the 9.2.2 reference VM (2026-11-09): the tab
// that takes the title bar's place, and the resize grips on the body's two top corners.
// Closed, the tab alone sits on the bottom of the window's screen. A click opens or closes the
// window; a drag along the bottom moves the tab; a drag up and away makes it a regular window.
Item {
    id: chrome
    required property var window
    required property Item frameItem
    required property var sessionManager
    property string fontFamily: ""
    property bool bold: true
    property url icon: ""
    readonly property bool closed: !window.popUpOpen
    anchors.fill: parent

    Item {
        id: tab
        objectName: "pop-up-tab-" + chrome.window.id
        readonly property rect screen: chrome.window.popUpScreen
        width: Math.min(label.implicitWidth + 40, Math.max(60, screen.width)); height: 20
        x: chrome.closed ? Math.max(screen.x, Math.min(chrome.window.popUpTabX, screen.x + screen.width - width)) - chrome.frameItem.x : 0
        y: chrome.closed ? screen.y + screen.height - height - chrome.frameItem.y : 0
        // 20 px: a black top edge over a white highlight, the #cccccc field with a #999999 lower
        // and inner right shadow, a black right edge and a #444444 drop shadow beside it.
        Rectangle {anchors.fill: parent; color: "#ccc"}
        Rectangle {x: 0; y: 0; width: parent.width - 1; height: 1; color: "black"}
        Rectangle {x: 0; y: 1; width: 1; height: parent.height - 1; color: "black"}
        Rectangle {x: 1; y: 1; width: parent.width - 3; height: 1; color: "white"}
        Rectangle {x: 1; y: 2; width: 1; height: parent.height - 3; color: "white"}
        Rectangle {x: parent.width - 3; y: 2; width: 1; height: parent.height - 2; color: "#999"}
        Rectangle {x: 0; y: parent.height - 1; width: parent.width - 2; height: 1; color: "#999"; visible: !chrome.closed}
        Rectangle {x: 0; y: parent.height - 1; width: parent.width - 2; height: 1; color: "black"; visible: chrome.closed}
        Rectangle {x: parent.width - 2; y: 0; width: 1; height: parent.height; color: "black"}
        Rectangle {x: parent.width - 1; y: 4; width: 1; height: parent.height - 4; color: "#444"}
        // Pressed: the field darkens while the button is down over it.
        Rectangle {x: 2; y: 2; width: parent.width - 5; height: parent.height - 3; color: "#888"; visible: area.pressed && !area.dragging && area.containsMouse}
        MenuGlyph {x: 5; y: 3; kind: "folder"; source: chrome.icon}
        Text {
            id: label
            x: 24; anchors.verticalCenter: parent.verticalCenter
            text: chrome.window.title; textFormat: Text.PlainText; color: "black"
            font.family: chrome.fontFamily; font.bold: chrome.bold; font.pixelSize: 12
            width: Math.min(implicitWidth, tab.width - 40); elide: Text.ElideRight
        }
        MouseArea {
            id: area
            objectName: "pop-up-tab-area-" + chrome.window.id
            anchors.fill: parent
            hoverEnabled: false
            preventStealing: true
            property point start
            property int startTabX
            property real grabY
            property bool dragging: false
            readonly property int awayDistance: 40
            onPressed: function(mouse) {
                start = mapToItem(null, mouse.x, mouse.y); startTabX = chrome.window.popUpTabX; grabY = mouse.y; dragging = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const p = mapToItem(null, mouse.x, mouse.y)
                if (!dragging && Math.abs(p.x - start.x) + Math.abs(p.y - start.y) < 4) return
                dragging = true
                if (start.y - p.y < awayDistance) chrome.sessionManager.movePopUpTab(chrome.window.id, Math.round(startTabX + p.x - start.x))
            }
            onReleased: function(mouse) {
                const p = mapToItem(null, mouse.x, mouse.y)
                if (!dragging) chrome.sessionManager.togglePopUp(chrome.window.id)
                else if (start.y - p.y >= awayDistance)
                    chrome.sessionManager.releasePopUp(chrome.window.id, Math.round(startTabX + p.x - start.x),
                                                       Math.round(p.y - grabY))
                dragging = false
            }
            onCanceled: dragging = false
        }
        // A file dragged over a closed tab and held there opens it, as a spring-loaded folder does.
        Timer {
            interval: 1000
            running: chrome.closed && chrome.sessionManager.nativeDragging && tab.contains(
                tab.mapFromItem(null, chrome.sessionManager.nativeDragPosition.x, chrome.sessionManager.nativeDragPosition.y))
            onTriggered: chrome.sessionManager.togglePopUp(chrome.window.id)
        }
    }

    // The grip strip under the tab: a black line, a white highlight and a #dddddd field, with a grip
    // in each corner (as on 9.2.2's pop-up header): diagonal white and grey stripes inside a
    // #555555 edge. Dragging one resizes the window upward and sideways.
    Item {
        visible: !chrome.closed
        x: 3; y: 23; width: chrome.width - 6; height: 13
        Rectangle {anchors.fill: parent; color: "#ddd"}
        Rectangle {width: parent.width; height: 1; color: "black"}
        Rectangle {y: 1; width: parent.width; height: 1; color: "white"}
    }
    component Grip: Canvas {
        id: grip
        property bool mirrored: false
        width: 13; height: 11
        visible: !chrome.closed
        onPaint: {
            const c = getContext("2d"); c.clearRect(0, 0, width, height)
            for (let r = 0; r < height; ++r)
                for (let x = 0; x <= 12 - r; ++x) {
                    const k = (x + r) % 4
                    const colour = x === 12 - r ? "#555" : k === 3 ? "#fff" : k === 1 && x > 0 ? "#888" : ""
                    if (!colour) continue
                    c.fillStyle = colour; c.fillRect(grip.mirrored ? width - 1 - x : x, r, 1, 1)
                }
        }
        MouseArea {
            anchors.fill: parent
            property point start
            property int startX
            property int startW
            property int startH
            onPressed: function(mouse) {
                start = mapToItem(null, mouse.x, mouse.y)
                startX = chrome.window.x; startW = chrome.window.width; startH = chrome.window.height
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const p = mapToItem(null, mouse.x, mouse.y), dx = Math.round(p.x - start.x)
                const height = startH - Math.round(p.y - start.y)
                if (grip.mirrored) chrome.sessionManager.resizePopUp(chrome.window.id, startX, startW + dx, height)
                else chrome.sessionManager.resizePopUp(chrome.window.id, startX + dx, startW - dx, height)
            }
        }
    }
    Grip {objectName: "pop-up-grip-left-" + chrome.window.id; x: 4; y: 25}
    Grip {objectName: "pop-up-grip-right-" + chrome.window.id; mirrored: true; x: chrome.width - 4 - width; y: 25}
}
