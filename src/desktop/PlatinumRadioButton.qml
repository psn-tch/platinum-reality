import QtQuick
import QtQuick.Controls

// A Platinum radio button: a white circle with a dark ring, filled with a dot when chosen.
// Grey when unavailable, like PlatinumCheckBox.
RadioButton {
    id: control
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    onPressed: themeSound("chkp")
    onReleased: themeSound("chkr")
    spacing: 8
    padding: 2
    implicitHeight: 24
    font.pixelSize: 12
    indicator: Rectangle {
        x: control.leftPadding; y: (control.height-height)/2
        implicitWidth: 13; implicitHeight: 13; radius: 6.5
        color: control.down ? "#bbb" : "white"; border.color: control.enabled ? "#333" : "#888"
        Rectangle {
            anchors.centerIn: parent; width: 5; height: 5; radius: 2.5
            visible: control.checked; color: control.enabled ? "#111" : "#888"
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text:control.text;textFormat:Text.PlainText;font:control.font
        color:control.enabled?"#111":"#888";verticalAlignment:Text.AlignVCenter
    }
}
