import QtQuick
import QtQuick.Controls

// Extensions Manager, a panel of the Control Panels window, laid out as 9.2.2's (captured in the
// reference VM, 2026-11-12): Selected Set, a list of items grouped under disclosure triangles with
// On/Off, Name, Size, Version and Package, then Restart, Revert and Duplicate Set…, and Show Item
// Information. Here the items are the programs that start when you log in (StartupItems); the
// changes take effect at the next login, as 9.2.2's took effect at the next restart.
Item {
    id: panel
    required property var items
    property var sessionManager: null
    objectName: "extensions-manager-panel"
    readonly property string panelId: "extensions-manager"
    readonly property string title: "Extensions Manager"
    readonly property string glyph: "general"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    onShownChanged: if (shown && items) items.refresh()
    property string selectedId: ""
    readonly property var selected: (items ? items.items : []).find(function(i) { return i.id === panel.selectedId }) || null
    property var collapsed: ({})
    property bool showInformation: false
    readonly property var groups: [{user: true, title: "Your Startup Items"}, {user: false, title: "System Startup Items"}]
    // The rows on screen: each group's heading, then its items unless it is closed.
    readonly property var rows: {
        const all = items ? items.items : []
        const result = []
        for (const group of groups) {
            const members = all.filter(function(i) { return i.user === group.user })
            if (!members.length) continue
            const on = members.filter(function(i) { return i.on }).length
            result.push({heading: true, user: group.user, title: group.title, count: members.length,
                         state: on === 0 ? "off" : on === members.length ? "on" : "mixed",
                         size: members.reduce(function(sum, i) { return sum + i.size }, 0)})
            if (!collapsed[group.user ? "user" : "system"])
                for (const member of members) result.push(member)
        }
        return result
    }
    function sizeText(bytes) {
        if (!bytes) return "–"
        if (bytes >= 10 * 1024 * 1024) return Math.round(bytes / (1024 * 1024)).toLocaleString(Qt.locale("en_US"), "f", 0) + "M"
        return Math.max(1, Math.round(bytes / 1024)).toLocaleString(Qt.locale("en_US"), "f", 0) + "K"
    }
    // A 9.2.2 check box, small enough for a list row; "mixed" draws a dash.
    component SmallCheck: Rectangle {
        id: check
        property string state: "off"
        signal toggled()
        width: 12; height: 12; color: tap.pressed ? "#bbb" : "white"; border.color: "#333"
        Text {
            anchors.centerIn: parent; font.pixelSize: 11; font.bold: true
            text: check.state === "on" ? "✕" : check.state === "mixed" ? "–" : ""
        }
        MouseArea { id: tap; anchors.fill: parent; anchors.margins: -4; onClicked: check.toggled() }
    }
    Column {
        id: body
        width: Math.min(parent.width, 540)
        spacing: 10
        Row {
            spacing: 8
            Text { text: "Selected Set:"; font.pixelSize: 12; font.bold: true; textFormat: Text.PlainText
                   anchors.verticalCenter: parent.verticalCenter }
            PlatinumPopup {
                objectName: "extensions-manager-set"
                width: 230
                choices: (panel.items ? panel.items.sets : []).map(function(name) { return {name: name} })
                current: panel.items ? panel.items.selectedSet : ""
                onChosen: (value) => panel.items.selectSet(value)
            }
            PlatinumButton {
                objectName: "extensions-manager-delete-set"
                visible: Boolean(panel.items) && !panel.items.selectedLocked && panel.items.selectedSet !== "My Settings"
                text: "Delete Set"; width: 90; height: 22
                onClicked: panel.items.deleteSet(panel.items.selectedSet)
            }
        }
        Rectangle {
            id: table
            width: parent.width; height: 240; color: "white"; border.color: "#555"
            readonly property real onWidth: 54
            readonly property real sizeWidth: 62
            readonly property real versionWidth: 92
            readonly property real packageWidth: 110
            readonly property real nameWidth: width - 2 - onWidth - sizeWidth - versionWidth - packageWidth
            readonly property var columns: [{title: "On/Off", width: onWidth}, {title: "Name", width: nameWidth},
                                            {title: "Size", width: sizeWidth}, {title: "Version", width: versionWidth},
                                            {title: "Package", width: packageWidth}]
            Row {
                id: heading
                x: 1; y: 1; height: 18
                Repeater {
                    model: table.columns
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: modelData.width; height: heading.height
                        // 9.2.2 shades the column the list is sorted by (Name).
                        color: index === 1 ? "#aaa" : "#ccc"
                        Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#888" }
                        Rectangle { visible: index > 0; x: 0; y: 2; width: 1; height: parent.height - 4; color: "#888" }
                        Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: modelData.title
                               font.pixelSize: 11; textFormat: Text.PlainText }
                    }
                }
            }
            ListView {
                id: list
                objectName: "extensions-manager-list"
                x: 1; y: heading.height + 1; width: parent.width - 2; height: parent.height - y - 1; clip: true
                model: panel.rows
                ScrollBar.vertical: PlatinumScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool isHeading: Boolean(modelData.heading)
                    objectName: isHeading ? "extensions-manager-group-" + (modelData.user ? "user" : "system")
                                          : "extensions-manager-item-" + modelData.id
                    width: list.width; height: 20
                    readonly property bool chosen: !isHeading && panel.selectedId === modelData.id
                    color: chosen ? (typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff") : "white"
                    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#eee" }
                    // The disclosure triangle beside a group's box, as 9.2.2's.
                    Text {
                        visible: row.isHeading
                        objectName: "extensions-manager-disclose-" + (row.modelData.user ? "user" : "system")
                        x: 6; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 10
                        text: panel.collapsed[row.modelData.user ? "user" : "system"] ? "▶" : "▼"
                        MouseArea {
                            anchors.fill: parent; anchors.margins: -4
                            onClicked: {
                                const key = row.modelData.user ? "user" : "system"
                                const next = Object.assign({}, panel.collapsed); next[key] = !next[key]; panel.collapsed = next
                            }
                        }
                    }
                    SmallCheck {
                        objectName: row.isHeading ? "extensions-manager-group-check-" + (row.modelData.user ? "user" : "system")
                                                  : "extensions-manager-check-" + row.modelData.id
                        x: row.isHeading ? 24 : 24; anchors.verticalCenter: parent.verticalCenter
                        state: row.isHeading ? row.modelData.state : row.modelData.on ? "on" : "off"
                        onToggled: {
                            if (row.isHeading) panel.items.setGroupOn(row.modelData.user, row.modelData.state !== "on")
                            else panel.items.setOn(row.modelData.id, !row.modelData.on)
                        }
                    }
                    // A folder for a group, a small document for an item: our own drawings.
                    Item {
                        x: table.onWidth + (row.isHeading ? 6 : 22); width: 14; height: 14
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle { visible: row.isHeading; x: 0; y: 1; width: 6; height: 3; color: "#8b8bd8"; border.color: "#333" }
                        Rectangle { visible: row.isHeading; x: 0; y: 3; width: 14; height: 10; color: "#a9a9f0"; border.color: "#333" }
                        Rectangle { visible: !row.isHeading; x: 2; y: 0; width: 10; height: 14; color: "white"; border.color: "#333" }
                        Rectangle { visible: !row.isHeading; x: 4; y: 9; width: 6; height: 2; color: "#333" }
                    }
                    Text {
                        x: table.onWidth + (row.isHeading ? 26 : 42); width: table.nameWidth - (row.isHeading ? 30 : 46)
                        anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                        text: row.isHeading ? row.modelData.title : row.modelData.name
                        font.pixelSize: 12; font.bold: row.isHeading; textFormat: Text.PlainText
                    }
                    Text {
                        x: table.onWidth + table.nameWidth + 6; width: table.sizeWidth - 10
                        anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight
                        text: panel.sizeText(row.modelData.size); font.pixelSize: 12; textFormat: Text.PlainText
                        font.bold: row.isHeading
                    }
                    Text {
                        x: table.onWidth + table.nameWidth + table.sizeWidth + 6; width: table.versionWidth - 10
                        anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                        text: row.isHeading ? "–" : (row.modelData.version || "–"); font.pixelSize: 12; textFormat: Text.PlainText
                    }
                    Text {
                        x: table.onWidth + table.nameWidth + table.sizeWidth + table.versionWidth + 6; width: table.packageWidth - 10
                        anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                        text: row.isHeading ? "–" : (row.modelData.package || "–"); font.pixelSize: 12; textFormat: Text.PlainText
                    }
                    MouseArea {
                        x: table.onWidth; width: parent.width - x; height: parent.height
                        onClicked: if (!row.isHeading) panel.selectedId = row.modelData.id
                    }
                }
                Text {
                    visible: list.count === 0
                    anchors.centerIn: parent; color: "#666"; font.pixelSize: 12; textFormat: Text.PlainText
                    text: "No programs are set to start when you log in."
                }
            }
        }
        Row {
            spacing: 10
            anchors.right: parent.right
            PlatinumButton {
                objectName: "extensions-manager-restart"
                text: "Restart"; width: 90
                enabled: Boolean(panel.items) && panel.items.changedSinceLogin && Boolean(panel.sessionManager) && panel.sessionManager.powerAvailable
                onClicked: panel.sessionManager.restartComputer()
            }
            PlatinumButton {
                objectName: "extensions-manager-revert"
                text: "Revert"; width: 90
                enabled: Boolean(panel.items) && panel.items.changedSinceLogin
                onClicked: panel.items.revert()
            }
            PlatinumButton {
                objectName: "extensions-manager-duplicate"
                text: "Duplicate Set…"; width: 120
                onClicked: { naming.visible = true; setName.text = panel.items.selectedSet + " copy"; setName.selectAll(); setName.forceActiveFocus() }
            }
        }
        Row {
            id: naming
            visible: false
            spacing: 8
            Text { text: "Name of the new set:"; font.pixelSize: 12; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter }
            TextField {
                id: setName
                objectName: "extensions-manager-set-name"
                width: 180; height: 26; font.pixelSize: 12; selectByMouse: true; maximumLength: 64
                color: "black"; background: FieldBackground {}
                onAccepted: duplicate.clicked()
            }
            PlatinumButton {
                id: duplicate
                objectName: "extensions-manager-duplicate-ok"
                text: "Duplicate"; width: 90
                onClicked: if (panel.items.duplicateSet(setName.text)) naming.visible = false
            }
            PlatinumButton { text: "Cancel"; width: 70; onClicked: naming.visible = false }
        }
        Text {
            objectName: "extensions-manager-message"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; textFormat: Text.PlainText
            color: panel.items && panel.items.error ? "#8b1010" : "#333"
            text: panel.items && panel.items.error ? panel.items.error
                  : "Changes take effect the next time you log in or restart."
        }
        // Show Item Information: what the chosen item is and what it runs.
        Item {
            width: disclosure.implicitWidth; height: disclosure.implicitHeight
            Row {
                id: disclosure
                spacing: 6
                Text { text: panel.showInformation ? "▼" : "▶"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Show Item Information"; font.pixelSize: 12; font.bold: true; textFormat: Text.PlainText }
            }
            MouseArea {
                objectName: "extensions-manager-show-information"
                anchors.fill: parent
                onClicked: panel.showInformation = !panel.showInformation
            }
        }
        Text {
            objectName: "extensions-manager-information"
            visible: panel.showInformation
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; textFormat: Text.PlainText
            text: !panel.selected ? "Click an item to see its information."
                  : panel.selected.name + (panel.selected.comment ? "\n" + panel.selected.comment : "")
                    + "\nRuns: " + panel.selected.command + "\nFile: " + panel.selected.file
        }
    }
}
