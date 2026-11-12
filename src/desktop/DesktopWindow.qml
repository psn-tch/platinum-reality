import QtQuick

// Original Platinum frame for desktop-owned accessories, matching Control Panels.
FocusScope {
    id: frame
    property string title: ""
    signal raised()
    signal closed()
    default property alias contents: page.data
    readonly property bool front: tracker.front
    property var moveManager: typeof manager === "undefined" ? null : manager
    onFrontChanged: if (!front && moveManager) moveManager.finishShellWindowMove(frame, false)
    property bool closeOnEscape: true
    // 9.2.2's Calculator style: a rounded black frame and title bar, white title and close box.
    property bool blackFrame: false
    // System 6's frame (the classic Control Panel): white, a black border and shadow, and a title
    // bar of black lines around the title.
    property bool classicFrame: false
    // Optional 9.2.2 window controls (Platinum frame only): the collapse box rolls the window up to
    // its title bar (as does a double-click on the title bar), the zoom box switches between the
    // size the user gave it and zoomedSize, and the size box at the bottom right resizes it.
    property bool collapsible: false
    property bool zoomable: false
    property bool resizable: false
    property bool collapsed: false
    property size minimumSize: Qt.size(240, 160)
    property size zoomedSize: Qt.size(width, height)
    property size userSize: Qt.size(width, height)
    property bool zoomed: false
    // The height while collapsed is only the title bar; the open height is kept meanwhile.
    property real openHeight: height
    function setCollapsed(on) {
        if (!collapsible || on === collapsed) return
        if (on) { openHeight = height; collapsed = true; height = 24 }
        else { collapsed = false; height = openHeight }
    }
    function toggleZoom() {
        if (!zoomable) return
        if (collapsed) setCollapsed(false)
        if (!zoomed) { userSize = Qt.size(width, height); width = zoomedSize.width; height = zoomedSize.height }
        else { width = userSize.width; height = userSize.height }
        zoomed = !zoomed
    }
    width: 520; height: 450; visible: false
    function open() { visible = true; raised(); forceActiveFocus() }
    function close() { visible = false; closed() }
    ShellWindowFocus { id: tracker; window: frame }
    // On workspaces like an application's window (WorkspaceMember.qml); hidden while away.
    readonly property WorkspaceMember workspaceMember: WorkspaceMember {window: frame}
    opacity: workspaceMember.away ? 0 : 1
    enabled: !workspaceMember.away
    Keys.onEscapePressed: if (closeOnEscape) frame.close()
    Keys.onPressed: function(event) {
        const modifiers=event.modifiers & ~Qt.KeypadModifier
        if (event.key === Qt.Key_W && (modifiers===Qt.MetaModifier || modifiers===Qt.ControlModifier)) {
            event.accepted = true; frame.close()
        }
    }
    Rectangle {
        visible: frame.blackFrame
        anchors.fill: parent; color: "black"; radius: 6
        Rectangle { x: 4; y: 22; width: parent.width - 8; height: parent.height - 26; radius: 3; color: "#ccc" }
    }
    Rectangle {
        visible: frame.classicFrame
        anchors.fill: parent; color: "white"; border.color: "black"
        Rectangle { x: 1; y: 21; width: parent.width - 2; height: 1; color: "black" }
    }
    Rectangle { visible: frame.classicFrame; z: -1; x: 1; y: 1; width: parent.width + 1; height: parent.height + 1; color: "black" }
    Rectangle {
        visible: !frame.blackFrame && !frame.classicFrame
        anchors.fill: parent; color: "#ddd"; border.color: "#222"
        Rectangle { x:1; y:1; width:parent.width-2; height:1; color:"white" }
        Rectangle { x:1; y:1; width:1; height:parent.height-2; color:"white" }
        Rectangle { x:parent.width-2; y:1; width:1; height:parent.height-2; color:"#888" }
        Rectangle { x:1; y:parent.height-2; width:parent.width-2; height:1; color:"#888" }
    }
    Rectangle { visible: !frame.classicFrame; z:-1; x:3; y:3; width:parent.width; height:parent.height; color:"#50000000" }
    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) { frame.raised(); frame.forceActiveFocus(); mouse.accepted = false }
    }
    Item {
        id: titleBar
        x:3; y:3; width:parent.width-6; height:18
        Repeater {
            model:5
            Rectangle {
                required property int index
                x:frame.classicFrame ? 1 : 20; y:3+index*3; width:titleBar.width-(frame.classicFrame ? 2 : 24); height:1
                visible: !frame.blackFrame && (!frame.classicFrame || frame.front)
                color:frame.classicFrame ? "black" : frame.front ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill:parent
            onPressed: function(mouse) {
                frame.raised(); frame.forceActiveFocus()
                if (frame.moveManager)
                    frame.moveManager.beginShellWindowMove(frame, mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: if (frame.moveManager) frame.moveManager.finishShellWindowMove(frame)
            onCanceled: if (frame.moveManager) frame.moveManager.finishShellWindowMove(frame, false)
            onDoubleClicked: frame.setCollapsed(!frame.collapsed)
        }
        // The zoom box, then the collapse box at the far right, as 9.2.2 draws them.
        Repeater {
            model: [{kind:"zoom", shown:frame.zoomable}, {kind:"collapse", shown:frame.collapsible}]
            Rectangle {
                required property var modelData
                required property int index
                objectName: frame.objectName + "-" + modelData.kind
                visible: modelData.shown && !frame.blackFrame
                x: titleBar.width - 16 - (index === 0 && frame.collapsible ? 18 : 0); y: 3; width: 12; height: 12
                border.color: "#222"; color: boxMouse.pressed ? "#888" : "#ddd"
                Rectangle { x:1; y:1; width:9; height:1; color: boxMouse.pressed ? "#666" : "white" }
                Rectangle { x:1; y:1; width:1; height:9; color: boxMouse.pressed ? "#666" : "white" }
                // Zoom: a small box in the corner of a larger one; collapse: one line across.
                Rectangle { visible: modelData.kind === "zoom"; x:2; y:2; width:6; height:6; color:"transparent"; border.color:"#222" }
                Rectangle { visible: modelData.kind === "collapse"; x:2; y:5; width:8; height:2; color:"#222" }
                MouseArea {
                    id: boxMouse; anchors.fill: parent
                    onClicked: if (modelData.kind === "zoom") frame.toggleZoom(); else frame.setCollapsed(!frame.collapsed)
                }
            }
        }
        Rectangle {
            objectName:frame.objectName+"-close"
            x:frame.classicFrame ? 8 : 2; y:frame.classicFrame ? 2 : 3; width:frame.classicFrame ? 14 : 12; height:frame.classicFrame ? 14 : 12
            visible: !frame.classicFrame || frame.front
            border.color:frame.blackFrame ? "white" : "#222"
            color:frame.blackFrame ? (closeMouse.pressed ? "#666" : "black") : frame.classicFrame ? (closeMouse.pressed ? "black" : "white") : closeMouse.pressed ? "#888" : "#ddd"
            Rectangle { visible: !frame.blackFrame && !frame.classicFrame; x:1; y:1; width:9; height:1; color:closeMouse.pressed ? "#666" : "white" }
            Rectangle { visible: !frame.blackFrame && !frame.classicFrame; x:1; y:1; width:1; height:9; color:closeMouse.pressed ? "#666" : "white" }
            MouseArea { id:closeMouse; anchors.fill:parent; onClicked:frame.close() }
        }
        Rectangle {
            anchors.centerIn:parent; width:Math.min(parent.width-(frame.zoomable || frame.collapsible ? 84 : 48),titleText.implicitWidth+12); height:18
            color:frame.blackFrame ? "black" : frame.classicFrame ? "white" : "#ddd"
            Text {
                id:titleText; anchors.centerIn:parent; text:frame.title; color:frame.blackFrame ? "white" : "black"
                font.pixelSize: frame.classicFrame ? 14 : 12
                textFormat:Text.PlainText; font.bold:true
            }
        }
    }
    Item { id:page; x:14; y:32; width:parent.width-28; height:parent.height-46; visible:!frame.collapsed }
    // The size box: drag it to change the window's size, never below minimumSize.
    Item {
        objectName: frame.objectName + "-size"
        visible: frame.resizable && !frame.collapsed && !frame.blackFrame
        x: frame.width - 17; y: frame.height - 17; width: 15; height: 15
        Canvas {
            anchors.fill: parent
            onPaint: {
                const c = getContext("2d"); c.clearRect(0, 0, width, height)
                for (let i = 0; i < 3; ++i) {
                    c.fillStyle = "white"; c.fillRect(4 + 3 * i, 12 - 3 * i, 9 - 3 * i, 1); c.fillRect(4 + 3 * i, 12 - 3 * i, 1, 1)
                    c.fillStyle = "#777"; c.fillRect(5 + 3 * i, 13 - 3 * i, 9 - 3 * i, 1)
                }
            }
        }
        MouseArea {
            anchors.fill: parent; preventStealing: true
            property point start
            property size startSize
            onPressed: function(mouse) {
                frame.raised(); frame.forceActiveFocus()
                start = mapToItem(frame.parent, mouse.x, mouse.y); startSize = Qt.size(frame.width, frame.height)
            }
            onPositionChanged: function(mouse) {
                const at = mapToItem(frame.parent, mouse.x, mouse.y)
                frame.width = Math.max(frame.minimumSize.width, Math.round(startSize.width + at.x - start.x))
                frame.height = Math.max(frame.minimumSize.height, Math.round(startSize.height + at.y - start.y))
                frame.zoomed = false
            }
        }
    }
}
