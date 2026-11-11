import QtQuick
import QtQuick.Controls

// Fingerprints, a panel of the Control Panels window (docs/fingerprints.md; Lily Hewitt, 2026-11-10:
// "i wanna use my finger print to log in"). Mac OS 9 had no fingerprint reader, so this is an original
// panel in the Platinum style: your fingers (added and deleted with fprintd), then whether Log In, the
// lock and password boxes accept one. The password always keeps working.
Item {
    id: panel
    required property var service
    objectName: "fingerprints-panel"
    readonly property string panelId: "fingerprints"
    readonly property string title: "Fingerprints"
    readonly property string glyph: "fingerprint"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 500
    implicitHeight: body.implicitHeight
    readonly property bool ready: Boolean(service) && service.installed && service.reader !== ""
    readonly property var fingers: service ? service.fingers : []
    readonly property var allFingers: ["right-index-finger", "right-middle-finger", "right-ring-finger",
        "right-little-finger", "right-thumb", "left-index-finger", "left-middle-finger", "left-ring-finger",
        "left-little-finger", "left-thumb"]
    readonly property var addable: allFingers.filter(function(f) { return fingers.indexOf(f) < 0 })
    property string selected: ""
    property string toAdd: ""
    readonly property string nextFinger: addable.indexOf(toAdd) >= 0 ? toAdd : (addable.length ? addable[0] : "")
    function titleOf(finger) {
        return finger.split("-").map(function(w) { return w.charAt(0).toUpperCase() + w.slice(1) }).join(" ")
    }
    function edit() { if (host) host.show(panelId) }
    onShownChanged: {
        if (!service) return
        if (shown) service.refresh()
        else service.cancel()  // Never keep the reader while the panel is away.
    }
    // A finger just added is chosen once fprintd's fresh list has it.
    property string added: ""
    onFingersChanged: {
        if (added !== "" && fingers.indexOf(added) >= 0) { selected = added; added = "" }
        else if (fingers.indexOf(selected) < 0) selected = ""
    }
    Connections {
        target: panel.service
        function onEnrolled(finger) {
            if (panel.fingers.indexOf(finger) >= 0) panel.selected = finger
            else panel.added = finger
        }
    }
    Column {
        id: body
        width: Math.min(parent.width, 520)
        spacing: 12
        Row {
            spacing: 10
            StripGlyph { kind: "fingerprint"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                objectName: "fingerprints-reader"
                width: 330; anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12; textFormat: Text.PlainText; wrapMode: Text.Wrap
                text: !panel.service ? ""
                      : !panel.service.installed ? "Fingerprint support isn’t installed."
                      : panel.service.reader === "" ? "No fingerprint reader was found."
                      : "Reader: " + panel.service.reader
            }
            PlatinumButton {
                objectName: "fingerprints-install"
                visible: Boolean(panel.service) && !panel.service.installed
                implicitWidth: 86
                text: "Install…"
                enabled: !panel.service.changing
                onClicked: panel.service.installSupport()
            }
        }
        PlatinumGroup {
            title: "My Fingerprints"
            spacing: 8
            Rectangle {
                width: parent.width; height: 108
                color: "white"; border.color: "#555"
                ListView {
                    id: list
                    objectName: "fingerprints-list"
                    anchors.fill: parent; anchors.margins: 1; clip: true
                    model: panel.fingers
                    ScrollBar.vertical: PlatinumScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property string modelData
                        readonly property bool chosen: modelData === panel.selected
                        objectName: "fingerprint-" + modelData
                        width: list.width; height: 26
                        color: chosen ? "#202050" : "white"
                        Rectangle {
                            x: 6; y: 2; width: 22; height: 22; color: "#ddd"; border.color: "#222"
                            StripGlyph { x: 1; y: 1; kind: "fingerprint" }
                        }
                        Text {
                            x: 36; anchors.verticalCenter: parent.verticalCenter
                            text: panel.titleOf(row.modelData); textFormat: Text.PlainText
                            font.pixelSize: 12; color: row.chosen ? "white" : "#111"
                        }
                        MouseArea { anchors.fill: parent; onClicked: panel.selected = row.modelData }
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        visible: list.count === 0; font.pixelSize: 12; color: "#555"; textFormat: Text.PlainText
                        text: panel.ready ? "No fingerprints yet. Choose a finger below and click Add." : ""
                    }
                }
            }
            Item {
                width: parent.width; height: 24
                visible: !panel.service || panel.service.enrolling === ""
                Row {
                    spacing: 8
                    PlatinumPopup {
                        objectName: "fingerprints-finger"
                        implicitWidth: 170
                        enabled: panel.ready && !panel.service.busy && panel.addable.length > 0
                        choices: panel.addable.map(function(f) { return {name: panel.titleOf(f), value: f} })
                        current: panel.nextFinger
                        onChosen: function(value) { panel.toAdd = value }
                    }
                    PlatinumButton {
                        objectName: "fingerprints-add"
                        text: "Add"
                        enabled: panel.ready && !panel.service.busy && panel.nextFinger !== ""
                        onClicked: panel.service.enroll(panel.nextFinger)
                    }
                }
                PlatinumButton {
                    anchors.right: parent.right
                    objectName: "fingerprints-delete"
                    text: "Delete"
                    enabled: panel.ready && !panel.service.busy && panel.selected !== ""
                    onClicked: panel.service.remove(panel.selected)
                }
            }
            // Adding a finger: what to do, and how many of the reader's scans are done.
            Column {
                objectName: "fingerprints-enrolling"
                width: parent.width; spacing: 8
                visible: Boolean(panel.service) && panel.service.enrolling !== ""
                Text {
                    width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; font.bold: true
                    textFormat: Text.PlainText
                    text: panel.service ? "Adding your " + panel.titleOf(panel.service.enrolling).toLowerCase() : ""
                }
                Rectangle {
                    objectName: "fingerprints-progress"
                    width: parent.width; height: 12; border.color: "#333"; color: "#eee"
                    Rectangle {
                        x: 1; y: 1; height: parent.height - 2; color: "#6666cc"
                        width: !panel.service || panel.service.stages <= 0 ? 0
                               : (parent.width - 2) * Math.min(1, panel.service.stage / panel.service.stages)
                    }
                }
                Item {
                    width: parent.width; height: 24
                    Text {
                        objectName: "fingerprints-instruction"
                        width: parent.width - 90; anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
                        text: panel.service ? panel.service.message : ""
                    }
                    PlatinumButton {
                        anchors.right: parent.right
                        objectName: "fingerprints-cancel"
                        text: "Cancel"
                        onClicked: panel.service.cancel()
                    }
                }
            }
        }
        PlatinumGroup {
            title: "Use My Fingerprint"
            spacing: 2
            PlatinumCheckBox {
                objectName: "fingerprints-login"
                text: "To log in, unlock the screen and approve password boxes"
                enabled: Boolean(panel.service) && panel.service.loginAvailable && !panel.service.changing
                         && (panel.service.login || panel.fingers.length > 0)
                checked: Boolean(panel.service) && panel.service.login
                onClicked: { checked = Qt.binding(function() { return Boolean(panel.service) && panel.service.login })
                             panel.service.setLogin(!panel.service.login, panel.service.sudo) }
            }
            PlatinumCheckBox {
                objectName: "fingerprints-sudo"
                text: "For sudo in Terminal"
                enabled: Boolean(panel.service) && panel.service.loginAvailable && !panel.service.changing
                         && (panel.service.sudo || panel.fingers.length > 0)
                checked: Boolean(panel.service) && panel.service.sudo
                onClicked: { checked = Qt.binding(function() { return Boolean(panel.service) && panel.service.sudo })
                             panel.service.setLogin(panel.service.login, !panel.service.sudo) }
            }
            Text {
                objectName: "fingerprints-login-detail"
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                topPadding: 4
                text: "Your password always works too. At Log In, press Return without typing a password, " +
                      "then touch the reader; the locked screen takes your finger at once. After logging in " +
                      "with a finger, Keychain stays locked until you unlock it with your password."
            }
        }
        Text {
            objectName: "fingerprints-message"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
            color: panel.service && panel.service.error ? "#8b1010" : "#333"
            text: !panel.service ? ""
                  : panel.service.error ? panel.service.error
                  : panel.service.changing ? (panel.service.message || "Waiting for the administrator’s password…")
                  : panel.service.enrolling === "" && panel.service.message ? panel.service.message
                  : !panel.service.installed ? "Click Install… to add fingerprint support (fprintd). An administrator’s password is needed."
                  : ""
        }
    }
}
