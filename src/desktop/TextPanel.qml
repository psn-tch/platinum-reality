import QtQuick
import QtQuick.Controls

// Text, a panel of the Control Panels window, laid out as 9.2.2's (captured 2026-11-12): Text
// Behaviors, with Script and Behavior, and what they affect. Here a behavior is an installed
// locale's collation and character classes (LocaleSettings); its script comes with its language.
Item {
    id: panel
    required property var settings
    objectName: "text-panel"
    readonly property string panelId: "text"
    readonly property string title: "Text"
    readonly property string glyph: "text"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    onShownChanged: if (shown && settings) settings.refresh()
    readonly property var formats: settings ? settings.formats : []
    readonly property string chosen: settings ? settings.text : ""
    readonly property var chosenFormat: formats.find(function(f) { return f.name === panel.chosen }) || null
    // 9.2.2 calls the Latin script Roman.
    function scriptName(f) { return !f ? "" : f.script === "Latin" ? "Roman" : f.script }
    // The language, and the region too when two behaviors share a language.
    function label(f) {
        const shared = formats.filter(function(o) { return o.language === f.language }).length > 1
        return shared ? f.language + " (" + f.region + ")" : f.language
    }
    Column {
        id: body
        width: parent.width
        spacing: 12
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Text Behaviors"; font.pixelSize: 14; font.bold: true }
        Grid {
            anchors.horizontalCenter: parent.horizontalCenter
            columns: 2; columnSpacing: 10; rowSpacing: 10
            verticalItemAlignment: Grid.AlignVCenter
            Text { text: "Script:"; font.pixelSize: 13; font.bold: true }
            PlatinumPopup {
                objectName: "text-script"
                implicitWidth: 200
                enabled: false // The script follows the behavior's language.
                choices: panel.chosenFormat ? [{name: panel.scriptName(panel.chosenFormat), value: "script"}] : []
                current: "script"
            }
            Text { text: "Behavior:"; font.pixelSize: 13; font.bold: true }
            PlatinumPopup {
                objectName: "text-behavior"
                implicitWidth: 200
                enabled: panel.formats.length > 0
                choices: panel.formats.map(function(f) { return {name: panel.label(f), value: f.name} })
                current: panel.chosen
                onChosen: function(value) { panel.settings.setText(value) }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Affects sort order, case conversion, and word definitions."; font.pixelSize: 12
        }
        Text {
            objectName: "text-note"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
            text: (panel.settings && panel.settings.error ? panel.settings.error + " "
                   : panel.settings && panel.chosen !== panel.settings.startedText
                     ? "Programs use this behavior after you next log in. " : "")
                  + "The behaviors offered are the ones installed on this computer."
        }
    }
}
