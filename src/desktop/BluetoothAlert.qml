import QtQuick
import QtQuick.Controls

// BlueZ's pairing questions (docs/bluetooth.md) as Platinum alerts: compare a code, type a PIN
// or passkey, type a code on the device, or allow a device to pair or connect. The alert follows
// the service's request: it opens for each question and closes when BlueZ withdraws it.
ShellDialog {
    id: alert
    required property var service
    objectName: "bluetooth-alert"
    alertSound: true
    title: "Bluetooth"
    width: 440
    standardButtons: Dialog.NoButton
    footer: Item {}
    readonly property var question: service ? service.request : ({})
    readonly property string kind: question && question.kind ? question.kind : ""
    readonly property string name: question && question.name ? question.name : ""
    readonly property bool typed: kind === "pin" || kind === "passkey"
    readonly property bool showsCode: kind === "confirm" || kind === "display-pin" || kind === "display-passkey"
    readonly property bool informational: kind === "display-pin" || kind === "display-passkey"
    // The question this alert is answering; 0 once answered.
    property var openId: 0
    function prompt() {
        if (kind === "pin") return "“" + name + "” wants to pair with this computer. Enter its PIN. Many headsets and speakers use 0000."
        if (kind === "passkey") return "Enter the passkey shown on “" + name + "”."
        if (kind === "confirm") return "Does “" + name + "” show this code? Pair only if the codes match."
        if (kind === "display-pin") return "To pair with “" + name + "”, type this PIN on it, then press Return or Enter."
        if (kind === "display-passkey") return "To pair with “" + name + "”, type this code on it, then press Return or Enter."
        if (kind === "authorize-pairing") return "“" + name + "” wants to pair with this computer."
        if (kind === "authorize-service") return "“" + name + "” wants to connect to this computer."
        return ""
    }
    function respond(accept) {
        if (!openId) return
        const id = openId
        openId = 0
        if (service && service.request.id === id) service.answer(accept, field.text, always.checked)
    }
    function sync() {
        const id = question && question.id ? question.id : 0
        if (id && id !== openId) {
            // Still closing after the previous answer (headsets ask about several services in
            // a row): the next question opens once the alert has closed.
            if (visible && !openId) return
            const reopen = !visible
            openId = id
            field.text = ""
            always.checked = false
            if (reopen) open()
        } else if (!id && openId) {
            // BlueZ withdrew the question: nothing to reply.
            openId = 0
            close()
        }
    }
    // After the binding above has the new question, whichever order notifications arrive in.
    onQuestionChanged: sync()
    // Escape or closing by any other way declines.
    Connections {
        target: alert
        function onClosed() {
            alert.respond(false)
            Qt.callLater(alert.sync)
        }
    }
    onOpened: if (typed) field.forceActiveFocus()
    // Return answers yes, except while a code is shown for typing on the device: the keys
    // pressed there may well be Return, and must not cancel the pairing here.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: alert.visible && !alert.informational && (!alert.typed || field.acceptableInput)
        onActivated: { alert.respond(true); alert.close() }
    }
    contentItem: Column {
        spacing: 14
        Row {
            spacing: 14
            Rectangle {
                width: 36; height: 36; color: "#ddd"; border.color: "#222"
                StripGlyph {anchors.centerIn: parent; width: 28; height: 28; kind: "bluetooth"}
            }
            Column {
                width: alert.availableWidth - 50; spacing: 10
                Text {
                    objectName: "bluetooth-alert-text"
                    width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12
                    text: alert.prompt()
                }
                Text {
                    objectName: "bluetooth-alert-code"
                    visible: alert.showsCode
                    text: alert.question && alert.question.code ? alert.question.code : ""
                    textFormat: Text.PlainText; font.pixelSize: 26; font.letterSpacing: 4
                }
                Text {
                    visible: alert.kind === "display-passkey" && alert.question.entered >= 0
                    text: alert.question.entered + " of " + String(alert.question.code || "").length + " digits typed"
                    textFormat: Text.PlainText; font.pixelSize: 11; color: "#444"
                }
                TextField {
                    id: field
                    objectName: "bluetooth-alert-field"
                    visible: alert.typed
                    // Typing goes straight to the field when the alert opens.
                    focus: alert.typed
                    width: 180; height: 28; font.pixelSize: 14; selectByMouse: true
                    color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                    background: Rectangle {color: "white"; border.color: "#555"}
                    maximumLength: alert.kind === "passkey" ? 6 : 16
                    validator: RegularExpressionValidator {
                        regularExpression: alert.kind === "passkey" ? /[0-9]{1,6}/ : /[!-~]{1,16}/
                    }
                    onAccepted: { alert.respond(true); alert.close() }
                }
                PlatinumCheckBox {
                    id: always
                    objectName: "bluetooth-alert-always"
                    visible: alert.kind === "authorize-service"
                    text: "Always allow this device"
                }
            }
        }
        Row {
            anchors.right: parent.right
            spacing: 10
            PlatinumButton {
                objectName: "bluetooth-alert-cancel"
                text: alert.kind.startsWith("authorize") ? "Don’t Allow" : "Cancel"
                onClicked: { alert.respond(false); alert.close() }
            }
            PlatinumButton {
                objectName: "bluetooth-alert-accept"
                visible: !alert.informational
                text: alert.kind === "authorize-service" ? "Allow" : alert.typed ? "OK" : "Pair"
                defaultAction: true
                enabled: !alert.typed || field.acceptableInput
                onClicked: { alert.respond(true); alert.close() }
            }
        }
    }
}
