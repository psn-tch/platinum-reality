import QtQuick

// The Platinum Log In (src/lock/LockScreen.qml) for SDDM on installed systems: the same Mac OS
// 9.2.2-style dialog, with SDDM's users, sessions and power actions. SDDM provides `sddm`,
// `userModel`, `sessionModel`, `config` and `keyboard`; docs/sddm-theme.md.
Rectangle {
    id: screen
    objectName: "sddm-screen"
    width: 1024; height: 768
    color: config.backgroundColor || "#666699"
    focus: true

    property int userIndex: Math.max(0, userModel.lastIndex)
    property int sessionIndex: Math.max(0, sessionModel.lastIndex)
    property string message: ""
    property bool messageIsError: false
    property bool busy: false
    // Fingerprint login is on (the Fingerprints control panel writes fingerprint=true into
    // theme.conf.user): Return with no password asks PAM for a finger instead.
    readonly property bool fingerprint: String(config.fingerprint) === "true"
    property bool usingFinger: false
    // users.count changes as the rows are created; objectAt() alone would not update this.
    readonly property var user: users.count > 0 ? users.objectAt(Math.min(userIndex, users.count - 1)) : null

    // Model rows as objects, so the dialog can show the chosen user and session by index.
    Instantiator {
        id: users
        model: userModel
        delegate: QtObject {
            required property string name
            required property string realName
            required property string icon
        }
    }
    Instantiator {
        id: sessions
        model: sessionModel
        delegate: QtObject {
            required property string name
        }
    }

    Image {
        anchors.fill: parent
        source: config.background ? "file:" + config.background : ""
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    component PlatinumButton: Rectangle {
        id: button
        property string label
        property bool isDefault: false
        property bool usable: true
        signal activated
        readonly property bool down: area.pressed && area.containsMouse
        width: Math.max(86, caption.implicitWidth + 24); height: 24
        color: down ? "#aaa" : "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: button.down ? "#666" : "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: button.down ? "#666" : "white"}
        Rectangle {x: 2; y: parent.height - 2; width: parent.width - 3; height: 1; color: button.down ? "white" : "#777"}
        Rectangle {x: parent.width - 2; y: 2; width: 1; height: parent.height - 3; color: button.down ? "white" : "#777"}
        Rectangle {visible: button.isDefault; anchors.fill: parent; anchors.margins: -4; radius: 4; color: "transparent"
                   border.width: 3; border.color: button.usable ? "#222" : "#888"}
        Text {
            id: caption
            anchors.centerIn: parent
            text: button.label; textFormat: Text.PlainText
            font.family: "Charcoal"; font.pixelSize: 12; color: button.usable ? "#111" : "#888"
        }
        MouseArea {id: area; anchors.fill: parent; enabled: button.usable; onClicked: button.activated()}
    }

    // A Platinum pop-up menu: the current choice in a box, the list below it when clicked.
    component PopupMenu: Rectangle {
        id: popup
        property var names: []
        property int current: 0
        signal chosen(int index)
        width: 200; height: 22; color: "white"; border.color: "#222"; z: 3
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "#ccc"}
        Text {
            x: 8; width: parent.width - 30; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
            text: popup.names[popup.current] || ""; textFormat: Text.PlainText
            font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
            text: "▾"; font.pixelSize: 12; color: "#111"
        }
        MouseArea {anchors.fill: parent; onClicked: list.visible = !list.visible}
        Rectangle {
            id: list
            objectName: popup.objectName + "-list"
            visible: false
            y: parent.height; width: parent.width; height: popup.names.length * 23 + 2
            color: "#eeeeee"; border.color: "#222"
            Rectangle {x: 3; y: 3; z: -1; width: parent.width; height: parent.height; color: "#60000000"}
            Column {
                x: 1; y: 1; width: parent.width - 2
                Repeater {
                    model: popup.names
                    Rectangle {
                        required property string modelData
                        required property int index
                        objectName: popup.objectName + "-" + index
                        width: parent.width; height: 23
                        color: choice.containsMouse ? "#333366" : "transparent"
                        Text {
                            x: 8; width: parent.width - 12; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                            text: (popup.current === index ? "✓ " : "   ") + modelData; textFormat: Text.PlainText
                            font.family: "Charcoal"; font.pixelSize: 12
                            color: choice.containsMouse ? "white" : "#111"
                        }
                        MouseArea {
                            id: choice
                            anchors.fill: parent; hoverEnabled: true
                            onClicked: { list.visible = false; popup.chosen(index) }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: dialog
        objectName: "login-dialog"
        width: 460; height: users.count > 1 ? 300 : 270
        anchors.centerIn: parent
        color: "#dddddd"
        border.color: "#111"
        Rectangle {x: 4; y: 4; z: -1; width: parent.width; height: parent.height; color: "#60000000"}
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}

        Item {
            id: titleBar
            x: 4; y: 3; width: parent.width - 8; height: 19
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width; spacing: 1
                Repeater {
                    model: 6
                    Item {
                        width: titleBar.width; height: 2
                        Rectangle {width: parent.width; height: 1; color: "white"}
                        Rectangle {y: 1; width: parent.width; height: 1; color: "#999"}
                    }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: title.implicitWidth + 16; height: parent.height
                color: "#dddddd"
                Text {
                    id: title
                    anchors.centerIn: parent
                    text: qsTr("Log In"); textFormat: Text.PlainText
                    font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
                }
            }
        }
        Rectangle {x: 4; y: 24; width: parent.width - 8; height: 1; color: "#888"}
        Rectangle {x: 4; y: 25; width: parent.width - 8; height: 1; color: "white"}

        Rectangle {
            id: well
            x: 22; y: 44; width: 72; height: 72
            color: "#eeeeee"; border.color: "#777"
            Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "white"}
            Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "white"}
            Image {
                id: face
                anchors.fill: parent; anchors.margins: 3
                source: screen.user && screen.user.icon ? "file:" + screen.user.icon : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(132, 132)
            }
            Item {
                visible: face.status !== Image.Ready
                anchors.fill: parent
                Rectangle {x: 25; y: 11; width: 22; height: 24; radius: 11; color: "#8a8ab8"; border.color: "#333"}
                Rectangle {x: 14; y: 38; width: 44; height: 30; radius: 14; color: "#8a8ab8"; border.color: "#333"}
                Rectangle {x: 15; y: 56; width: 42; height: 12; color: "#8a8ab8"}
                Rectangle {x: 14; y: 56; width: 1; height: 12; color: "#333"}
                Rectangle {x: 57; y: 56; width: 1; height: 12; color: "#333"}
            }
        }

        Column {
            x: 114; y: 40; width: parent.width - 136; spacing: 7
            // With more than one account, Mac OS 9's Multiple Users asked who is logging in.
            Row {
                visible: users.count > 1
                spacing: 8; z: 4
                Text {
                    text: qsTr("User:"); textFormat: Text.PlainText
                    font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
                    anchors.verticalCenter: parent.verticalCenter
                }
                PopupMenu {
                    objectName: "login-user"
                    width: 220
                    names: {
                        let result = []
                        for (let i = 0; i < users.count; ++i)
                            result.push(users.objectAt(i).realName || users.objectAt(i).name)
                        return result
                    }
                    current: screen.userIndex
                    onChosen: (index) => { screen.userIndex = index; password.text = ""; screen.message = ""; password.forceActiveFocus() }
                }
            }
            Text {
                objectName: "login-name"
                width: parent.width; elide: Text.ElideRight
                text: qsTr("Welcome, %1").arg(screen.user ? (screen.user.realName || screen.user.name) : "")
                textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 13; font.bold: true; color: "#111"
            }
            Text {
                text: qsTr("Enter your password:"); textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
            }
            Rectangle {
                width: parent.width; height: 24
                color: password.readOnly ? "#eeeeee" : "white"; border.color: "#777"
                Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "#aaa"}
                Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "#aaa"}
                TextInput {
                    id: password
                    objectName: "login-password"
                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    echoMode: TextInput.Password; passwordCharacter: "•"
                    font.family: "Charcoal"; font.pixelSize: 13; color: "#111"
                    selectionColor: "#333366"; selectedTextColor: "white"
                    readOnly: screen.busy
                    focus: true
                    clip: true
                    onAccepted: screen.login()
                    Keys.onEscapePressed: clear()
                }
            }
            Text {
                objectName: "login-message"
                width: parent.width; height: 32
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                text: screen.message || (keyboard && keyboard.capsLock && keyboard.capsLock.enabled ? qsTr("Caps Lock is on.")
                      : screen.fingerprint ? qsTr("Or press Return, then touch the fingerprint reader.") : "")
                textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 11
                color: screen.messageIsError ? "#990000" : "#333"
            }
        }

        Row {
            visible: sessions.count > 1
            x: 114; y: dialog.height - 82; spacing: 8; z: 2
            Text {
                text: qsTr("Session:"); textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
                anchors.verticalCenter: parent.verticalCenter
            }
            PopupMenu {
                objectName: "login-session"
                width: 190
                names: {
                    let result = []
                    for (let i = 0; i < sessions.count; ++i)
                        result.push(sessions.objectAt(i).name)
                    return result
                }
                current: screen.sessionIndex
                onChosen: (index) => { screen.sessionIndex = index; password.forceActiveFocus() }
            }
        }

        Row {
            x: 22; anchors.bottom: parent.bottom; anchors.bottomMargin: 18; spacing: 10
            PlatinumButton {
                objectName: "login-restart"
                label: qsTr("Restart")
                visible: sddm.canReboot
                onActivated: sddm.reboot()
            }
            PlatinumButton {
                objectName: "login-shut-down"
                label: qsTr("Shut Down")
                visible: sddm.canPowerOff
                onActivated: sddm.powerOff()
            }
        }
        PlatinumButton {
            objectName: "login-button"
            label: qsTr("Log In")
            isDefault: true
            usable: !screen.busy && (password.text.length > 0 || screen.fingerprint) && !!screen.user
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.bottom: parent.bottom; anchors.bottomMargin: 18
            onActivated: screen.login()
        }
    }

    function login() {
        if (busy || !user || (password.text.length === 0 && !fingerprint))
            return
        busy = true
        usingFinger = password.text.length === 0
        messageIsError = false
        message = usingFinger ? qsTr("Touch the fingerprint reader.") : qsTr("Logging in…")
        sddm.login(user.name, password.text, sessionIndex)
    }
    Connections {
        target: sddm
        function onLoginFailed() {
            screen.busy = false
            screen.messageIsError = true
            screen.message = screen.usingFinger ? qsTr("Sorry, that fingerprint was not recognized. Please try again.")
                                                : qsTr("Sorry, that password is incorrect. Please try again.")
            password.text = ""
            password.forceActiveFocus()
        }
        function onLoginSucceeded() { screen.message = "" }
        // PAM's own words while it waits, such as fprintd's "Place your finger on …".
        function onInformationMessage(text) {
            if (screen.busy && text) { screen.messageIsError = false; screen.message = text }
        }
    }
    Component.onCompleted: password.forceActiveFocus()
}
