import QtQuick
import QtQuick.Controls
import QtQml.Models

// Sound device controls; the helper is queried only when this panel opens.
ShellDialog {
    id: panel
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    required property var service
    objectName: "sound-panel"
    title: "Sound"
    width: 540
    standardButtons: Dialog.Close
    readonly property var values: service.state
    // Alert Sounds comes first, as in Mac OS 9's Sound control panel; the panel opens on Output.
    property var alertSounds: null
    readonly property bool alerts: tabs.currentIndex === 0
    readonly property bool input: tabs.currentIndex === 2
    readonly property var devices: (input ? values.inputs : values.outputs) || []
    readonly property string defaultName: (input ? values.defaultInput : values.defaultOutput) || ""
    readonly property var current: devices.find(function(row) { return row.name === panel.defaultName }) || null
    readonly property var card: current ? ((values.cards || []).find(function(row) { return row.index === panel.current.card }) || null) : null
    readonly property var profile: card ? ((card.profiles || []).find(function(row) { return row.name === panel.card.activeProfile }) || null) : null
    function edit() {
        if (visible || sessionManager.shellModal) return
        open()
        service.refresh()
        if (alertSounds) alertSounds.refresh()
    }
    function choose(name) {
        if (input) service.chooseInput(name)
        else service.chooseOutput(name)
    }
    Timer { interval: 2000; repeat: true; running: panel.visible; onTriggered: panel.service.refresh() }
    onAboutToHide: { if (level.editTarget) level.commit(); levelDelay.stop(); profiles.close() }
    onInputChanged: if (level && level.editTarget) level.commit()
    onDefaultNameChanged: if (level && level.editTarget) level.commit()
    onCardChanged: if (!card && profiles) profiles.close()
    component SoundTab: TabButton {
        id: tab
        // Theme sounds directly: an inline component does not see the panel's id.
        readonly property var sounds: typeof manager !== "undefined" && manager ? manager.interfaceSounds : null
        onPressed: if (sounds) sounds.play("tabp")
        onReleased: if (sounds) sounds.play("tabr")
        implicitHeight: 30
        contentItem: Text {
            text: tab.text; font.pixelSize: 12; font.bold: tab.checked
            color: "#111"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: tab.down ? "#aaa" : tab.checked ? "#ccc" : "#ddd"
            border.color: "#333"
            Rectangle { x: 1; y: 1; width: parent.width-2; height: 1; color: "white" }
            Rectangle { anchors.fill: parent; anchors.margins: 3; color: "transparent"; border.color: "#555"; visible: tab.activeFocus }
        }
    }
    contentItem: Column {
        spacing: 10
        Row {
            spacing: 10
            StripGlyph { kind: "sound"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: "Choose where to play and record sound."
                font.pixelSize: 12; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter
            }
        }
        TabBar {
            id: tabs
            width: parent.width
            background: Rectangle { color: "transparent" }
            currentIndex: 1
            SoundTab { text: "Alert Sounds"; objectName: "sound-tab-alerts" }
            SoundTab { text: "Output"; objectName: "sound-tab-output" }
            SoundTab { text: "Input"; objectName: "sound-tab-input" }
        }
        // Alert Sounds: choosing one plays it. Personal sounds are the user's own files.
        Rectangle {
            visible: panel.alerts
            width: parent.width; height: 194; color: "white"; border.color: "#555"
            ListView {
                id: alertList
                objectName: "sound-alerts"
                anchors.fill: parent; anchors.margins: 1; clip: true
                model: panel.alertSounds ? panel.alertSounds.sounds : []
                ScrollBar.vertical: ScrollBar {}
                delegate: RadioButton {
                    id: alert
                    required property var modelData
                    objectName: "sound-alert-" + modelData.name
                    width: alertList.width; height: 26
                    checked: panel.alertSounds && modelData.name === panel.alertSounds.selected
                    onClicked: panel.alertSounds.choose(modelData.name)
                    indicator: Rectangle {
                        x: 8; y: (alert.height - height) / 2; width: 13; height: 13; radius: 7
                        color: "white"; border.color: "#333"
                        Rectangle { anchors.centerIn: parent; width: 7; height: 7; radius: 4; color: "#222"; visible: alert.checked }
                    }
                    background: Rectangle { color: alert.activeFocus ? "#e5e5ef" : "white" }
                    contentItem: Text {
                        leftPadding: 30; verticalAlignment: Text.AlignVCenter
                        text: alert.modelData.name; textFormat: Text.PlainText; font.pixelSize: 12; elide: Text.ElideRight
                    }
                }
            }
        }
        Column {
            visible: panel.alerts && Boolean(panel.alertSounds)
            width: parent.width; spacing: 4
            Text {
                text: "Alert volume   " + (panel.alertSounds ? panel.alertSounds.volume + "%" : "")
                      + (panel.alertSounds && panel.alertSounds.volume === 0 ? " (alerts flash the menu bar)" : "")
                font.pixelSize: 12; textFormat: Text.PlainText
            }
            Slider {
                id: alertVolume
                objectName: "sound-alert-volume"
                width: 300; height: 25; from: 0; to: 100; stepSize: 5
                value: panel.alertSounds ? panel.alertSounds.volume : 0
                onPressedChanged: if (pressed) panel.themeSound("sltp"); else { panel.alertSounds.setVolume(Math.round(value)); panel.alertSounds.play() }
                onMoved: if (!pressed) panel.alertSounds.setVolume(Math.round(value))
                background: Rectangle {
                    x: alertVolume.leftPadding; y: alertVolume.height/2-3; width: alertVolume.availableWidth; height: 6
                    color: "#aaa"; border.color: "#666"
                    Rectangle { x: 1; y: 1; width: parent.width-2; height: 1; color: "#777" }
                }
                handle: Rectangle {
                    x: alertVolume.leftPadding+alertVolume.visualPosition*(alertVolume.availableWidth-width)
                    y: alertVolume.height/2-height/2; width: 12; height: 22
                    color: alertVolume.pressed ? "#aaa" : "#ddd"; border.color: "#333"
                    Rectangle { x: 1; y: 1; width: parent.width-2; height: 1; color: "white" }
                    Rectangle { x: 1; y: 1; width: 1; height: parent.height-2; color: "white" }
                    Rectangle { x: 4; y: 5; width: 1; height: 11; color: "#888" }
                    Rectangle { x: 7; y: 5; width: 1; height: 11; color: "#888" }
                }
            }
            Text {
                objectName: "sound-alert-folder"
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: panel.alertSounds ? "To add your own alert sounds, put WAV files in " + panel.alertSounds.assetFolder
                                          + " and reopen this panel." : ""
            }
        }
        Rectangle {
            visible: !panel.alerts
            width: parent.width; height: 194; color: "white"; border.color: "#555"
            ListView {
                id: list
                objectName: "sound-devices"
                anchors.fill: parent; anchors.margins: 1; clip: true
                model: panel.devices
                ScrollBar.vertical: ScrollBar {}
                delegate: RadioButton {
                    id: row
                    required property var modelData
                    objectName: "sound-device-" + modelData.name
                    width: list.width; height: 48
                    checked: modelData.name === panel.defaultName
                    onClicked: panel.choose(modelData.name)
                    indicator: Rectangle {
                        x: 10; y: (row.height-height)/2; width: 13; height: 13; radius: 7
                        color: "white"; border.color: "#333"
                        Rectangle { anchors.centerIn: parent; width: 7; height: 7; radius: 4; color: "#222"; visible: row.checked }
                    }
                    background: Rectangle { color: row.activeFocus ? "#e5e5ef" : "white" }
                    contentItem: Column {
                        leftPadding: 34; rightPadding: 10; topPadding: 7; spacing: 3
                        Text {
                            width: parent.width-44; font.pixelSize: 12; elide: Text.ElideRight
                            text: row.modelData.description || row.modelData.name; textFormat: Text.PlainText
                        }
                        Text {
                            width: parent.width-44; font.pixelSize: 11; color: "#555"; elide: Text.ElideRight
                            text: (row.modelData.bluetooth ? "Bluetooth" : panel.input ? "Microphone" : "Audio output")
                                  + (row.checked ? " — Selected" : "")
                            textFormat: Text.PlainText
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width-40
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; font.pixelSize: 12
                    visible: list.count === 0; textFormat: Text.PlainText
                    text: panel.service.busy ? "Reading sound devices…" : panel.input ? "No microphones are available." : "No sound outputs are available."
                }
            }
        }
        Column {
            visible: !panel.alerts
            width: parent.width; spacing: 4
            Text {
                font.pixelSize: 12; textFormat: Text.PlainText
                text: (panel.input ? "Input level" : "Output volume")
                      + (panel.current && panel.current.volume >= 0 ? "   " + panel.current.volume + "%" : "")
            }
            Row {
                spacing: 12
                Slider {
                    id: level
                    objectName: "sound-volume"
                    from: 0; to: 100; stepSize: 1; width: 300; height: 28
                    value: editTarget && panel.current && panel.current.name === editTarget
                           && editKind === (panel.input ? "input" : "output") ? pendingValue
                           : panel.current && panel.current.volume >= 0 ? panel.current.volume : 0
                    enabled: Boolean(panel.current) && panel.current.volume >= 0
                    wheelEnabled: true
                    property string editTarget: ""
                    property string editKind: ""
                    property int pendingValue: 0
                    function remember() {
                        // Capture before enabling the binding to pendingValue. A read-only
                        // refresh must not pull a held slider toward the old device value.
                        const chosen = Math.round(value)
                        pendingValue = chosen
                        if (!editTarget && panel.current) { editTarget = panel.current.name; editKind = panel.input ? "input" : "output" }
                    }
                    function commit() {
                        levelDelay.stop()
                        if (editTarget) panel.service.setVolume(editKind, editTarget, pendingValue)
                        editTarget = ""
                        editKind = ""
                    }
                    onPressedChanged: {
                        if (pressed) { panel.themeSound("sltp"); remember() }
                        else if (enabled) { remember(); commit() }
                    }
                    onMoved: { remember(); if (!pressed) levelDelay.restart() }
                    background: Rectangle {
                        x: level.leftPadding; y: level.height/2-3; width: level.availableWidth; height: 6
                        color: "#aaa"; border.color: "#666"
                        Rectangle { x: 1; y: 1; width: parent.width-2; height: 1; color: "#777" }
                    }
                    handle: Rectangle {
                        x: level.leftPadding+level.visualPosition*(level.availableWidth-width)
                        y: level.height/2-height/2; width: 12; height: 22
                        color: level.pressed ? "#aaa" : "#ddd"; border.color: "#333"
                        Rectangle { x: 1; y: 1; width: parent.width-2; height: 1; color: "white" }
                        Rectangle { x: 1; y: 1; width: 1; height: parent.height-2; color: "white" }
                        Rectangle { x: 4; y: 5; width: 1; height: 11; color: "#888" }
                        Rectangle { x: 7; y: 5; width: 1; height: 11; color: "#888" }
                    }
                }
                PlatinumButton {
                    objectName: "sound-mute"
                    text: panel.current && panel.current.muted ? "Unmute" : "Mute"
                    enabled: Boolean(panel.current)
                    onClicked: panel.service.setMuted(panel.input ? "input" : "output", panel.current.name, !panel.current.muted)
                }
            }
        }
        Timer { id: levelDelay; interval: 250; onTriggered: level.commit() }
        Column {
            width: parent.width; spacing: 4; visible: !panel.alerts && Boolean(panel.card) && panel.card.profiles.length > 1
            Text { text: "Mode"; font.pixelSize: 12 }
            PlatinumButton {
                id: modeButton
                objectName: "sound-profile"
                width: parent.width
                text: (panel.profile ? panel.profile.description : "Choose a sound mode") + "  ▾"
                contentItem: Text {
                    text: modeButton.text; font.pixelSize: 12; textFormat: Text.PlainText
                    elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                }
                onClicked: profiles.open()
                Menu {
                    id: profiles
                    objectName: "sound-profile-menu"
                    y: modeButton.height; width: modeButton.width
                    background: Rectangle { color: "#eee"; border.color: "#333" }
                    Instantiator {
                        model: panel.card ? panel.card.profiles : []
                        delegate: MenuItem {
                            id: option
                            required property var modelData
                            objectName: "sound-profile-" + modelData.name
                            enabled: modelData.available
                            text: (panel.card && panel.card.activeProfile === modelData.name ? "✓ " : "   ") + modelData.description
                            contentItem: Text {
                                text: option.text; textFormat: Text.PlainText; font.pixelSize: 12
                                color: !option.enabled ? "#888" : option.highlighted ? "white" : "#111"; elide: Text.ElideRight
                            }
                            background: Rectangle { color: option.highlighted ? "#202050" : "transparent" }
                            onTriggered: if (panel.card) panel.service.chooseProfile(panel.card.name, modelData.name)
                        }
                        onObjectAdded: (index, object) => profiles.insertItem(index, object)
                        onObjectRemoved: (index, object) => profiles.removeItem(object)
                    }
                }
            }
        }
        Text {
            objectName: "sound-message"
            visible: !panel.alerts
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; textFormat: Text.PlainText
            color: panel.service.error ? "#8b1010" : "#444"
            text: panel.service.error || panel.service.warning || (panel.service.busy ? "Applying sound settings…"
                  : "Applications using the previous device follow your choice when possible.")
        }
        PlatinumButton { objectName: "sound-refresh"; text: "Refresh"; enabled: !panel.service.busy
                         onClicked: panel.alerts ? panel.alertSounds.refresh() : panel.service.refresh() }
    }
}
