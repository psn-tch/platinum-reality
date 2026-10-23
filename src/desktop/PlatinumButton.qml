import QtQuick
import QtQuick.Controls

Button {
    id: control
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    onPressed: themeSound("btnp")
    onReleased: themeSound("btnr")
    property bool defaultAction: false
    implicitWidth: Math.max(86, contentItem.implicitWidth + 28)
    implicitHeight: 28
    font.pixelSize: 12
    contentItem: Text {
        text: control.text; textFormat: Text.PlainText
        font: control.font; color: !control.enabled ? "#888" : control.down ? "white" : "#111"
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
    }
    // 9.2.2's push button and default ring, measured in Sammy's reference VM (2026-10-22): a
    // rounded black outline; raised, a white top-left and a two-step (#aaa, #777) bottom-right edge
    // on #ddd; held, #444/#555 and #777/#888 on #666 with white text. The default ring is a rounded
    // black line 3 px out, its groove lit #aaa above and left, shaded #aaa then #777 below and right.
    background: Item {
        readonly property color edge: control.enabled ? "black" : "#888"
        Rectangle {
            objectName: "platinum-button-face"
            anchors.fill: parent; radius: 3; border.color: parent.edge
            color: control.down ? "#666" : "#ddd"
            Rectangle {x:2;y:1;width:parent.width-4;height:1;color:control.down?"#444":"white"}
            Rectangle {x:1;y:2;width:1;height:parent.height-4;color:control.down?"#444":"white"}
            Rectangle {visible:control.down;x:2;y:2;width:parent.width-5;height:1;color:"#555"}
            Rectangle {visible:control.down;x:2;y:2;width:1;height:parent.height-5;color:"#555"}
            Rectangle {x:2;y:parent.height-3;width:parent.width-4;height:1;color:control.down?"#777":"#aaa"}
            Rectangle {x:parent.width-3;y:2;width:1;height:parent.height-4;color:control.down?"#777":"#aaa"}
            Rectangle {x:2;y:parent.height-2;width:parent.width-3;height:1;color:control.down?"#888":"#777"}
            Rectangle {x:parent.width-2;y:2;width:1;height:parent.height-3;color:control.down?"#888":"#777"}
        }
        Item {
            objectName: "platinum-button-ring"
            anchors.fill: parent; visible: control.activeFocus || control.defaultAction
            Rectangle {x:-3;y:-3;width:parent.width+6;height:parent.height+6;radius:5;color:"transparent";border.color:parent.parent.edge}
            Rectangle {x:1;y:-1;width:parent.width-2;height:1;color:"#aaa"}
            Rectangle {x:-1;y:1;width:1;height:parent.height-2;color:"#aaa"}
            Rectangle {x:1;y:parent.height;width:parent.width-1;height:1;color:"#aaa"}
            Rectangle {x:parent.width;y:1;width:1;height:parent.height;color:"#aaa"}
            Rectangle {x:1;y:parent.height+1;width:parent.width;height:1;color:"#777"}
            Rectangle {x:parent.width+1;y:1;width:1;height:parent.height;color:"#777"}
        }
    }
}
