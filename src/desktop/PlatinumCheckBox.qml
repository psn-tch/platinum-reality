import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    onPressed: themeSound("chkp")
    onReleased: themeSound("chkr")
    spacing: 8
    padding: 2
    implicitHeight: 26
    font.pixelSize: 12
    indicator: Rectangle {
        x: control.leftPadding; y: (control.height-height)/2
        implicitWidth: 13; implicitHeight: 13
        color: control.down ? "#bbb" : "white"; border.color: "#333"
        Rectangle {x:1;y:1;width:parent.width-2;height:1;color:"#888"}
        Canvas {
            id: tick
            anchors.fill:parent
            Connections {target:control;function onCheckedChanged(){tick.requestPaint()} function onEnabledChanged(){tick.requestPaint()}}
            onPaint: {
                let c = getContext("2d");c.clearRect(0,0,width,height)
                if(control.checked){c.strokeStyle=control.enabled?"#111":"#888";c.lineWidth=2;c.beginPath();c.moveTo(3,6);c.lineTo(6,9);c.lineTo(11,3);c.stroke()}
            }
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text:control.text;textFormat:Text.PlainText;font:control.font
        color:control.enabled?"#111":"#888";verticalAlignment:Text.AlignVCenter
    }
}
