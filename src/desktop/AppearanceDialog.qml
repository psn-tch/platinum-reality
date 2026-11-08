import QtQuick
import QtQuick.Controls

// The Mac OS 9 Appearance control panel is tabbed (docs/historical-fidelity-review.md F10,
// V922 E17/E18): Appearance · Fonts · Desktop · Sound · Options. There is only one theme here,
// so the Themes tab is left out rather than shown with nothing to choose. It is a panel of the
// Control Panels window, and every change applies at once, as in Mac OS 9.
Item {
    id: panel
    // The Appearance sound track (InterfaceSounds); silent without the user's own import.
    function themeSound(tag) { if (typeof manager !== "undefined" && manager && manager.interfaceSounds) manager.interfaceSounds.play(tag) }
    required property var settings
    property var sessionManager: null
    objectName: "appearance-dialog"
    readonly property string panelId: "appearance"
    readonly property string title: "Appearance"
    readonly property string glyph: "appearance"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 500
    implicitHeight: body.implicitHeight
    readonly property var interfaceSounds: sessionManager ? sessionManager.interfaceSounds : null
    readonly property bool soundTrackOn: !!interfaceSounds && interfaceSounds.available && interfaceSounds.enabled
    property bool previewCollapsed: false
    property int tab: 0
    readonly property var tabs: ["Appearance", "Fonts", "Desktop", "Sound", "Options"]
    // Place Picture… shows the Desktop Picture panel.
    signal placePictureRequested()
    function edit(page) {
        if (interfaceSounds) interfaceSounds.refresh()
        previewCollapsed = false
        tab = Math.max(0, tabs.indexOf(page === undefined ? "Appearance" : page))
        if (host) host.show(panelId)
    }
    function setShadeDuration(ms) { settings.apply(ms, settings.controlStripVisible) }
    function setStripVisible(on) { settings.apply(settings.shadeDuration, on) }
    function setAccent(name) { settings.applyOptions(name, settings.doubleClickCollapse) }
    function setDoubleClick(on) { settings.applyOptions(settings.accent, on) }
    function setSoundTrack(on) { if (interfaceSounds && interfaceSounds.available) interfaceSounds.enabled = on }
    Column {
        id: body
        width: parent.width
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

            // Appearance, laid out as 9.2.2's tab (reference VM, 2026-11-04): Appearance, Highlight
            // Color (selected text) and Variation (menus and controls: blue, or graphite as V922 E11).
            Grid {
                visible: panel.tab === 0
                x: 20; y: 22; columns: 2; columnSpacing: 8; rowSpacing: 4
                Text { width: 120; height: 20; text: "Appearance:"; font.pixelSize: 12; font.bold: true
                       horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                PlatinumPopup {
                    objectName: "appearance-theme"; implicitWidth: 200; enabled: false
                    choices: [{name: "Platinum", value: "platinum"}]; current: "platinum"
                }
                Item { width: 1; height: 1 }
                Text { text: "for the overall look of menus, icons, windows and controls"; font.pixelSize: 11; color: "#333"; bottomPadding: 10 }
                Text { width: 120; height: 20; text: "Highlight Color:"; font.pixelSize: 12; font.bold: true
                       horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                PlatinumPopup {
                    objectName: "appearance-highlight"; implicitWidth: 200
                    choices: panel.settings.highlightChoices(); current: panel.settings.highlight
                    onChosen: function(value) { panel.settings.setHighlight(value) }
                }
                Item { width: 1; height: 1 }
                Text { text: "for selected text"; font.pixelSize: 11; color: "#333"; bottomPadding: 10 }
                Text { width: 120; height: 20; text: "Variation:"; font.pixelSize: 12; font.bold: true
                       horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter }
                PlatinumPopup {
                    objectName: "appearance-variation"; implicitWidth: 200
                    choices: [{name: "Blue", value: "blue", color: "#6666cc"}, {name: "Graphite", value: "graphite", color: "#888888"}]
                    current: panel.settings.accent
                    onChosen: function(value) { panel.setAccent(value) }
                }
                Item { width: 1; height: 1 }
                Text { text: "for menus and controls"; font.pixelSize: 11; color: "#333"; bottomPadding: 14 }
                Item { width: 1; height: 1 }
                // How both look together: a menu with its selected item, and selected text.
                Row {
                    spacing: 14
                    Rectangle {
                        objectName: "appearance-accent-sample"
                        width: 160; height: 58; color: "#eee"; border.color: "#222"
                        Column {
                            x: 3; y: 3; width: parent.width - 6
                            Rectangle {width: parent.width; height: 25; color: "transparent"
                                Text {x: 12; anchors.verticalCenter: parent.verticalCenter; text: "Menu Item"; font.pixelSize: 12}}
                            Rectangle {width: parent.width; height: 25; color: panel.settings.accentColor
                                Text {x: 12; anchors.verticalCenter: parent.verticalCenter; text: "Selected Item"; font.pixelSize: 12; color: "white"}}
                        }
                    }
                    Rectangle {
                        width: 150; height: 26; color: "white"; border.color: "#222"; anchors.verticalCenter: parent.verticalCenter
                        Row {
                            x: 6; anchors.verticalCenter: parent.verticalCenter
                            Text { text: "Some "; font.pixelSize: 12 }
                            Rectangle {
                                objectName: "appearance-highlight-sample"
                                width: selectedWord.implicitWidth + 2; height: 16; color: panel.settings.highlightColor
                                Text { id: selectedWord; x: 1; anchors.verticalCenter: parent.verticalCenter; text: "selected"
                                       font.pixelSize: 12; color: panel.settings.highlightTextColor }
                            }
                            Text { text: " text"; font.pixelSize: 12 }
                        }
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
                        onClicked: panel.placePictureRequested()
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
                              text: panel.soundTrackOn ? "Platinum Sounds" : "None"
                              color: soundTab.available ? "black" : "#888"}
                        Text {anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                              text: "\u25BE"; font.pixelSize: 12; visible: soundTab.available}
                        MouseArea {anchors.fill: parent; enabled: soundTab.available; onClicked: trackMenu.popup(0, parent.height)}
                        Menu {
                            id: trackMenu
                            MenuItem {objectName: "sound-track-platinum"; text: "Platinum Sounds"; checkable: true
                                      checked: panel.soundTrackOn; onTriggered: panel.setSoundTrack(true)}
                            MenuItem {objectName: "sound-track-none"; text: "None"; checkable: true
                                      checked: !panel.soundTrackOn; onTriggered: panel.setSoundTrack(false)}
                        }
                    }
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
                    text: soundTab.available
                          ? "Menus, windows, WindowShade and the Trash play the sounds from your own Mac OS 9 installation, at the alert volume."
                          : "No sound track is installed. Your own Mac OS 9 \"Platinum Sounds\" can be imported with Apple menu ▸ Asset Importer."
                }
            }

            // Options: double-click collapse (V922 E17), the Control Strip, and WindowShade speed.
            Column {
                visible: panel.tab === 4
                x: 20; y: 14; width: parent.width - 40; spacing: 10
                PlatinumCheckBox {
                    objectName: "option-double-click-collapse"; text: "Double-click title bar to collapse windows"
                    checked: panel.settings.doubleClickCollapse; onToggled: panel.setDoubleClick(checked)
                }
                PlatinumCheckBox {
                    objectName: "show-control-strip"; text: "Show Control Strip"
                    checked: panel.settings.controlStripVisible; onToggled: panel.setStripVisible(checked)
                }
                PlatinumCheckBox {
                    objectName: "keep-screen-awake"; text: "Never turn the screen off automatically"
                    checked: panel.settings.keepScreenAwake; onToggled: panel.settings.setKeepScreenAwake(checked)
                }
                Rectangle {width: parent.width; height: 1; color: "#999"}
                Row {
                    width: parent.width; spacing: 10
                    Text {text: "WindowShade speed:"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter}
                    Slider {
                        // Saved when the slider is let go, not on every step of a drag.
                        onPressedChanged: if (pressed) panel.themeSound("sltp"); else panel.setShadeDuration(Math.round(value))
                        id: speed; objectName: "shade-speed"; width: 170; height: 26
                        from: 0; to: 1200; stepSize: 10; value: panel.settings.shadeDuration
                        onMoved: if (!pressed) panel.setShadeDuration(Math.round(value))
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
                        text: Math.round(speed.value) === 0 ? "Instant" : (speed.value / 1000).toFixed(2) + " s"
                        font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 8
                    PlatinumButton {text: "Fast"; implicitWidth: 64; onClicked: panel.setShadeDuration(120)}
                    PlatinumButton {text: "Normal"; implicitWidth: 70; onClicked: panel.setShadeDuration(240)}
                    PlatinumButton {text: "Slow"; implicitWidth: 64; onClicked: panel.setShadeDuration(600)}
                    PlatinumButton {objectName: "shade-preview-button"; text: "Try It"; implicitWidth: 64
                                    onClicked: panel.previewCollapsed = !panel.previewCollapsed}
                }
                Item {
                    width: parent.width; height: 104
                    WindowRoll {
                        objectName: "shade-preview"; width: parent.width - 12; x: 6
                        contentHeight: 72; collapsed: panel.previewCollapsed; animationDuration: Math.round(speed.value)
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
