import QtQuick
import QtQuick.Controls

// Numbers, a panel of the Control Panels window, laid out as 9.2.2's (captured 2026-11-12): Number
// Format over Separators and Currency, and a Sample. Here a number format is an installed locale
// (LocaleSettings); its separators and currency come with it, so they are shown, not edited.
Item {
    id: panel
    required property var settings
    objectName: "numbers-panel"
    readonly property string panelId: "numbers"
    readonly property string title: "Numbers"
    readonly property string glyph: "numbers"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    onShownChanged: if (shown && settings) settings.refresh()
    readonly property var formats: settings ? settings.formats : []
    readonly property string chosen: settings ? settings.numbers : ""
    readonly property var format: settings ? settings.numberFormat(chosen) : ({})
    // The region, and the language too when two formats share a region.
    function label(f) {
        const shared = formats.filter(function(o) { return o.region === f.region }).length > 1
        return shared ? f.region + " (" + f.language + ")" : f.region
    }
    // A read-only field, as 9.2.2's separator and symbol boxes.
    component Field: Rectangle {
        property alias text: value.text
        width: 34; height: 20; color: "white"; border.color: "#555"
        Text { id: value; anchors.centerIn: parent; font.pixelSize: 13; font.bold: true; textFormat: Text.PlainText }
    }
    Column {
        id: body
        width: parent.width
        spacing: 12
        Row {
            spacing: 8
            StripGlyph { anchors.verticalCenter: parent.verticalCenter; width: 24; height: 24; kind: "numbers" }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Number Format:"; font.pixelSize: 13; font.bold: true }
            PlatinumPopup {
                objectName: "numbers-format"
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 230
                enabled: panel.formats.length > 0
                choices: panel.formats.map(function(f) { return {name: panel.label(f), value: f.name} })
                current: panel.chosen
                onChosen: function(value) { panel.settings.setNumbers(value) }
            }
        }
        Row {
            spacing: 10
            width: parent.width
            PlatinumGroup {
                title: "Separators"; objectName: "numbers-separators"
                width: (parent.width - 10) / 2; spacing: 8
                Row { spacing: 8
                      Text { width: 80; anchors.verticalCenter: parent.verticalCenter; text: "Decimal:"; font.pixelSize: 13; font.bold: true }
                      Field { objectName: "numbers-decimal"; text: panel.format.decimal || "" } }
                Row { spacing: 8
                      Text { width: 80; anchors.verticalCenter: parent.verticalCenter; text: "Thousands:"; font.pixelSize: 13; font.bold: true }
                      // A space separator shows as a space, so the box is not mistaken for empty.
                      Field { objectName: "numbers-thousands"; text: (panel.format.thousands || "").replace(/[\s  ]/, "␣") } }
            }
            PlatinumGroup {
                title: "Currency"; objectName: "numbers-currency"
                width: (parent.width - 10) / 2; spacing: 4
                Row { spacing: 8
                      Text { anchors.verticalCenter: parent.verticalCenter; text: "Symbol:"; font.pixelSize: 13; font.bold: true }
                      Field { objectName: "numbers-symbol"; width: 48; text: panel.format.symbol || "" } }
                PlatinumRadioButton { objectName: "numbers-before"; text: "Before number"; enabled: false; autoExclusive: false
                                      checked: panel.format.before === true }
                PlatinumRadioButton { objectName: "numbers-after"; text: "After number"; enabled: false; autoExclusive: false
                                      checked: panel.format.before === false }
            }
        }
        PlatinumGroup {
            title: "Sample"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 200; padding: 10
            Text { objectName: "numbers-sample"; width: parent.width; horizontalAlignment: Text.AlignHCenter
                   text: panel.format.sample || ""; font.pixelSize: 14; textFormat: Text.PlainText }
        }
        Text {
            objectName: "numbers-note"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
            text: (panel.settings && panel.settings.error ? panel.settings.error + " "
                   : panel.settings && panel.chosen !== panel.settings.startedNumbers
                     ? "Programs use this format after you next log in. " : "")
                  + "The formats offered are the ones installed on this computer."
        }
    }
}
