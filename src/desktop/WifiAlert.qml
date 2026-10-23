import QtQuick
import QtQuick.Controls

// iwd's password questions (docs/wifi.md) as a Platinum alert: the password of a network being
// joined, or a user name and password for a company or school network. The alert follows the
// service's request: it opens for each question and closes when iwd withdraws it.
ShellDialog {
    id: alert
    required property var service
    objectName: "wifi-alert"
    alertSound: true
    title: "Wi-Fi"
    width: 440
    standardButtons: Dialog.NoButton
    footer: Item {}
    readonly property var question: service ? service.request : ({})
    readonly property string kind: question && question.kind ? question.kind : ""
    readonly property string name: question && question.name ? question.name : ""
    readonly property bool asksUser: kind === "user-password"
    // WPA: 8 to 63 characters, or the 64 hexadecimal digits of the key itself.
    readonly property bool acceptable: password.text.length > 0 && (!asksUser || user.text.trim().length > 0) &&
        (kind !== "passphrase" || question.type !== "psk" ||
         (password.text.length >= 8 && password.text.length <= 63) || /^[0-9A-Fa-f]{64}$/.test(password.text))
    // The question this alert is answering; 0 once answered.
    property var openId: 0
    function prompt() {
        if (kind === "passphrase") return "The network “" + name + "” requires a password."
        if (kind === "user-password") return "The network “" + name + "” requires a user name and password."
        if (kind === "password") return "Enter the password for “" + question.user + "” on the network “" + name + "”."
        if (kind === "private-key") return "Enter the passphrase of the private key for the network “" + name + "”."
        return ""
    }
    function respond(accept) {
        if (!openId) return
        const id = openId
        openId = 0
        if (service && service.request.id === id) service.answer(accept, password.text, user.text.trim())
        password.text = ""
    }
    function sync() {
        const id = question && question.id ? question.id : 0
        if (id && id !== openId) {
            if (visible && !openId) return
            const reopen = !visible
            openId = id
            password.text = ""
            user.text = ""
            show.checked = false
            if (reopen) open()
        } else if (!id && openId) {
            // iwd withdrew the question: nothing to reply.
            openId = 0
            password.text = ""
            close()
        }
    }
    onQuestionChanged: sync()
    // Escape or closing by any other way cancels joining.
    Connections {
        target: alert
        function onClosed() {
            alert.respond(false)
            Qt.callLater(alert.sync)
        }
    }
    onOpened: (asksUser ? user : password).forceActiveFocus()
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: alert.visible && alert.acceptable
        onActivated: { alert.respond(true); alert.close() }
    }
    contentItem: Column {
        spacing: 14
        Row {
            spacing: 14
            Rectangle {
                width: 36; height: 36; color: "#ddd"; border.color: "#222"
                StripGlyph {anchors.centerIn: parent; width: 28; height: 28; kind: "wifi"}
            }
            Column {
                width: alert.availableWidth - 50; spacing: 10
                Text {
                    objectName: "wifi-alert-text"
                    width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12
                    text: alert.prompt()
                }
                Grid {
                    columns: 2; columnSpacing: 8; rowSpacing: 8; verticalItemAlignment: Grid.AlignVCenter
                    Text {visible: alert.asksUser; text: "Name:"; font.pixelSize: 12}
                    TextField {
                        id: user
                        objectName: "wifi-alert-user"
                        visible: alert.asksUser
                        width: 220; height: 26; font.pixelSize: 13; selectByMouse: true
                        color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                        background: Rectangle {color: "white"; border.color: "#555"}
                        maximumLength: 256
                        onAccepted: password.forceActiveFocus()
                    }
                    Text {text: "Password:"; font.pixelSize: 12}
                    TextField {
                        id: password
                        objectName: "wifi-alert-password"
                        width: 220; height: 26; font.pixelSize: 13; selectByMouse: true
                        echoMode: show.checked ? TextInput.Normal : TextInput.Password
                        color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                        background: Rectangle {color: "white"; border.color: "#555"}
                        maximumLength: 256
                        onAccepted: if (alert.acceptable) { alert.respond(true); alert.close() }
                    }
                }
                PlatinumCheckBox {
                    id: show
                    objectName: "wifi-alert-show"
                    text: "Show password"
                }
            }
        }
        Row {
            anchors.right: parent.right
            spacing: 10
            PlatinumButton {
                objectName: "wifi-alert-cancel"
                text: "Cancel"
                onClicked: { alert.respond(false); alert.close() }
            }
            PlatinumButton {
                objectName: "wifi-alert-accept"
                text: "Join"
                defaultAction: true
                enabled: alert.acceptable
                onClicked: { alert.respond(true); alert.close() }
            }
        }
    }
}
