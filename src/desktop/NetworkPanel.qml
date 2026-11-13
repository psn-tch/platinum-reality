import QtQuick
import QtQuick.Controls

// Network, a panel of the Control Panels window (docs/wifi.md): Wi-Fi on or off, the networks
// iwd sees with their signal and lock, Join, Disconnect and Forget, and this computer's
// connections with their addresses. Passwords are asked by the Wi-Fi alert (WifiAlert.qml);
// Forget asks first through the desktop's confirmation.
Item {
    id: panel
    // iwd (WifiService) and the Quick Controls helper, which turns the Wi-Fi radio on and off.
    required property var service
    property var controls: null
    signal forgetRequested(string path, string name)
    objectName: "network-panel"
    readonly property string panelId: "network"
    readonly property string title: "Network"
    readonly property string glyph: "wifi"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property var radio: controls && controls.state && controls.state.wifi ? controls.state.wifi : ({available: false})
    readonly property bool ready: Boolean(service) && service.available
    readonly property var networks: ready ? service.networks : []
    readonly property var connected: networks.find(function(n) { return n.connected }) || null
    // Rows are chosen by object path: the list re-sorts as signal strengths change.
    property string selected: ""
    readonly property var current: networks.find(function(n) { return n.path === panel.selected }) || null
    property var connections: []
    property var dns: ({servers: [], domains: []})
    // TCP/IP's Connect via: the connection shown, by interface name; the one with a router first.
    property string via: ""
    readonly property var shownConnection: connections.find(function(c) { return c.name === panel.via })
                                           || connections.find(function(c) { return c.router }) || connections[0] || null
    function edit() { if (host) host.show(panelId) }
    function refreshConnections() {
        connections = service && service.connections ? service.connections() : []
        dns = service && service.nameServers ? service.nameServers() : ({servers: [], domains: []})
    }
    function join(network) { if (network && !network.connected && network.busy === "") service.connectNetwork(network.path) }
    onShownChanged: if (shown) {
        if (controls) controls.refresh()
        if (ready) service.scan()
        refreshConnections()
    }
    // Addresses arrive a moment after joining; follow them while the panel is on screen.
    Timer { interval: 3000; repeat: true; running: panel.shown; onTriggered: panel.refreshConnections() }
    onConnectedChanged: refreshConnections()
    Column {
        id: body
        width: parent.width
        spacing: 10
        Row {
            spacing: 10
            StripGlyph { kind: "wifi"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                objectName: "network-wifi-state"
                width: 360; anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12; textFormat: Text.PlainText; wrapMode: Text.Wrap
                text: !panel.radio.available ? "Wi-Fi is not available on this computer."
                      : !panel.radio.enabled ? "Wi-Fi: Off"
                      : panel.connected ? "Wi-Fi: On, connected to “" + panel.connected.name + "”"
                      : "Wi-Fi: On, not connected"
            }
            PlatinumButton {
                objectName: "network-wifi-power"
                implicitWidth: 86
                text: panel.radio.enabled ? "Turn Off" : "Turn On"
                enabled: panel.radio.available && Boolean(panel.controls) && !panel.controls.busy
                onClicked: panel.controls.set("wifi", panel.radio.enabled ? 0 : 1)
            }
        }
        Text { text: "Networks"; font.pixelSize: 12; font.bold: true; visible: panel.radio.enabled === true }
        Rectangle {
            visible: panel.radio.enabled === true
            width: parent.width; height: 196; color: "white"; border.color: "#555"
            ListView {
                id: list
                objectName: "network-list"
                anchors.fill: parent; anchors.margins: 1; clip: true
                model: panel.networks
                ScrollBar.vertical: PlatinumScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool chosen: modelData.path === panel.selected
                    objectName: "network-row-" + modelData.name
                    width: list.width; height: 38
                    color: chosen ? "#202050" : "white"
                    Column {
                        x: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 70; spacing: 2
                        Text {
                            width: parent.width; elide: Text.ElideRight; font.pixelSize: 12; textFormat: Text.PlainText
                            font.bold: row.modelData.connected; color: row.chosen ? "white" : "#111"
                            text: row.modelData.name
                        }
                        Text {
                            objectName: "network-status-" + row.modelData.name
                            width: parent.width; elide: Text.ElideRight; font.pixelSize: 11; textFormat: Text.PlainText
                            color: row.chosen ? "#ccc" : "#555"
                            text: row.modelData.busy === "joining" ? "Joining…"
                                  : row.modelData.connected ? "Connected"
                                  : row.modelData.known ? "Saved" : row.modelData.secure ? "Secured" : "Open"
                        }
                    }
                    // Its lock (secured) and four signal bars, as in the Control Strip menu.
                    Row {
                        // Clear of the list's scroll bar.
                        anchors.right: parent.right; anchors.rightMargin: 26; anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        readonly property color on: row.chosen ? "white" : "black"
                        readonly property color off: row.chosen ? "#9999cc" : "#aaa"
                        Item { width: 9; height: 11; anchors.bottom: parent.bottom; visible: row.modelData.secure
                            Rectangle { x: 1.5; y: 0; width: 6; height: 7; radius: 3; color: "transparent"; border.width: 1.5; border.color: parent.parent.on }
                            Rectangle { x: 0; y: 4.5; width: 9; height: 6.5; color: parent.parent.on } }
                        Item { width: 15; height: 12; anchors.bottom: parent.bottom
                            readonly property int bars: row.modelData.bars || 0
                            Repeater {
                                model: 4
                                Rectangle {
                                    required property int index
                                    x: index * 4; y: 9 - index * 3; width: 3; height: 3 + index * 3
                                    color: parent.bars > index ? parent.parent.on : parent.parent.off
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.selected = row.modelData.path
                        onDoubleClicked: { panel.selected = row.modelData.path; panel.join(row.modelData) }
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#555"
                    visible: list.count === 0; textFormat: Text.PlainText
                    text: !panel.ready ? "Wi-Fi networks are not available." : panel.service.scanning ? "Looking for networks…" : "No networks found."
                }
            }
        }
        Item {
            visible: panel.radio.enabled === true
            width: parent.width; height: 28
            Row {
                spacing: 10
                PlatinumButton {
                    objectName: "network-join"
                    readonly property bool leaving: panel.current !== null && panel.current.connected
                    text: leaving ? "Disconnect" : "Join"
                    defaultAction: enabled && !leaving
                    enabled: panel.ready && panel.current !== null && panel.current.busy === ""
                    onClicked: leaving ? panel.service.disconnectNetwork() : panel.join(panel.current)
                }
                PlatinumButton {
                    objectName: "network-forget"
                    text: "Forget…"
                    enabled: panel.ready && panel.current !== null && panel.current.known
                    onClicked: panel.forgetRequested(panel.current.path, panel.current.name)
                }
            }
            PlatinumButton {
                anchors.right: parent.right
                objectName: "network-scan"
                text: panel.ready && panel.service.scanning ? "Scanning…" : "Scan"
                enabled: panel.ready && !panel.service.scanning
                onClicked: panel.service.scan()
            }
        }
        Text {
            objectName: "network-message"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
            color: panel.ready && panel.service.message ? "#8b1010" : "#333"
            text: panel.ready && panel.service.message ? panel.service.message
                  : panel.radio.enabled === true ? "Double-click a network to join it. Networks you have joined are saved and joined again by themselves."
                  : ""
        }
        // TCP/IP, laid out as 9.2.2's (captured 2026-11-12): Connect via, then the connection's
        // addresses and router, and the name servers and search domains in their boxes. Read-only:
        // the connection is set up by Wi-Fi above (or by the system for a cable).
        PlatinumGroup {
            title: "TCP/IP"
            width: parent.width
            Column {
                objectName: "network-connections"
                width: parent.width; spacing: 8
                Row {
                    spacing: 10
                    visible: panel.connections.length > 0
                    Text { width: 120; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter
                           text: "Connect via:"; font.pixelSize: 12; textFormat: Text.PlainText }
                    PlatinumPopup {
                        objectName: "network-connect-via"
                        implicitWidth: 220
                        choices: panel.connections.map(function(c) { return {name: c.kind + " (" + c.name + ")", value: c.name} })
                        current: panel.shownConnection ? panel.shownConnection.name : ""
                        onChosen: function(value) { panel.via = value }
                    }
                }
                Grid {
                    visible: Boolean(panel.shownConnection)
                    columns: 2; columnSpacing: 10; rowSpacing: 6
                    Repeater {
                        model: {
                            const c = panel.shownConnection
                            if (!c) return []
                            const v4 = c.addresses.filter(function(a) { return a.version === 4 })
                            const v6 = c.addresses.filter(function(a) { return a.version === 6 })
                            let rows = [["IP Address:", v4.length ? v4[0].address : "None"],
                                        ["Subnet mask:", v4.length && v4[0].netmask ? v4[0].netmask : "None"],
                                        ["Router address:", c.router || "None"]]
                            for (const a of v6) rows.push(["IPv6 address:", a.address])
                            if (c.hardware) rows.push(["Hardware address:", c.hardware])
                            return rows.reduce(function(all, r) { return all.concat(r) }, [])
                        }
                        Text {
                            required property var modelData
                            required property int index
                            objectName: index % 2 === 1 ? "network-tcpip-value-" + Math.floor(index / 2) : ""
                            width: index % 2 === 0 ? 120 : 330
                            horizontalAlignment: index % 2 === 0 ? Text.AlignRight : Text.AlignLeft
                            text: modelData; font.pixelSize: 12; textFormat: Text.PlainText
                            color: index % 2 === 0 ? "#111" : "#333"; wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Row {
                    visible: Boolean(panel.shownConnection)
                    spacing: 14
                    Repeater {
                        model: [["Name server addr.:", panel.dns.servers, "network-name-servers"],
                                ["Search domains:", panel.dns.domains, "network-search-domains"]]
                        Column {
                            required property var modelData
                            spacing: 3
                            Text { text: modelData[0]; font.pixelSize: 12; textFormat: Text.PlainText }
                            Rectangle {
                                width: 210; height: 70; color: "white"; border.color: "#555"
                                Text {
                                    objectName: modelData[2]
                                    x: 6; y: 4; width: parent.width - 12
                                    text: modelData[1].join("\n"); font.pixelSize: 12; textFormat: Text.PlainText
                                    elide: Text.ElideRight; maximumLineCount: 4
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: panel.connections.length === 0
                    font.pixelSize: 12; color: "#555"; textFormat: Text.PlainText
                    text: "Not connected to a network."
                }
            }
        }
    }
}
