import QtQuick

// Mac OS 9.2.2-style Log In dialog (Multiple Users), drawn as an original Platinum adaptation.
// One window per screen. The dialog shows where the keyboard is (the compositor chooses), so
// typing never goes to a field on another screen; until a screen has it, every screen shows it.
Rectangle {
    id: screen
    objectName: "lock-screen"
    color: "#666699"
    focus: true

    Image {
        anchors.fill: parent
        source: lock.desktopPicture
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize: Qt.size(screen.width, screen.height)
    }

    Rectangle {
        id: dialog
        objectName: "login-dialog"
        visible: screen.Window.active || !lock.keyboardOnLock
        width: 440; height: lock.loginMode ? 270 : 236
        anchors.centerIn: parent
        color: "#dddddd"
        border.color: "#111"
        Rectangle {x: 4; y: 4; z: -1; width: parent.width; height: parent.height; color: "#60000000"}
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}

        // Title bar: Platinum's horizontal ridges either side of the centered title.
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

        // The user's picture, or a plain figure, in an inset well.
        Rectangle {
            id: well
            x: 22; y: 44; width: 72; height: 72
            color: "#eeeeee"; border.color: "#777"
            Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "white"}
            Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "white"}
            Image {
                id: face
                anchors.fill: parent; anchors.margins: 3
                source: lock.picture
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
            Text {
                objectName: "login-name"
                width: parent.width; elide: Text.ElideRight
                text: qsTr("Welcome, %1").arg(lock.fullName); textFormat: Text.PlainText
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
                    readOnly: !lock.ready || lock.busy
                    focus: true
                    clip: true
                    onAccepted: login()
                    Keys.onEscapePressed: clear()
                }
            }
            Text {
                objectName: "login-message"
                width: parent.width; height: 32
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                text: lock.message; textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 11
                color: lock.messageIsError ? "#990000" : "#333"
            }
        }

        // Log In only: which session starts, as Mac OS 9's Log In offered its user's settings.
        Row {
            objectName: "login-session-row"
            visible: lock.loginMode
            x: 114; y: 192; spacing: 8; z: 2
            Text {
                text: qsTr("Session:"); textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                id: sessionBox
                objectName: "login-session"
                readonly property var names: ({platinum: qsTr("Platinum Reality"), kde: qsTr("KDE Plasma")})
                width: 170; height: 22; color: "white"; border.color: "#222"
                Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "#ccc"}
                Text {
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    text: sessionBox.names[lock.session]; textFormat: Text.PlainText
                    font.family: "Charcoal"; font.pixelSize: 12; color: "#111"
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                    text: "\u25BE"; font.pixelSize: 12; color: "#111"
                }
                MouseArea {anchors.fill: parent; onClicked: sessionList.visible = !sessionList.visible}
                Rectangle {
                    id: sessionList
                    objectName: "login-session-list"
                    visible: false
                    y: parent.height; width: parent.width; height: 48
                    color: "#eeeeee"; border.color: "#222"
                    Rectangle {x: 3; y: 3; z: -1; width: parent.width; height: parent.height; color: "#60000000"}
                    Column {
                        x: 1; y: 1; width: parent.width - 2
                        Repeater {
                            model: ["platinum", "kde"]
                            Rectangle {
                                required property string modelData
                                objectName: "login-session-" + modelData
                                width: parent.width; height: 23
                                color: choice.containsMouse ? "#333366" : "transparent"
                                Text {
                                    x: 8; anchors.verticalCenter: parent.verticalCenter
                                    text: (lock.session === modelData ? "\u2713 " : "   ") + sessionBox.names[modelData]
                                    textFormat: Text.PlainText
                                    font.family: "Charcoal"; font.pixelSize: 12
                                    color: choice.containsMouse ? "white" : "#111"
                                }
                                MouseArea {
                                    id: choice
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: {lock.session = modelData; sessionList.visible = false; password.forceActiveFocus()}
                                }
                            }
                        }
                    }
                }
            }
        }
        Row {
            objectName: "login-fingerprint-hint"
            visible: lock.fingerprintAvailable
            x: 22; y: parent.height - 38; spacing: 6
            // A small ridged fingertip.
            Item {
                width: 14; height: 16; anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        x: 1 + index * 2.5; y: 1 + index * 2.5
                        width: 12 - index * 5; height: 14 - index * 5
                        radius: width / 2; color: "transparent"; border.color: "#444"
                    }
                }
            }
            Text {
                text: qsTr("Or touch the fingerprint reader."); textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 11; color: "#333"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Log In: the default button, with Platinum's heavy ring.
        Rectangle {
            id: loginButton
            objectName: "login-button"
            property bool down: buttonArea.pressed && buttonArea.containsMouse
            readonly property bool usable: lock.ready && !lock.busy && password.text.length > 0
            width: 86; height: 24
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.bottom: parent.bottom; anchors.bottomMargin: 18
            color: down ? "#aaa" : "#ddd"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: loginButton.down ? "#666" : "white"}
            Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: loginButton.down ? "#666" : "white"}
            Rectangle {x: 2; y: parent.height - 2; width: parent.width - 3; height: 1; color: loginButton.down ? "white" : "#777"}
            Rectangle {x: parent.width - 2; y: 2; width: 1; height: parent.height - 3; color: loginButton.down ? "white" : "#777"}
            Rectangle {anchors.fill: parent; anchors.margins: -4; radius: 4; color: "transparent"; border.width: 3; border.color: loginButton.usable ? "#222" : "#888"}
            Text {
                anchors.centerIn: parent
                text: qsTr("Log In"); textFormat: Text.PlainText
                font.family: "Charcoal"; font.pixelSize: 12; color: loginButton.usable ? "#111" : "#888"
            }
            MouseArea {id: buttonArea; anchors.fill: parent; onClicked: login()}
        }
    }

    function login() {
        if (lock.submit(password.text))
            password.text = ""
    }
    Connections {
        target: lock
        function onRejected() { password.forceActiveFocus() }
    }
    // Typing anywhere goes to the password field.
    Component.onCompleted: password.forceActiveFocus()
}
