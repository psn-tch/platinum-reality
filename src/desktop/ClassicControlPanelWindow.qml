import QtQuick
import QtQuick.Controls

// Control Panel, the desk accessory after the System 6 Control Panel (Lily Hewitt's reference,
// 2026-11-05): one black-and-white window to keep open, a scrolling list of icons on the left and
// the chosen one's settings on the right. General has the desktop pattern, insertion point and
// menu blinking, the time and date, the speaker volume and the menu bar clock; Keyboard, Mouse and
// Sound have their main settings; the others open their Control Panels panel. Original drawings.
DesktopWindow {
    id: window
    objectName: "classic-control-panel"
    title: "Control Panel"
    classicFrame: true
    closeOnEscape: false
    width: 640; height: 520
    required property var appearance
    required property var input
    // The Quick Controls service: state.volume {available, value 0-100, muted} and set().
    property var controls: null
    signal openPanel(string action)
    property int chosen: 0
    readonly property var entries: [
        {name: "General", glyph: "general"}, {name: "Keyboard", glyph: "keyboard"},
        {name: "Mouse", glyph: "mouse"}, {name: "Sound", glyph: "sound"},
        {name: "Monitors", glyph: "monitors", action: "monitors"},
        {name: "Network", glyph: "wifi", action: "network"},
        {name: "Date & Time", glyph: "clock", action: "date-time"},
        {name: "Appearance", glyph: "appearance", action: "appearance"},
        {name: "Desktop Picture", glyph: "picture", action: "desktop-picture"},
        {name: "Energy Saver", glyph: "energy", action: "energy"},
        {name: "Bluetooth", glyph: "bluetooth", action: "bluetooth"}]
    readonly property var entry: entries[chosen]
    onVisibleChanged: if (visible && controls) controls.refresh()

    // The black-and-white controls this window is drawn with.
    component ClassicRadio: Item {
        id: radio
        property string text: ""
        property bool checked: false
        property bool labelBelow: false
        signal clicked()
        implicitWidth: labelBelow ? Math.max(20, label.implicitWidth) : 20 + label.implicitWidth
        implicitHeight: labelBelow ? 20 + label.implicitHeight : 20
        Rectangle {
            x: radio.labelBelow ? (radio.width - 18) / 2 : 0; y: 1; width: 18; height: 18; radius: 9
            color: hit.pressed ? "black" : "white"; border.color: "black"; border.width: 2
            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: hit.pressed ? "white" : "black"; visible: radio.checked }
        }
        Text {
            id: label
            x: radio.labelBelow ? (radio.width - implicitWidth) / 2 : 24; y: radio.labelBelow ? 20 : 1
            text: radio.text; textFormat: Text.PlainText; font.pixelSize: 14; color: "black"
        }
        MouseArea { id: hit; anchors.fill: parent; onClicked: radio.clicked() }
    }
    component ClassicButton: Rectangle {
        id: button
        property string text: ""
        signal clicked()
        width: buttonLabel.implicitWidth + 28; height: 26; radius: 8
        color: buttonMouse.pressed ? "black" : "white"; border.color: "black"; border.width: 1
        Text { id: buttonLabel; anchors.centerIn: parent; text: button.text; textFormat: Text.PlainText; font.pixelSize: 14; color: buttonMouse.pressed ? "white" : "black" }
        MouseArea { id: buttonMouse; anchors.fill: parent; onClicked: button.clicked() }
    }
    component Caption: Text {
        textFormat: Text.PlainText; font.pixelSize: 14; color: "black"
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
    }
    // A 50% grey of dots, as System 6 fills scroll bar tracks.
    component Dots: Canvas {
        onPaint: {
            const c = getContext("2d"); c.fillStyle = "white"; c.fillRect(0, 0, width, height); c.fillStyle = "black"
            for (let y = 0; y < height; ++y) for (let x = y % 2; x < width; x += 2) c.fillRect(x, y, 1, 1)
        }
        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
    }

    Item {
        id: body
        x: -13; y: -10; width: window.width - 2; height: window.height - 23
        // The icons, in a scrolling column with the system's name at the foot.
        Item {
            id: column
            width: 150; height: parent.height
            ListView {
                id: icons
                objectName: "classic-control-panel-list"
                width: parent.width - 17; height: parent.height - 24; clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: window.entries
                ScrollBar.vertical: ScrollBar {
                    id: iconsBar
                    parent: column
                    x: column.width - 17; y: 0; width: 17; height: icons.height
                    policy: ScrollBar.AlwaysOn
                    background: Rectangle {
                        border.color: "black"; color: "white"
                        Dots { x: 1; y: 1; width: parent.width - 2; height: parent.height - 2; visible: iconsBar.size < 1 }
                    }
                    contentItem: Rectangle { implicitWidth: 15; implicitHeight: 20; color: "white"; border.color: "black"; visible: iconsBar.size < 1 }
                }
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: "classic-control-panel-" + modelData.glyph
                    width: icons.width; height: 80
                    StripGlyph { anchors.horizontalCenter: parent.horizontalCenter; y: 10; width: 36; height: 36; kind: modelData.glyph }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 52
                        width: name.implicitWidth + 6; height: name.implicitHeight + 2
                        color: index === window.chosen ? "black" : "white"
                        Text { id: name; anchors.centerIn: parent; text: modelData.name; textFormat: Text.PlainText; font.pixelSize: 13; color: index === window.chosen ? "white" : "black" }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.chosen = index
                        onDoubleClicked: if (modelData.action) window.openPanel(modelData.action)
                    }
                }
            }
            Rectangle { y: parent.height - 24; width: parent.width; height: 1; color: "black" }
            Text { x: 4; y: parent.height - 20; text: "Platinum Reality"; textFormat: Text.PlainText; font.pixelSize: 12; color: "black" }
        }
        Rectangle { x: column.width; width: 1; height: parent.height; color: "black" }

        Item {
            id: pane
            x: column.width + 1; width: parent.width - x; height: parent.height

            // General
            Item {
                id: general
                objectName: "classic-general"
                anchors.fill: parent
                visible: window.entry.glyph === "general"
                readonly property real patternWidth: 320
                readonly property real blinkWidth: 150
                readonly property real volumeX: width - 150
                Rectangle { y: 150; width: parent.width; height: 1; color: "black" }
                Rectangle { x: general.patternWidth; width: 1; height: 150; color: "black" }
                Rectangle { x: general.blinkWidth; y: 150; width: 1; height: parent.height - 150 - 110; color: "black" }
                Rectangle { x: general.volumeX; y: 150; width: 1; height: parent.height - 150; color: "black" }
                Rectangle { y: parent.height - 110; width: general.volumeX; height: 1; color: "black" }

                // Desktop Pattern: dots to edit at the left, the pattern at the right with arrows
                // through the patterns; a click on the pattern puts it on the desktop.
                Item {
                    id: patternBox
                    width: general.patternWidth; height: 150
                    readonly property var presets: ["aa55aa55aa55aa55", "8822882288228822", "77dd77dd77dd77dd",
                        "0102040810204080", "ff808080ff080808", "8040201008040201", "ff818181818181ff",
                        "1038fe381000ff00", "0000000000000000", "ffffffffffffffff"]
                    property string pattern: window.appearance.desktopPattern || presets[0]
                    property int preset: Math.max(0, presets.indexOf(pattern))
                    function bit(x, y) { return (parseInt(pattern.substr(2 * y, 2), 16) >> (7 - x)) & 1 }
                    function setBit(x, y, on) {
                        let row = parseInt(pattern.substr(2 * y, 2), 16)
                        row = on ? row | (1 << (7 - x)) : row & ~(1 << (7 - x))
                        pattern = pattern.substr(0, 2 * y) + ("0" + row.toString(16)).slice(-2) + pattern.substr(2 * y + 2)
                    }
                    Connections { target: window.appearance; function onChanged() { if (window.appearance.desktopPattern) patternBox.pattern = window.appearance.desktopPattern } }
                    Rectangle {
                        id: editor
                        objectName: "classic-pattern-editor"
                        x: 34; y: 18; width: 8 * 11 + 2; height: 8 * 11 + 2; color: "white"; border.color: "black"
                        Repeater {
                            model: 64
                            Rectangle {
                                required property int index
                                x: 1 + (index % 8) * 11; y: 1 + Math.floor(index / 8) * 11; width: 11; height: 11
                                color: patternBox.bit(index % 8, Math.floor(index / 8)) ? "black" : "white"
                                Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#888"; border.width: 1; opacity: 0.25 }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            property bool paint: true
                            function at(mouse) { return Qt.point(Math.max(0, Math.min(7, Math.floor((mouse.x - 1) / 11))), Math.max(0, Math.min(7, Math.floor((mouse.y - 1) / 11)))) }
                            onPressed: function(mouse) { const p = at(mouse); paint = !patternBox.bit(p.x, p.y); patternBox.setBit(p.x, p.y, paint) }
                            onPositionChanged: function(mouse) { const p = at(mouse); patternBox.setBit(p.x, p.y, paint) }
                        }
                    }
                    Item {
                        x: 160; y: 18; width: 130; height: 90
                        // The strip of arrows over the pattern, to step through the patterns.
                        Rectangle {
                            width: parent.width; height: 14; color: "white"; border.color: "black"
                            Repeater {
                                model: 2
                                Text {
                                    required property int index
                                    objectName: index === 0 ? "classic-pattern-previous" : "classic-pattern-next"
                                    x: index === 0 ? parent.width / 2 - 26 : parent.width / 2 + 14; y: -2
                                    text: index === 0 ? "◀" : "▶"; font.pixelSize: 11; color: "black"
                                    MouseArea {
                                        anchors.fill: parent; anchors.margins: -4
                                        onClicked: {
                                            const n = patternBox.presets.length
                                            patternBox.preset = (patternBox.preset + (index === 0 ? n - 1 : 1)) % n
                                            patternBox.pattern = patternBox.presets[patternBox.preset]
                                        }
                                    }
                                }
                            }
                        }
                        Canvas {
                            id: preview
                            objectName: "classic-pattern-preview"
                            y: 13; width: parent.width; height: parent.height - 13
                            property string pattern: patternBox.pattern
                            onPatternChanged: requestPaint()
                            onPaint: {
                                const c = getContext("2d"); c.fillStyle = "white"; c.fillRect(0, 0, width, height); c.fillStyle = "black"
                                for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x)
                                    if (patternBox.bit(x % 8, y % 8)) c.fillRect(x, y, 1, 1)
                                c.strokeStyle = "black"; c.lineWidth = 1; c.strokeRect(0.5, 0.5, width - 1, height - 1)
                            }
                            MouseArea { anchors.fill: parent; onClicked: window.appearance.setDesktopPattern(patternBox.pattern) }
                        }
                    }
                    Caption { y: 118; width: parent.width; text: "Desktop Pattern" }
                }

                // Rate of Insertion Point Blinking
                Item {
                    x: general.patternWidth + 1; width: general.width - x; height: 150
                    Caption { y: 8; width: parent.width; text: "Rate of Insertion\nPoint Blinking" }
                    Rectangle {
                        id: caret
                        anchors.horizontalCenter: parent.horizontalCenter; y: 58; width: 2; height: 24; color: "black"
                        Timer { interval: window.appearance.caretFlashTime / 2; running: window.visible; repeat: true; onTriggered: caret.visible = !caret.visible }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 96; spacing: 22
                        Repeater {
                            model: ["Slow", "", "Fast"]
                            ClassicRadio {
                                required property var modelData
                                required property int index
                                objectName: "classic-caret-" + index
                                labelBelow: true; text: modelData; width: 40
                                checked: window.appearance.general.caretBlink === index
                                onClicked: window.appearance.setGeneralOption("caretBlink", index)
                            }
                        }
                    }
                }

                // Menu Blinking
                Item {
                    y: 151; width: general.blinkWidth; height: parent.height - 151 - 110
                    Caption { y: 10; width: parent.width; text: "Menu Blinking" }
                    // A small menu, its middle item chosen.
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 40; width: 84; height: 64; color: "white"; border.color: "black"
                        Rectangle { width: parent.width; height: 10; color: "white"; border.color: "black" }
                        Rectangle { x: 18; y: 0; width: 22; height: 10; color: "black" }
                        Rectangle { x: 18; y: 9; width: 40; height: 48; color: "white"; border.color: "black"
                            Repeater { model: 4; Rectangle { required property int index; x: 4; y: 5 + index * 11; width: 30; height: index === 1 ? 9 : 2; color: "black" } }
                        }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 118; spacing: 4
                        Repeater {
                            model: ["Off", "1", "2", "3"]
                            ClassicRadio {
                                required property var modelData
                                required property int index
                                objectName: "classic-menu-blink-" + index
                                labelBelow: true; text: modelData; width: 30
                                checked: window.appearance.general.menuBlinks === index
                                onClicked: window.appearance.setGeneralOption("menuBlinks", index)
                            }
                        }
                    }
                }

                // Time, 12 or 24 hours, and the date.
                Item {
                    id: timeBox
                    x: general.blinkWidth + 1; y: 151; width: general.volumeX - x; height: parent.height - 151 - 110
                    property date now: new Date()
                    Timer { interval: 1000; running: window.visible; repeat: true; triggeredOnStart: true; onTriggered: timeBox.now = new Date() }
                    readonly property bool hours24: window.appearance.clock.hours24 === true
                    Text { x: 10; y: 8; text: "Time"; textFormat: Text.PlainText; font.pixelSize: 14 }
                    StripGlyph { x: 64; y: 6; width: 22; height: 22; kind: "clock" }
                    Text {
                        objectName: "classic-time"
                        anchors.horizontalCenter: parent.horizontalCenter; y: 34
                        text: Qt.formatTime(timeBox.now, timeBox.hours24 ? "HH:mm:ss" : "h:mm:ss AP")
                        textFormat: Text.PlainText; font.pixelSize: 22; font.family: "monospace"
                    }
                    Rectangle { y: 72; width: parent.width; height: 1; color: "black" }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 82; spacing: 14
                        ClassicRadio { objectName: "classic-12hr"; text: "12hr."; checked: !timeBox.hours24; onClicked: window.appearance.setClockOption("hours24", false) }
                        ClassicRadio { objectName: "classic-24hr"; text: "24hr."; checked: timeBox.hours24; onClicked: window.appearance.setClockOption("hours24", true) }
                    }
                    Rectangle { y: 112; width: parent.width; height: 1; color: "black" }
                    Text { x: 10; y: 120; text: "Date"; textFormat: Text.PlainText; font.pixelSize: 14 }
                    Text {
                        objectName: "classic-date"
                        anchors.horizontalCenter: parent.horizontalCenter; y: 146
                        text: Qt.formatDate(timeBox.now, "M/ d/yy"); textFormat: Text.PlainText; font.pixelSize: 22; font.family: "monospace"
                    }
                    MouseArea { anchors.fill: parent; z: -1; onDoubleClicked: window.openPanel("date-time") }
                }

                // Speaker Volume, 0 to 7.
                Item {
                    x: general.volumeX + 1; y: 151; width: general.width - x; height: parent.height - 151
                    VolumeSlider { anchors.horizontalCenter: parent.horizontalCenter; y: 14; height: parent.height - 70 }
                    Caption { y: parent.height - 50; width: parent.width; text: "Speaker\nVolume" }
                }

                // The menu bar clock, on or off (where System 6 kept the RAM cache).
                Item {
                    y: parent.height - 109; width: general.volumeX; height: 109
                    Text { x: 24; anchors.verticalCenter: parent.verticalCenter; text: "Menu Bar Clock"; textFormat: Text.PlainText; font.pixelSize: 14 }
                    Column {
                        x: parent.width - 110; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                        ClassicRadio { objectName: "classic-clock-on"; text: "On"; checked: window.appearance.clock.visible !== false; onClicked: window.appearance.setClockOption("visible", true) }
                        ClassicRadio { objectName: "classic-clock-off"; text: "Off"; checked: window.appearance.clock.visible === false; onClicked: window.appearance.setClockOption("visible", false) }
                    }
                }
            }

            // Keyboard: Key Repeat Rate and Delay Until Repeat, as rows of buttons.
            Item {
                objectName: "classic-keyboard"
                anchors.fill: parent
                visible: window.entry.glyph === "keyboard"
                readonly property var rates: [4, 8, 15, 25, 40]
                readonly property var delays: [1200, 900, 600, 300]
                function nearest(list, value) {
                    let best = 0
                    for (let i = 1; i < list.length; ++i) if (Math.abs(list[i] - value) < Math.abs(list[best] - value)) best = i
                    return best
                }
                StripGlyph { anchors.horizontalCenter: parent.horizontalCenter; y: 30; width: 64; height: 64; kind: "keyboard" }
                Caption { y: 120; width: parent.width; text: "Key Repeat Rate" }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 150; spacing: 14
                    Text { text: "Slow"; font.pixelSize: 14; y: 1 }
                    Repeater {
                        model: 5
                        ClassicRadio {
                            required property int index
                            objectName: "classic-repeat-rate-" + index
                            checked: parent.parent.nearest(parent.parent.rates, window.input.keyboard.repeatRate) === index
                            onClicked: window.input.set("keyboard", "repeatRate", parent.parent.rates[index])
                        }
                    }
                    Text { text: "Fast"; font.pixelSize: 14; y: 1 }
                }
                Caption { y: 210; width: parent.width; text: "Delay Until Repeat" }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 240; spacing: 14
                    Text { text: "Long"; font.pixelSize: 14; y: 1 }
                    Repeater {
                        model: 4
                        ClassicRadio {
                            required property int index
                            objectName: "classic-repeat-delay-" + index
                            checked: parent.parent.nearest(parent.parent.delays, window.input.keyboard.repeatDelay) === index
                            onClicked: window.input.set("keyboard", "repeatDelay", parent.parent.delays[index])
                        }
                    }
                    Text { text: "Short"; font.pixelSize: 14; y: 1 }
                }
                ClassicButton { anchors.horizontalCenter: parent.horizontalCenter; y: 300; text: "Keyboard Layouts…"; onClicked: window.openPanel("keyboard") }
            }

            // Mouse: Mouse Tracking.
            Item {
                objectName: "classic-mouse"
                anchors.fill: parent
                visible: window.entry.glyph === "mouse"
                readonly property var speeds: [-0.75, -0.35, 0, 0.5]
                StripGlyph { anchors.horizontalCenter: parent.horizontalCenter; y: 30; width: 64; height: 64; kind: "mouse" }
                Caption { y: 120; width: parent.width; text: "Mouse Tracking" }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 150; spacing: 18
                    Repeater {
                        model: ["Very Slow", "Slow", "Medium", "Fast"]
                        ClassicRadio {
                            required property var modelData
                            required property int index
                            objectName: "classic-tracking-" + index
                            labelBelow: true; text: modelData
                            readonly property var speeds: [-0.75, -0.35, 0, 0.5]
                            checked: {
                                let best = 0
                                for (let i = 1; i < speeds.length; ++i)
                                    if (Math.abs(speeds[i] - window.input.pointer.speed) < Math.abs(speeds[best] - window.input.pointer.speed)) best = i
                                return best === index
                            }
                            onClicked: window.input.set("pointer", "speed", speeds[index])
                        }
                    }
                }
                ClassicButton { anchors.horizontalCenter: parent.horizontalCenter; y: 230; text: "Trackpad and Buttons…"; onClicked: window.openPanel("mouse") }
            }

            // Sound: the speaker volume, with the alert sounds a button away.
            Item {
                objectName: "classic-sound"
                anchors.fill: parent
                visible: window.entry.glyph === "sound"
                VolumeSlider { x: 60; y: 30; height: 300 }
                Caption { x: 20; y: 350; width: 120; text: "Speaker\nVolume" }
                StripGlyph { x: 230; y: 60; width: 64; height: 64; kind: "sound" }
                ClassicButton { x: 200; y: 160; text: "Alert Sounds…"; onClicked: window.openPanel("sound") }
            }

            // Every other icon opens its panel.
            Item {
                anchors.fill: parent
                visible: window.entry.action !== undefined
                StripGlyph { anchors.horizontalCenter: parent.horizontalCenter; y: 80; width: 72; height: 72; kind: window.entry.glyph }
                Caption { y: 176; width: parent.width; text: window.entry.name }
                ClassicButton {
                    objectName: "classic-open-panel"
                    anchors.horizontalCenter: parent.horizontalCenter; y: 220
                    text: "Open " + window.entry.name + "…"; onClicked: window.openPanel(window.entry.action)
                }
            }
        }
    }

    // System 6's speaker volume: a rounded groove marked 0 to 7 and a box to drag along it.
    component VolumeSlider: Item {
        id: slider
        objectName: "classic-volume"
        width: 70
        readonly property var volume: window.controls && window.controls.state ? (window.controls.state.volume || ({})) : ({})
        readonly property bool available: volume.available === true
        property int level: available ? Math.round((volume.value || 0) * 7 / 100) : 0
        property bool dragging: false
        readonly property real track: height - 24
        opacity: available ? 1 : 0.4
        Repeater {
            model: 8
            Item {
                required property int index
                y: 12 + slider.track * (7 - index) / 7 - 8; width: 30; height: 16
                Text { text: index; font.pixelSize: 14; x: 0 }
                Rectangle { x: 14; y: 8; width: 10; height: 1; color: "black" }
            }
        }
        Rectangle {
            x: 34; y: 0; width: 22; height: slider.height; radius: 11; border.color: "black"; border.width: 2
            Dots { x: 6; y: 8; width: 10; height: parent.height - 16 }
        }
        Rectangle {
            objectName: "classic-volume-knob"
            x: 28; y: 12 + slider.track * (7 - slider.level) / 7 - 9; width: 34; height: 18; radius: 3
            color: "white"; border.color: "black"; border.width: 2
            Rectangle { x: 5; y: 6; width: 24; height: 1; color: "black" }
            Rectangle { x: 5; y: 10; width: 24; height: 1; color: "black" }
        }
        MouseArea {
            anchors.fill: parent; enabled: slider.available; preventStealing: true
            function levelAt(y) { return Math.max(0, Math.min(7, Math.round(7 - (y - 12) * 7 / slider.track))) }
            onPressed: function(mouse) { slider.dragging = true; slider.level = levelAt(mouse.y) }
            onPositionChanged: function(mouse) { slider.level = levelAt(mouse.y) }
            onReleased: {
                slider.dragging = false
                if (window.controls) window.controls.set("volume", Math.round(slider.level * 100 / 7))
            }
        }
        // Follows the system volume again (keys, the Control Strip) once the new value is applied.
        Connections {
            target: window.controls
            function onStateChanged() {
                if (!slider.dragging)
                    slider.level = Qt.binding(function() { return slider.available ? Math.round((slider.volume.value || 0) * 7 / 100) : 0 })
            }
        }
    }
}
