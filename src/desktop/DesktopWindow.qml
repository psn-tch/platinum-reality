import QtQuick

// Original Platinum frame for desktop-owned accessories, matching Control Panels.
FocusScope {
    id: frame
    property string title: ""
    signal raised()
    signal closed()
    default property alias contents: page.data
    readonly property bool front: tracker.front
    property bool closeOnEscape: true
    // 9.2.2's Calculator style: a rounded black frame and title bar, white title and close box.
    property bool blackFrame: false
    width: 520; height: 450; visible: false
    function open() { visible = true; raised(); forceActiveFocus() }
    function close() { visible = false; closed() }
    ShellWindowFocus { id: tracker; window: frame }
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
        visible: !frame.blackFrame
        anchors.fill: parent; color: "#ddd"; border.color: "#222"
        Rectangle { x:1; y:1; width:parent.width-2; height:1; color:"white" }
        Rectangle { x:1; y:1; width:1; height:parent.height-2; color:"white" }
        Rectangle { x:parent.width-2; y:1; width:1; height:parent.height-2; color:"#888" }
        Rectangle { x:1; y:parent.height-2; width:parent.width-2; height:1; color:"#888" }
    }
    Rectangle { z:-1; x:3; y:3; width:parent.width; height:parent.height; color:"#50000000" }
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
                x:20; y:3+index*3; width:titleBar.width-24; height:1
                visible: !frame.blackFrame
                color:frame.front ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill:parent; drag.target:frame; drag.threshold:2
            onPressed: { frame.raised(); frame.forceActiveFocus() }
        }
        Rectangle {
            objectName:frame.objectName+"-close"
            x:2; y:3; width:12; height:12; border.color:frame.blackFrame ? "white" : "#222"
            color:frame.blackFrame ? (closeMouse.pressed ? "#666" : "black") : closeMouse.pressed ? "#888" : "#ddd"
            Rectangle { visible: !frame.blackFrame; x:1; y:1; width:9; height:1; color:closeMouse.pressed ? "#666" : "white" }
            Rectangle { visible: !frame.blackFrame; x:1; y:1; width:1; height:9; color:closeMouse.pressed ? "#666" : "white" }
            MouseArea { id:closeMouse; anchors.fill:parent; onClicked:frame.close() }
        }
        Rectangle {
            anchors.centerIn:parent; width:Math.min(parent.width-48,titleText.implicitWidth+12); height:18
            color:frame.blackFrame ? "black" : "#ddd"
            Text {
                id:titleText; anchors.centerIn:parent; text:frame.title; color:frame.blackFrame ? "white" : "black"
                textFormat:Text.PlainText; font.pixelSize:12; font.bold:true
            }
        }
    }
    Item { id:page; x:14; y:32; width:parent.width-28; height:parent.height-46 }
}
