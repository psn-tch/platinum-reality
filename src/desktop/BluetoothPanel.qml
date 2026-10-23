import QtQuick
import QtQuick.Controls

// The Bluetooth control panel (docs/bluetooth.md). Pairings live in BlueZ, so devices paired
// under KDE are listed here as they are. Add Device looks for devices in pairing mode; pairing
// questions appear as alerts (BluetoothAlert.qml) whether or not this panel is open.
ShellDialog {
    id: panel
    required property var service
    signal forgetRequested(string path, string name)
    objectName: "bluetooth-panel"
    title: adding ? "Add Bluetooth Device" : "Bluetooth"
    width: 500
    standardButtons: Dialog.Close
    property bool adding: false
    // Rows are chosen by object path: the lists re-sort as devices are renamed or added.
    property string selected: ""
    property string candidate: ""
    readonly property bool ready: Boolean(service) && service.available
    readonly property bool powered: ready && service.powered
    readonly property var devices: ready ? service.devices : []
    readonly property var nearby: ready ? service.nearby : []
    function find(list, path) {
        for (const device of list) if (device.path === path) return device
        return null
    }
    readonly property var current: find(devices, selected)
    readonly property var currentNearby: find(nearby, candidate)
    readonly property var pairing: nearby.find(function(d) {return d.busy === "pairing"}) || null
    function edit() {
        if (visible || sessionManager.shellModal) return
        adding = false
        open()
    }
    function addDevice() {
        if (!visible) {
            if (sessionManager.shellModal) return
            open()
        }
        candidate = ""
        adding = true
        if (powered) service.startDiscovery()
    }
    function back() {
        adding = false
        if (service) service.stopDiscovery()
    }
    function toggle(device) {
        if (!device || device.busy !== "") return
        if (device.connected) service.disconnectDevice(device.path)
        else service.connectDevice(device.path)
    }
    function pair(device) {
        if (device && device.busy === "" && !pairing) service.pairDevice(device.path)
    }
    function status(device) {
        if (device.busy === "connecting") return "Connecting…"
        if (device.busy === "disconnecting") return "Disconnecting…"
        if (device.busy === "forgetting") return "Forgetting…"
        if (device.busy === "pairing") return "Pairing…"
        if (adding) return device.type
        return device.type + " — " + (device.connected ? "Connected" + (device.battery >= 0 ? ", battery " + device.battery + "%" : "")
                                                       : "Not connected")
    }
    onAboutToHide: if (adding) back()
    Connections {
        target: panel.service
        // A device paired from Add Device is trusted and connecting: show it in the list.
        function onPaired(path) {
            panel.adding = false
            panel.selected = path
        }
    }
    contentItem: Column {
        spacing: 10
        Row {
            spacing: 10
            StripGlyph {kind: "bluetooth"; anchors.verticalCenter: parent.verticalCenter}
            Text {
                objectName: "bluetooth-power-state"
                width: 340; anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12; textFormat: Text.PlainText; wrapMode: Text.Wrap
                text: !panel.ready ? "Bluetooth is not available on this computer."
                      : "Bluetooth: " + (panel.powered ? "On" : "Off")
            }
            PlatinumButton {
                objectName: "bluetooth-power"
                implicitWidth: 86
                text: panel.powered ? "Turn Off" : "Turn On"
                enabled: panel.ready
                onClicked: panel.service.setPowered(!panel.powered)
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            visible: panel.powered && panel.service.computerName !== ""
            text: panel.adding ? "Turn on the device and put it in pairing mode. Devices found nearby appear below."
                               : "Other devices see this computer as “" + panel.service.computerName + "”."
        }
        Rectangle {
            width: parent.width; height: 220
            color: "white"; border.color: "#555"
            ListView {
                id: list
                objectName: panel.adding ? "bluetooth-nearby" : "bluetooth-devices"
                anchors.fill: parent; anchors.margins: 1; clip: true
                model: panel.adding ? panel.nearby : panel.devices
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool chosen: modelData.path === (panel.adding ? panel.candidate : panel.selected)
                    objectName: "bluetooth-device-" + modelData.name
                    width: list.width; height: 42
                    color: chosen ? "#202050" : "white"
                    Rectangle {
                        x: 9; y: 9; width: 24; height: 24; color: "#ddd"; border.color: "#222"
                        StripGlyph {x: 2; y: 2; kind: row.modelData.kind}
                    }
                    Column {
                        x: 44; y: 5; width: parent.width - 54; spacing: 2
                        Text {
                            width: parent.width; text: row.modelData.name; textFormat: Text.PlainText
                            font.pixelSize: 12; elide: Text.ElideRight; color: row.chosen ? "white" : "#111"
                        }
                        Text {
                            objectName: "bluetooth-status-" + row.modelData.name
                            width: parent.width; text: panel.status(row.modelData); textFormat: Text.PlainText
                            font.pixelSize: 11; elide: Text.ElideRight; color: row.chosen ? "#ddd" : "#555"
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: if (panel.adding) panel.candidate = row.modelData.path; else panel.selected = row.modelData.path
                        onDoubleClicked: if (panel.adding) panel.pair(row.modelData); else panel.toggle(row.modelData)
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                    visible: list.count === 0; font.pixelSize: 12; color: "#555"; textFormat: Text.PlainText
                    text: !panel.ready ? "" : !panel.powered ? "Bluetooth is off."
                          : panel.adding ? (panel.service.discovering ? "Looking for devices…" : "No devices found.")
                          : "No devices are paired with this computer."
                }
            }
        }
        Item {
            width: parent.width; height: 28
            Row {
                spacing: 10
                visible: !panel.adding
                PlatinumButton {
                    objectName: "bluetooth-connect"
                    text: panel.current && panel.current.connected ? "Disconnect" : "Connect"
                    enabled: panel.powered && panel.current !== null && panel.current.busy === ""
                    onClicked: panel.toggle(panel.current)
                }
                PlatinumButton {
                    objectName: "bluetooth-forget"
                    text: "Forget…"
                    enabled: panel.ready && panel.current !== null && panel.current.busy === ""
                    onClicked: panel.forgetRequested(panel.current.path, panel.current.name)
                }
            }
            PlatinumButton {
                anchors.right: parent.right
                visible: !panel.adding
                objectName: "bluetooth-add"
                text: "Add Device…"
                enabled: panel.powered
                onClicked: panel.addDevice()
            }
            PlatinumButton {
                visible: panel.adding
                objectName: "bluetooth-back"
                text: "Back"
                onClicked: panel.back()
            }
            PlatinumButton {
                anchors.right: parent.right
                visible: panel.adding
                objectName: "bluetooth-pair"
                text: "Pair"
                defaultAction: enabled
                enabled: panel.currentNearby !== null && panel.currentNearby.busy === "" && !panel.pairing
                onClicked: panel.pair(panel.currentNearby)
            }
        }
        Text {
            objectName: "bluetooth-message"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
            color: panel.service && panel.service.message ? "#8b1010" : "#333"
            text: !panel.ready ? ""
                  : panel.service.message ? panel.service.message
                  : panel.pairing ? "Pairing with “" + panel.pairing.name + "”…"
                  : panel.adding ? (panel.service.discovering ? "Looking for devices…" : "")
                  : panel.powered ? "Devices you trust reconnect by themselves when they are turned on."
                  : "Turn Bluetooth on to use your devices."
        }
    }
}
