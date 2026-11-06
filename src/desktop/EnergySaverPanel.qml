import QtQuick
import QtQuick.Controls

// Energy Saver, a panel of the Control Panels window: as Mac OS 9's, a slider for how long the
// computer waits before sleeping and one for the display, each with Never at its right end.
// Saved in energy.json (EnergySettings); the session supervisor gives the times to swayidle.
// Sleep locks the computer first; any input turns the displays back on.
Item {
    id: panel
    required property var settings
    // The manager: Sleep Now, and whether power control exists here.
    property var sessionManager: null
    objectName: "energy-panel"
    readonly property string panelId: "energy"
    readonly property string title: "Energy Saver"
    readonly property string glyph: "energy"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    // Minutes at each slider step; 0 (Never) last.
    readonly property var sleepSteps: [5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 0]
    readonly property var displaySteps: [1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120, 0]
    function edit() { if (host) host.show(panelId) }
    function describe(minutes) {
        if (minutes === 0) return "Never"
        if (minutes < 60) return minutes + (minutes === 1 ? " minute" : " minutes")
        const hours = minutes / 60
        return (Number.isInteger(hours) ? hours : hours.toFixed(1)) + (hours === 1 ? " hour" : " hours")
    }
    function nearest(steps, minutes) {
        let best = 0
        for (let i = 0; i < steps.length; ++i)
            if (steps[i] === minutes) return i
            else if (steps[i] !== 0 && Math.abs(steps[i] - minutes) < Math.abs(steps[best] - minutes)) best = i
        return best
    }
    Column {
        id: body
        width: parent.width
        spacing: 10
        Row {
            spacing: 10
            StripGlyph { kind: "energy"; anchors.verticalCenter: parent.verticalCenter }
            Text {
                width: 440; wrapMode: Text.Wrap; anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12; textFormat: Text.PlainText
                text: "Save energy when the computer is not being used."
            }
        }
        // 9.2.2's Sleep Setup: a sentence above each slider, its steps ticked and labelled below.
        PlatinumGroup {
            title: "Sleep Setup"
            width: Math.min(parent.width, 500)
            spacing: 6
            Text {
                objectName: "energy-sleep-label"
                font.pixelSize: 12; textFormat: Text.PlainText
                text: "Put the computer to sleep whenever it’s inactive for: " + panel.describe(panel.settings.sleep)
            }
            PlatinumSlider {
                objectName: "energy-sleep"
                width: parent.width; from: 0; to: panel.sleepSteps.length - 1; stepSize: 1; snapMode: Slider.SnapAlways
                tickCount: panel.sleepSteps.length
                tickLabels: ["5 min", "", "", "20", "", "", "1 hr", "", "2", "", "4", "Never"]
                value: panel.nearest(panel.sleepSteps, panel.settings.sleep)
                onCommitted: function(value) { panel.settings.set("sleep", panel.sleepSteps[Math.round(value)]) }
            }
            Item { width: 1; height: 6 }
            Text {
                objectName: "energy-display-label"
                font.pixelSize: 12; textFormat: Text.PlainText
                text: "Turn the display off when inactive for: " + panel.describe(panel.settings.displayOff)
            }
            PlatinumSlider {
                objectName: "energy-display"
                width: parent.width; from: 0; to: panel.displaySteps.length - 1; stepSize: 1; snapMode: Slider.SnapAlways
                tickCount: panel.displaySteps.length
                tickLabels: ["1 min", "", "", "5", "", "", "20", "", "", "1 hr", "", "2", "Never"]
                value: panel.nearest(panel.displaySteps, panel.settings.displayOff)
                onCommitted: function(value) { panel.settings.set("displayOff", panel.displaySteps[Math.round(value)]) }
            }
        }
        Text {
            objectName: "energy-note"
            width: Math.min(parent.width, 500); wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: (panel.settings.sleep !== 0 && panel.settings.displayOff !== 0 && panel.settings.displayOff >= panel.settings.sleep
                   ? "The computer sleeps before the display would turn off. " : "")
                  + "The computer locks when it sleeps. Moving the mouse or pressing a key turns the display back on. "
                  + "A playing video, or Appearance's “Never turn the screen off automatically”, keeps both awake."
        }
        Row {
            spacing: 12
            PlatinumButton {
                objectName: "energy-sleep-now"; text: "Sleep Now"
                enabled: Boolean(panel.sessionManager) && panel.sessionManager.powerAvailable
                onClicked: panel.sessionManager.sleepComputer()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12; color: "#444"; textFormat: Text.PlainText
                visible: !(Boolean(panel.sessionManager) && panel.sessionManager.powerAvailable)
                text: "Sleep is available in the full session."
            }
        }
    }
}
