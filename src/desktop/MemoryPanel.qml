import QtQuick
import QtQuick.Controls

// Memory, a panel of the Control Panels window, laid out as 9.2.2's (captured 2026-11-12): Disk
// Cache, Virtual Memory and RAM Disk in three rows, each with its picture, its choices and its
// details. On Linux the kernel and the system manage all three (MemoryInfo), so the choices show
// how things are and cannot be changed here.
Item {
    id: panel
    required property var info
    objectName: "memory-panel"
    readonly property string panelId: "memory"
    readonly property string title: "Memory"
    readonly property string glyph: "memory"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    onShownChanged: if (shown && info) info.refresh()
    // The cache grows and shrinks as the computer runs; keep it current while the panel is showing.
    Timer { interval: 2000; repeat: true; running: panel.shown && panel.info !== null; onTriggered: panel.info.refresh() }
    readonly property var facts: info ? info.facts : ({})
    // Sizes as 9.2.2 wrote them: K below a megabyte, else M.
    function size(bytes) {
        const value = Number(bytes || 0)
        return value < 1024 * 1024 ? Math.round(value / 1024) + "K" : Math.round(value / (1024 * 1024)) + "M"
    }
    component Divider: Rectangle { width: parent.width; height: 2; color: "transparent"
        Rectangle { width: parent.width; height: 1; color: "#888" }
        Rectangle { y: 1; width: parent.width; height: 1; color: "white" } }
    component Field: Rectangle {
        property alias text: value.text
        width: 72; height: 20; color: "white"; border.color: "#555"
        Text { id: value; anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
               font.pixelSize: 12; textFormat: Text.PlainText }
    }
    // One setting: picture, name and choices on the left; details on the right.
    component Setting: Row {
        id: setting
        property string kind: ""
        property string name: ""
        default property alias details: detailsColumn.data
        property alias choices: choicesColumn.data
        width: parent.width; spacing: 12
        StripGlyph { y: 4; width: 36; height: 36; kind: setting.kind }
        Column {
            width: 130; spacing: 0
            Text { text: setting.name; font.pixelSize: 12; bottomPadding: 4 }
            Column { id: choicesColumn; spacing: 0 }
        }
        Column { id: detailsColumn; width: setting.width - 36 - 130 - 24; spacing: 6 }
    }
    Column {
        id: body
        width: parent.width
        spacing: 10
        Setting {
            objectName: "memory-disk-cache"
            kind: "diskcache"; name: "Disk Cache"
            choices: [
                PlatinumRadioButton { text: "Default setting"; checked: true; enabled: false; autoExclusive: false },
                PlatinumRadioButton { text: "Custom setting"; enabled: false; autoExclusive: false }
            ]
            Text {
                objectName: "memory-cache-text"
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12
                text: "Linux sizes the disk cache as the computer runs, using memory nothing else needs. " +
                      "Its current size is " + panel.size(panel.facts.cache) + "."
            }
        }
        Divider {}
        Setting {
            objectName: "memory-virtual-memory"
            kind: "memory"; name: "Virtual Memory"
            choices: [
                PlatinumRadioButton { objectName: "memory-swap-on"; text: "On"; checked: panel.facts.swapOn === true
                                      enabled: false; autoExclusive: false },
                PlatinumRadioButton { objectName: "memory-swap-off"; text: "Off"; checked: panel.facts.swapOn === false
                                      enabled: false; autoExclusive: false }
            ]
            Text { objectName: "memory-swap-kind"; width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12
                   text: panel.facts.swapOn ? "Uses " + panel.facts.swapKind + "." : "No swap space is set up." }
            Text { objectName: "memory-built-in"; font.pixelSize: 12
                   text: "Available built-in memory: " + panel.size(panel.facts.builtIn) }
            Row {
                spacing: 8; visible: panel.facts.swapOn === true
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Size:"; font.pixelSize: 12 }
                Field { objectName: "memory-swap-size"; text: panel.size(panel.facts.swapSize) }
                Text { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                       text: "in use: " + panel.size(panel.facts.swapUsed) }
            }
        }
        Divider {}
        Setting {
            objectName: "memory-ram-disk"
            kind: "ramdisk"; name: "RAM Disk"
            choices: [
                PlatinumRadioButton { objectName: "memory-ramdisk-on"; text: "On"; checked: panel.facts.ramDisk === true
                                      enabled: false; autoExclusive: false },
                PlatinumRadioButton { objectName: "memory-ramdisk-off"; text: "Off"; checked: panel.facts.ramDisk === false
                                      enabled: false; autoExclusive: false }
            ]
            Text { width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12
                   text: panel.facts.ramDisk ? "The temporary items folder (/tmp) is a RAM disk. It can grow to this part of " +
                                               "built-in memory:"
                                             : "The temporary items folder (/tmp) is on the hard disk." }
            PlatinumSlider {
                objectName: "memory-ramdisk-percent"
                visible: panel.facts.ramDisk === true
                width: parent.width; from: 0; to: 100; enabled: false
                tickCount: 3; tickLabels: ["0%", "50%", "100%"]
                value: panel.facts.builtIn ? 100 * Number(panel.facts.ramDiskSize || 0) / Number(panel.facts.builtIn) : 0
            }
            Row {
                spacing: 8; visible: panel.facts.ramDisk === true
                Text { anchors.verticalCenter: parent.verticalCenter; text: "RAM Disk Size:"; font.pixelSize: 12 }
                Field { objectName: "memory-ramdisk-size"; text: panel.size(panel.facts.ramDiskSize) }
                Text { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                       text: "in use: " + panel.size(panel.facts.ramDiskUsed) }
            }
            // A tmpfs is never saved: /tmp starts empty at every startup.
            PlatinumCheckBox { visible: panel.facts.ramDisk === true; text: "Save contents to disk on Restart and Shutdown"
                               checked: false; enabled: false }
        }
        Divider {}
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
            text: "Linux manages the disk cache, virtual memory and the RAM disk itself, so they are shown here as they are."
        }
    }
}
