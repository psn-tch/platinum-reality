import QtQuick
import QtQuick.Controls

ShellDialog {
    id: controls
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    required property var service
    // Bluetooth power comes from the Bluetooth service, which follows BlueZ live.
    property var bluetooth: null
    readonly property bool bluetoothReady: Boolean(bluetooth) && bluetooth.available
    objectName: "quick-controls"
    title: "Control Strip"
    width: 350
    standardButtons: Dialog.Close
    property var values: service.state
    function showControls() { open(); service.refresh() }
    function item(name) { return values[name] || ({available:false}) }
    contentItem: Column {
        spacing: 12
        Text {text:"Quick settings";font.pixelSize:12;color:"#111"}
        Repeater {
            model:[{key:"brightness",label:"Brightness"},{key:"volume",label:"Volume"}]
            delegate: Column {
                required property var modelData
                width: parent.width
                spacing: 3
                Text {
                    font.pixelSize:12;color:"#111"
                    text:modelData.label + (controls.item(modelData.key).available
                          ? "   " + controls.item(modelData.key).value + "%"
                            + (modelData.key==="volume" && controls.item("volume").muted ? " (muted)" : "")
                          : "   Unavailable")
                }
                Row {
                    spacing:8
                    Slider {
                        id: slider
                        objectName:"quick-"+modelData.key
                        width:210;height:25
                        from:modelData.key==="brightness" ? 5 : 0;to:100;stepSize:1
                        value:controls.item(modelData.key).value || 0
                        // Stay enabled while the helper runs so keyboard focus and
                        // dragging continue; the service applies the newest value.
                        enabled:controls.item(modelData.key).available
                        function commit(){commitDelay.stop();controls.service.set(modelData.key,Math.round(value))}
                        onPressedChanged:{if(pressed)controls.themeSound("sltp");else if(enabled)commit()}
                        // Wheel movement has no press/release pair.
                        onMoved:if(!pressed)commitDelay.restart()
                        Timer{id:commitDelay;interval:300;onTriggered:slider.commit()}
                        background: Rectangle {
                            x:slider.leftPadding;y:slider.height/2-3
                            width:slider.availableWidth;height:6;color:"#aaa";border.color:"#666"
                            Rectangle{x:1;y:1;width:parent.width-2;height:1;color:"#777"}
                        }
                        handle: Rectangle {
                            x:slider.leftPadding+slider.visualPosition*(slider.availableWidth-width)
                            y:slider.height/2-height/2;width:12;height:22
                            color:slider.pressed ? "#aaa" : "#ddd";border.color:slider.enabled ? "#222" : "#999"
                            Rectangle{x:1;y:1;width:parent.width-2;height:1;color:"white"}
                            Rectangle{x:1;y:1;width:1;height:parent.height-2;color:"white"}
                            Rectangle{x:4;y:5;width:1;height:11;color:"#888"}
                            Rectangle{x:7;y:5;width:1;height:11;color:"#888"}
                        }
                    }
                    PlatinumButton {
                        visible:modelData.key==="volume"
                        objectName:"quick-mute"
                        text:controls.item("volume").muted ? "Unmute" : "Mute"
                        implicitWidth:72;implicitHeight:25
                        enabled:controls.item("volume").available && !controls.service.busy
                        onClicked:controls.service.set("mute",controls.item("volume").muted ? 0 : 1)
                    }
                }
            }
        }
        Repeater {
            model:[{key:"wifi",label:"Wi-Fi"}]
            delegate: Row {
                required property var modelData
                spacing:8
                Text {
                    width:210;height:28;verticalAlignment:Text.AlignVCenter;font.pixelSize:12
                    text:modelData.label+"   "+(controls.item(modelData.key).available
                         ? (controls.item(modelData.key).enabled ? "On" : "Off") : "Unavailable")
                }
                PlatinumButton {
                    objectName:"quick-"+modelData.key
                    implicitWidth:72
                    text:controls.item(modelData.key).enabled ? "Turn Off" : "Turn On"
                    enabled:controls.item(modelData.key).available && !controls.service.busy
                    onClicked:controls.service.set(modelData.key,controls.item(modelData.key).enabled ? 0 : 1)
                }
            }
        }
        Row {
            spacing:8
            Text {
                width:210;height:28;verticalAlignment:Text.AlignVCenter;font.pixelSize:12
                text:"Bluetooth   "+(controls.bluetoothReady ? (controls.bluetooth.powered ? "On" : "Off") : "Unavailable")
            }
            PlatinumButton {
                objectName:"quick-bluetooth"
                implicitWidth:72
                text:controls.bluetoothReady && controls.bluetooth.powered ? "Turn Off" : "Turn On"
                enabled:controls.bluetoothReady
                onClicked:controls.bluetooth.setPowered(!controls.bluetooth.powered)
            }
        }
        Text {
            width:parent.width;wrapMode:Text.Wrap;font.pixelSize:11
            color:controls.service.error ? "#8b1010" : "#444"
            text:controls.service.error || (controls.service.busy ? "Reading settings…" : "Brightness controls the active display.")
        }
        PlatinumButton {
            objectName:"quick-refresh";text:"Refresh";enabled:!controls.service.busy
            onClicked:controls.service.refresh()
        }
    }
}
