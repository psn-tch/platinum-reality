import QtQuick
import QtQuick.Controls

// The Mac OS 9 Appearance control panel is tabbed (docs/historical-fidelity-review.md F10,
// V922 E17/E18): Appearance · Fonts · Desktop · Sound · Options. There is only one theme here,
// so the Themes tab is left out rather than shown with nothing to choose. Changes still apply
// with OK, as before; the desktop picture applies from its own chooser.
ShellDialog {
    id: panel
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    required property var settings
    objectName: "appearance-dialog"
    title: "Appearance"
    width: 500
    standardButtons: Dialog.Ok | Dialog.Cancel
    property int draftDuration: 240
    property bool draftStripVisible: true
    property string draftAccent: "blue"
    property bool draftDoubleClick: true
    property bool draftKeepAwake: false
    property bool draftSoundTrack: false
    readonly property var interfaceSounds: sessionManager ? sessionManager.interfaceSounds : null
    property bool previewCollapsed: false
    property int tab: 0
    property bool placeAfterClose: false
    readonly property var tabs: ["Appearance", "Fonts", "Desktop", "Sound", "Options"]
    // Place Picture… closes the panel (keeping its changes) and asks for the picture chooser.
    signal placePictureRequested()
    function edit(page) {
        if (visible || sessionManager.shellModal) return
        draftDuration = settings.shadeDuration
        draftStripVisible = settings.controlStripVisible
        draftAccent = settings.accent
        draftDoubleClick = settings.doubleClickCollapse
        draftKeepAwake = settings.keepScreenAwake
        if (interfaceSounds) interfaceSounds.refresh()
        draftSoundTrack = !!interfaceSounds && interfaceSounds.available && interfaceSounds.enabled
        previewCollapsed = false
        placeAfterClose = false
        tab = Math.max(0, tabs.indexOf(page === undefined ? "Appearance" : page))
        open()
    }
    onAccepted: {
        settings.apply(draftDuration, draftStripVisible)
        settings.applyOptions(draftAccent, draftDoubleClick)
        settings.setKeepScreenAwake(draftKeepAwake)
        if (interfaceSounds && interfaceSounds.available) interfaceSounds.enabled = draftSoundTrack
    }
    onClosed: if (placeAfterClose) {placeAfterClose = false; placePictureRequested()}
    contentItem: Column {
        spacing: 0
        Row {
            x: 4; height: 24; spacing: -4
            Repeater {
                model: panel.tabs
                delegate: Item {
                    id: tabItem
                    required property string modelData
                    required property int index
                    readonly property bool selected: panel.tab === index
                    objectName: "appearance-tab-" + modelData.toLowerCase()
                    width: tabLabel.implicitWidth + 28; height: 24
                    z: selected ? 2 : 1
                    Canvas {
                        id: tabShape
                        anchors.fill: parent
                        onPaint: {
                            let c = getContext("2d")
                            c.reset()
                            c.fillStyle = tabItem.selected ? "#eee" : "#ccc"; c.strokeStyle = "#222"; c.lineWidth = 1
                            c.beginPath()
                            c.moveTo(0.5, height); c.lineTo(4.5, 2.5); c.lineTo(width - 10.5, 2.5)
                            c.lineTo(width - 0.5, height)
                            c.fill(); c.stroke()
                            if (!tabItem.selected) {c.beginPath(); c.moveTo(0, height - 0.5); c.lineTo(width, height - 0.5); c.stroke()}
                        }
                        Connections {target: panel; function onTabChanged() {tabShape.requestPaint()}}
                        // Created while the dialog is hidden, and sized only after layout.
                        onAvailableChanged: if (available) requestPaint()
                        onVisibleChanged: if (visible) requestPaint()
                        onWidthChanged: requestPaint()
                    }
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent; anchors.horizontalCenterOffset: -3; anchors.verticalCenterOffset: 1
                        text: tabItem.modelData; font.pixelSize: 12; color: "#111"
                    }
                    MouseArea {anchors.fill: parent; onClicked: panel.tab = tabItem.index
                               onPressed: panel.themeSound("tabp"); onReleased: panel.themeSound("tabr")}
                }
            }
        }
        Rectangle {
            id: page
            objectName: "appearance-page"
            width: parent.width; height: 330
            color: "#eee"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}

            // Appearance: the highlight variation (blue by default; graphite seen in V922 E11).
            Column {
                visible: panel.tab === 0
                x: 20; y: 18; width: parent.width - 40; spacing: 12
                Text {text: "Appearance: Platinum"; font.pixelSize: 12; font.bold: true}
                Text {text: "Highlight color:"; font.pixelSize: 12}
                Row {
                    spacing: 10
                    Repeater {
                        model: [{name: "Blue", value: "blue", color: "#2c2994"},
                                {name: "Graphite", value: "graphite", color: "#656565"}]
                        delegate: PlatinumButton {
                            required property var modelData
                            objectName: "appearance-accent-" + modelData.value
                            text: modelData.name; implicitWidth: 110
                            defaultAction: panel.draftAccent === modelData.value
                            onClicked: panel.draftAccent = modelData.value
                            Rectangle {x: 8; anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12
                                       color: parent.modelData.color; border.color: "#222"}
                        }
                    }
                }
                Rectangle {
                    objectName: "appearance-accent-sample"
                    width: 200; height: 58; color: "#eee"; border.color: "#222"
                    Column {
                        x: 3; y: 3; width: parent.width - 6
                        Rectangle {width: parent.width; height: 25; color: "transparent"
                            Text {x: 12; anchors.verticalCenter: parent.verticalCenter; text: "Menu Item"; font.pixelSize: 12}}
                        Rectangle {width: parent.width; height: 25; color: panel.draftAccent === "graphite" ? "#656565" : "#2c2994"
                            Text {x: 12; anchors.verticalCenter: parent.verticalCenter; text: "Selected Item"; font.pixelSize: 12; color: "white"}}
                    }
                }
            }

            // Fonts: the three Mac OS 8.5–9 roles (F11); all follow the system font for now.
            Column {
                visible: panel.tab === 1
                x: 20; y: 18; width: parent.width - 40; spacing: 14
                Repeater {
                    model: [{role: "Large System Font", bold: true}, {role: "Small System Font", bold: false},
                            {role: "Views Font", bold: false}]
                    delegate: Row {
                        required property var modelData
                        spacing: 10
                        Text {width: 140; text: modelData.role + ":"; font.pixelSize: 12; horizontalAlignment: Text.AlignRight}
                        Text {objectName: "appearance-font-" + modelData.role.split(" ")[0].toLowerCase()
                              text: panel.settings.fontFamily; font.pixelSize: 12; font.bold: modelData.bold; font.family: panel.settings.fontFamily}
                    }
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
                    text: "Each role uses the system font for now. Separate choices are planned."
                }
            }

            // Desktop: the current picture, with Place Picture… as in Mac OS 9.
            Row {
                visible: panel.tab === 2
                x: 20; y: 18; spacing: 16
                Rectangle {
                    width: 240; height: 170; color: "#708b97"; border.color: "#222"; clip: true
                    Image {
                        objectName: "appearance-desktop-preview"
                        anchors.fill: parent; anchors.margins: 1
                        source: panel.settings.wallpaper; asynchronous: true; sourceSize: Qt.size(480, 340)
                        fillMode: panel.settings.wallpaperLayout === "fit" ? Image.PreserveAspectFit
                                  : panel.settings.wallpaperLayout === "center" ? Image.Pad
                                  : panel.settings.wallpaperLayout === "tile" ? Image.Tile : Image.PreserveAspectCrop
                    }
                }
                Column {
                    spacing: 10; width: 180
                    Text {
                        width: parent.width; wrapMode: Text.WrapAnywhere; font.pixelSize: 12
                        text: panel.settings.wallpaperPath === "" ? "No picture: a solid desktop color."
                              : panel.settings.wallpaperPath.split("/").pop()
                    }
                    PlatinumButton {
                        objectName: "appearance-place-picture"; text: "Place Picture…"; implicitWidth: 150
                        onClicked: {panel.placeAfterClose = true; panel.accept()}
                    }
                    PlatinumButton {
                        objectName: "appearance-remove-picture"; text: "Remove Picture"; implicitWidth: 150
                        enabled: panel.settings.wallpaperPath !== ""
                        onClicked: panel.settings.applyDesktopPicture("", panel.settings.wallpaperLayout)
                    }
                }
            }

            // Sound: the track is the user's own "Platinum Sounds", imported from their Mac OS 9
            // installation (scripts/import-macos9-assets.py); nothing is bundled (F17).
            Column {
                id: soundTab
                visible: panel.tab === 3
                x: 20; y: 18; width: parent.width - 40; spacing: 12
                readonly property bool available: !!panel.interfaceSounds && panel.interfaceSounds.available
                Row {
                    spacing: 8
                    Text {text: "Sound track:"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter}
                    Rectangle {
                        objectName: "appearance-sound-track"
                        width: 180; height: 22; color: soundTab.available ? "white" : "#ddd"
                        border.color: soundTab.available ? "#222" : "#888"
                        Text {x: 8; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                              text: panel.draftSoundTrack ? "Platinum Sounds" : "None"
                              color: soundTab.available ? "black" : "#888"}
                        Text {anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                              text: "\u25BE"; font.pixelSize: 12; visible: soundTab.available}
                        MouseArea {anchors.fill: parent; enabled: soundTab.available; onClicked: trackMenu.popup(0, parent.height)}
                        Menu {
                            id: trackMenu
                            MenuItem {objectName: "sound-track-platinum"; text: "Platinum Sounds"; checkable: true
                                      checked: panel.draftSoundTrack; onTriggered: panel.draftSoundTrack = true}
                            MenuItem {objectName: "sound-track-none"; text: "None"; checkable: true
                                      checked: !panel.draftSoundTrack; onTriggered: panel.draftSoundTrack = false}
                        }
                    }
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
                    text: soundTab.available
                          ? "Menus, windows, WindowShade and the Trash play the sounds from your own Mac OS 9 installation, at the alert volume."
                          : "No sound track is installed. Your own Mac OS 9 \"Platinum Sounds\" can be imported with scripts/import-macos9-assets.py."
                }
            }

            // Options: double-click collapse (V922 E17), the Control Strip, and WindowShade speed.
            Column {
                visible: panel.tab === 4
                x: 20; y: 14; width: parent.width - 40; spacing: 10
                PlatinumCheckBox {
                    objectName: "option-double-click-collapse"; text: "Double-click title bar to collapse windows"
                    checked: panel.draftDoubleClick; onToggled: panel.draftDoubleClick = checked
                }
                PlatinumCheckBox {
                    objectName: "show-control-strip"; text: "Show Control Strip"
                    checked: panel.draftStripVisible; onToggled: panel.draftStripVisible = checked
                }
                PlatinumCheckBox {
                    objectName: "keep-screen-awake"; text: "Never turn the screen off automatically"
                    checked: panel.draftKeepAwake; onToggled: panel.draftKeepAwake = checked
                }
                Rectangle {width: parent.width; height: 1; color: "#999"}
                Row {
                    width: parent.width; spacing: 10
                    Text {text: "WindowShade speed:"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter}
                    Slider {
                        onPressedChanged: if (pressed) panel.themeSound("sltp")
                        id: speed; objectName: "shade-speed"; width: 170; height: 26
                        from: 0; to: 1200; stepSize: 10; value: panel.draftDuration
                        onMoved: panel.draftDuration = Math.round(value)
                        background: Rectangle {x: speed.leftPadding; y: (speed.height - height) / 2; width: speed.availableWidth
                                               height: 5; color: "white"; border.color: "#777"}
                        handle: Rectangle {
                            x: speed.leftPadding + speed.visualPosition * (speed.availableWidth - width); y: (speed.height - height) / 2
                            implicitWidth: 13; implicitHeight: 21; color: speed.pressed ? "#bbb" : "#ddd"; border.color: "#333"
                            Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
                            Rectangle {x: 4; y: 6; width: 1; height: 9; color: "#777"}
                            Rectangle {x: 7; y: 6; width: 1; height: 9; color: "#777"}
                        }
                    }
                    Text {
                        text: panel.draftDuration === 0 ? "Instant" : (panel.draftDuration / 1000).toFixed(2) + " s"
                        font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 8
                    PlatinumButton {text: "Fast"; implicitWidth: 64; onClicked: panel.draftDuration = 120}
                    PlatinumButton {text: "Normal"; implicitWidth: 70; onClicked: panel.draftDuration = 240}
                    PlatinumButton {text: "Slow"; implicitWidth: 64; onClicked: panel.draftDuration = 600}
                    PlatinumButton {objectName: "shade-preview-button"; text: "Try It"; implicitWidth: 64
                                    onClicked: panel.previewCollapsed = !panel.previewCollapsed}
                }
                Item {
                    width: parent.width; height: 104
                    WindowRoll {
                        objectName: "shade-preview"; width: parent.width - 12; x: 6
                        contentHeight: 72; collapsed: panel.previewCollapsed; animationDuration: panel.draftDuration
                        Rectangle {anchors.fill: parent; color: "#ccc"; border.color: "#333"}
                        Rectangle {x: 2; y: 2; width: parent.width - 4; height: 19; color: "#bbb"
                            Text {anchors.centerIn: parent; text: "WindowShade Preview"; font.pixelSize: 12}}
                        Rectangle {x: 3; y: 23; width: parent.width - 6; height: 72; color: "white"
                            Text {anchors.centerIn: parent; text: "A window rolls into its title bar."; font.pixelSize: 12}}
                    }
                }
            }
        }
    }
}
