import QtQuick
import QtQuick.Controls

// Date & Time, a panel of the Control Panels window: today, a month calendar, the time zone,
// and the menu bar clock's options (Mac OS 9's Date & Time "Menu Bar Clock" settings). The
// computer's own clock and zone are set by the system; this panel shows them.
Item {
    id: panel
    required property var settings
    objectName: "date-time-panel"
    readonly property string panelId: "date-time"
    readonly property string title: "Date & Time"
    readonly property string glyph: "clock"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    property date now: new Date()
    // The month the calendar shows, as numbers: a QML date at local midnight can move across a
    // daylight-saving change (1 November 2026 was one) and land in the previous month.
    property int year: now.getFullYear()
    property int monthIndex: now.getMonth()
    readonly property var month: new Date(year, monthIndex, 15)
    readonly property var clock: settings.clock
    readonly property var zone: settings.timeZone()
    function edit() { if (host) host.show(panelId) }
    function showMonth(delta) {
        const index = year * 12 + monthIndex + delta
        year = Math.floor(index / 12)
        monthIndex = index % 12
    }
    onShownChanged: if (shown) { now = new Date(); year = now.getFullYear(); monthIndex = now.getMonth() }
    Timer { interval: 1000; repeat: true; running: panel.shown; onTriggered: panel.now = new Date() }
    // Six weeks from the Sunday on or before the 1st.
    readonly property var days: {
        // Noon, never midnight, so no daylight-saving change moves a day.
        const first = new Date(year, monthIndex, 1, 12)
        let list = []
        for (let i = 0; i < 42; ++i) {
            const d = new Date(year, monthIndex, 1 - first.getDay() + i, 12)
            list.push({day: d.getDate(), inMonth: d.getMonth() === monthIndex,
                       today: d.getFullYear() === now.getFullYear() && d.getMonth() === now.getMonth()
                              && d.getDate() === now.getDate()})
        }
        return list
    }
    Column {
        id: body
        width: parent.width
        spacing: 12
        Row {
            width: parent.width; spacing: 18
            // Today and the time, large.
            Column {
                width: 230; spacing: 4
                Text {
                    objectName: "date-time-today"
                    width: parent.width; wrapMode: Text.Wrap
                    text: Qt.formatDate(panel.now, "dddd, MMMM d, yyyy"); textFormat: Text.PlainText
                    font.pixelSize: 13; font.bold: true
                }
                Text {
                    objectName: "date-time-now"
                    text: Qt.formatTime(panel.now, panel.clock.hours24 ? "HH:mm:ss" : "h:mm:ss AP"); textFormat: Text.PlainText
                    font.pixelSize: 26
                }
                Text {
                    objectName: "date-time-zone"
                    width: parent.width; wrapMode: Text.Wrap; color: "#333"; font.pixelSize: 12; textFormat: Text.PlainText
                    text: "Time zone: " + panel.zone.name + (panel.zone.id ? " (" + panel.zone.id + ")" : "")
                          + "\n" + panel.zone.offset
                }
            }
            // The month, Sunday first; today circled.
            Rectangle {
                id: calendar
                objectName: "date-time-calendar"
                width: 7 * 34 + 12; height: 220
                color: "white"; border.color: "#555"
                Row {
                    x: 6; y: 6; width: parent.width - 12; height: 24
                    PlatinumButton {
                        objectName: "date-time-previous"; text: "◀"; implicitWidth: 28; implicitHeight: 22
                        onClicked: panel.showMonth(-1)
                    }
                    Text {
                        objectName: "date-time-month"
                        width: parent.width - 56; height: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        text: Qt.formatDate(panel.month, "MMMM yyyy"); textFormat: Text.PlainText; font.pixelSize: 12; font.bold: true
                    }
                    PlatinumButton {
                        objectName: "date-time-next"; text: "▶"; implicitWidth: 28; implicitHeight: 22
                        onClicked: panel.showMonth(1)
                    }
                }
                Grid {
                    x: 6; y: 34; columns: 7
                    Repeater {
                        model: ["S", "M", "T", "W", "T", "F", "S"]
                        Text {
                            required property string modelData
                            width: 34; height: 20; horizontalAlignment: Text.AlignHCenter
                            text: modelData; font.pixelSize: 11; font.bold: true; color: "#444"
                        }
                    }
                    Repeater {
                        model: panel.days
                        Item {
                            required property var modelData
                            width: 34; height: 26
                            Rectangle {
                                anchors.centerIn: parent; width: 24; height: 24; radius: 12
                                visible: modelData.today; color: panel.settings.accentColor
                            }
                            Text {
                                anchors.centerIn: parent
                                text: modelData.day; font.pixelSize: 12; font.bold: modelData.today
                                color: modelData.today ? "white" : modelData.inMonth ? "#111" : "#aaa"
                            }
                        }
                    }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: "#999" }
        Text { text: "Menu Bar Clock"; font.pixelSize: 12; font.bold: true }
        // A sample of the menu bar as it will look.
        Rectangle {
            width: Math.max(220, sample.implicitWidth + 24); height: 24
            color: "#ddd"; border.color: "#222"
            Text {
                id: sample
                objectName: "date-time-sample"
                anchors.centerIn: parent
                text: panel.clock.visible ? desktopClock(panel.now) : "No clock"; textFormat: Text.StyledText
                font.pixelSize: 12; color: panel.clock.visible ? "black" : "#888"
                function desktopClock(time) {
                    return typeof desktop !== "undefined" && desktop ? desktop.clockText(time, panel.clock, false)
                                                                     : Qt.formatTime(time, "h:mm AP")
                }
            }
        }
        Grid {
            columns: 2; columnSpacing: 24; rowSpacing: 6
            Repeater {
                model: [{key: "visible", label: "Show the clock in the menu bar"},
                        {key: "hours24", label: "Use a 24-hour clock"},
                        {key: "seconds", label: "Show the seconds"},
                        {key: "weekday", label: "Show the day of the week"},
                        {key: "date", label: "Show the date"},
                        {key: "flash", label: "Flash the time separators"}]
                PlatinumCheckBox {
                    required property var modelData
                    objectName: "clock-" + modelData.key
                    text: modelData.label
                    enabled: modelData.key === "visible" || panel.clock.visible
                    checked: panel.clock[modelData.key] === true
                    onToggled: panel.settings.setClockOption(modelData.key, checked)
                }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "Click the clock in the menu bar to see the date."
        }
    }
}
