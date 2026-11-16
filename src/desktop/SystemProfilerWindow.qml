import QtQuick
import QtQuick.Controls

// Apple System Profiler, laid out as 9.2.2's (captured in the reference VM, 2026-11-15): folder tabs,
// then on System Profile the overviews under disclosure triangles (Software, Memory, Hardware,
// Network, Printer), and on Devices and Volumes each bus with its devices in boxes, the disks with
// their volumes. The facts are this computer's (SystemProfile, MemoryInfo), read when it opens. Of
// 9.2.2's tabs, Control Panels, Extensions, Applications and System Folders have nothing to list here.
DesktopWindow {
    id: profiler
    objectName: "system-profiler-window"
    title: "Apple System Profiler"
    width: 620; height: 470
    resizable: true; zoomable: true; collapsible: true
    minimumSize: Qt.size(480, 320)
    required property var source
    property var memory: null
    property var facts: ({})
    property string tab: "profile"
    property var closedSections: ({network: true})
    onRaised: refresh()
    function refresh() { if (source) facts = source.profile(); if (memory) memory.refresh() }
    readonly property var software: facts.software || ({})
    readonly property var hardware: facts.hardware || ({})
    readonly property var mem: memory ? memory.facts : ({})
    function megabytes(bytes) { return bytes ? Math.round(Number(bytes) / (1024 * 1024)) + " MB" : "–" }
    function toggle(section) {
        const next = Object.assign({}, closedSections)
        next[section] = !next[section]
        closedSections = next
    }
    // A folder tab, as 9.2.2's: slanted ends, the chosen one joined to the page.
    component FolderTab: Item {
        id: folderTab
        property string text: ""
        property string value: ""
        readonly property bool chosen: profiler.tab === value
        width: label.implicitWidth + 30; height: 20
        Canvas {
            anchors.fill: parent
            property bool chosen: folderTab.chosen
            onChosenChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const c = getContext("2d"); c.reset()
                c.beginPath(); c.moveTo(0.5, height); c.lineTo(8.5, 0.5); c.lineTo(width - 8.5, 0.5); c.lineTo(width - 0.5, height)
                c.fillStyle = chosen ? "#eee" : "#ccc"; c.fill(); c.strokeStyle = "#333"; c.stroke()
            }
        }
        Text { id: label; anchors.centerIn: parent; text: folderTab.text; font.pixelSize: 11; textFormat: Text.PlainText }
        MouseArea { anchors.fill: parent; onClicked: profiler.tab = folderTab.value }
    }
    // An overview under its disclosure triangle.
    component Section: Column {
        id: section
        property string name: ""
        property string key: ""
        default property alias rows: body.data
        readonly property bool open: !profiler.closedSections[key]
        width: parent ? parent.width : 400
        spacing: 4
        Row {
            spacing: 6
            Text { objectName: "profiler-triangle-" + section.key; text: section.open ? "▼" : "▶"; font.pixelSize: 11; color: "#336"
                   MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: profiler.toggle(section.key) } }
            Text { text: section.name; font.pixelSize: 12 }
        }
        Column { id: body; x: 20; width: section.width - 20; spacing: 2; visible: section.open }
    }
    // "Label:  value", as the overviews list their facts.
    component Fact: Row {
        property string label: ""
        property var value: "" // Undefined until the facts are read.
        spacing: 8
        Text { width: 140; text: parent.label + ":"; font.pixelSize: 12; textFormat: Text.PlainText }
        Text { text: parent.value ? String(parent.value) : "–"; font.pixelSize: 12; textFormat: Text.PlainText }
    }
    // A device or volume in its box, as on Devices and Volumes.
    component Box: Rectangle {
        property alias text: boxLabel.text
        width: Math.min(260, boxLabel.implicitWidth + 16); height: 18; color: "#eee"; border.color: "#555"
        Text { id: boxLabel; x: 6; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 10; elide: Text.ElideRight
               font.pixelSize: 11; textFormat: Text.PlainText }
    }
    Item {
        anchors.fill: parent
        Row {
            id: tabs
            x: 8; y: 6; spacing: -4
            FolderTab { objectName: "profiler-tab-profile"; text: "System Profile"; value: "profile" }
            FolderTab { objectName: "profiler-tab-devices"; text: "Devices and Volumes"; value: "devices" }
        }
        Rectangle {
            id: page
            x: 4; y: tabs.y + tabs.height; width: parent.width - 8; height: parent.height - y - 4
            color: "#eee"; border.color: "#333"
            Flickable {
                anchors.fill: parent; anchors.margins: 8; clip: true
                contentHeight: profiler.tab === "profile" ? profile.implicitHeight : devices.implicitHeight
                ScrollBar.vertical: PlatinumScrollBar {}
                Column {
                    id: profile
                    objectName: "profiler-profile"
                    visible: profiler.tab === "profile"
                    width: parent.width - 20; spacing: 10
                    Section {
                        name: "Software overview"; key: "software"
                        PlatinumGroup {
                            title: "Linux overview"; width: parent.width - 10; spacing: 2
                            Fact { objectName: "profiler-system"; label: "System"; value: profiler.software.system }
                            Fact { label: "Kernel"; value: profiler.software.kernel }
                            Fact { objectName: "profiler-platinum"; label: "Platinum Reality"; value: profiler.software.platinum }
                            Fact { label: "Toolkit"; value: profiler.software.toolkit }
                        }
                    }
                    Section {
                        name: "Memory overview"; key: "memory"
                        Fact { label: "Disk cache"; value: profiler.megabytes(profiler.mem.cache) }
                        Fact { label: "Virtual memory"; value: profiler.mem.swapOn ? profiler.megabytes(profiler.mem.swapSize) : "Off" }
                        Fact { objectName: "profiler-built-in"; label: "Built-in memory"; value: profiler.megabytes(profiler.mem.builtIn) }
                    }
                    Section {
                        name: "Hardware overview"; key: "hardware"
                        Fact { objectName: "profiler-model"; label: "Model name"; value: [profiler.hardware.vendor, profiler.hardware.model].filter(Boolean).join(" ") }
                        Fact { objectName: "profiler-processor"; label: "Processor info"; value: profiler.hardware.processor }
                        Fact { label: "Processor cores"; value: profiler.hardware.cores ? String(profiler.hardware.cores) : "" }
                        Fact { label: "Machine speed"; value: profiler.hardware.speed }
                    }
                    Section {
                        name: "Network overview"; key: "network"
                        Repeater {
                            model: profiler.facts.network || []
                            delegate: Fact { required property var modelData
                                             label: modelData.kind + " (" + modelData.name + ")"
                                             value: modelData.addresses.length ? modelData.addresses.join(", ") : "No address" }
                        }
                        Text { visible: !(profiler.facts.network || []).length; text: "No network is connected."; font.pixelSize: 12 }
                    }
                    Row { spacing: 26
                          Text { text: "Printer overview"; font.pixelSize: 12 }
                          Text { text: "Note: No printer has been chosen."; font.pixelSize: 12 } }
                }
                Column {
                    id: devices
                    objectName: "profiler-devices"
                    visible: profiler.tab === "devices"
                    width: parent.width - 20; spacing: 12
                    // USB, one row per bus with its devices.
                    Repeater {
                        model: {
                            const buses = []
                            for (const device of profiler.facts.usb || []) if (!buses.includes(device.bus)) buses.push(device.bus)
                            return buses.sort(function(a, b) { return a - b })
                        }
                        delegate: Row {
                            required property int modelData
                            spacing: 10
                            Text { width: 90; text: "USB " + modelData; font.pixelSize: 12 }
                            Column {
                                spacing: 4
                                Repeater {
                                    model: (profiler.facts.usb || []).filter(function(d) { return d.bus === modelData })
                                    delegate: Box { required property var modelData; text: modelData.name }
                                }
                            }
                        }
                    }
                    // Disks, each joined to its mounted volumes.
                    Repeater {
                        model: profiler.facts.storage || []
                        delegate: Row {
                            id: disk
                            required property var modelData
                            spacing: 10
                            Text { width: 90; font.pixelSize: 12; text: disk.modelData.usb ? "USB disk" : disk.modelData.removable ? "Removable" : "Internal" }
                            Box { objectName: "profiler-disk-" + disk.modelData.name
                                  text: (disk.modelData.model || disk.modelData.name) + (disk.modelData.size ? ", " + disk.modelData.size : "") }
                            Text { text: "—"; font.pixelSize: 12; visible: disk.modelData.volumes.length > 0 }
                            Column {
                                spacing: 4
                                Repeater {
                                    model: disk.modelData.volumes
                                    delegate: Row {
                                        required property var modelData
                                        spacing: 6
                                        MenuGlyph { kind: "disk"; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: modelData.name + (modelData.size ? " (" + modelData.size + ", " + modelData.fileSystem + ")" : "")
                                               font.pixelSize: 11; textFormat: Text.PlainText }
                                    }
                                }
                                Text { visible: disk.modelData.volumes.length === 0; text: "No mounted volumes"; font.pixelSize: 11; color: "#555" }
                            }
                        }
                    }
                    // Video: each card and the displays connected to it.
                    Repeater {
                        model: profiler.facts.video || []
                        delegate: Row {
                            required property var modelData
                            spacing: 10
                            Text { width: 90; text: "Video"; font.pixelSize: 12 }
                            Box { text: modelData.card }
                            Text { text: "—"; font.pixelSize: 12; visible: modelData.connectors.length > 0 }
                            Column {
                                spacing: 4
                                Repeater { model: modelData.connectors; delegate: Box { required property string modelData; text: "Display (" + modelData + ")" } }
                            }
                        }
                    }
                }
            }
        }
    }
}
