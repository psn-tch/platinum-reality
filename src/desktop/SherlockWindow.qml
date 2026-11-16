import QtQuick
import QtQuick.Controls

// Sherlock 2, laid out as 9.2.2's (captured in the reference VM, 2026-11-15): the channel strip, the
// search field and its magnifying-glass button, File Names / Contents / Custom, the list of disks to
// search (On, Name, Index Status), which becomes the list of items found (Name, Kind, Date Modified,
// Size), and the lower pane: "Items Found" or where the chosen item is, from its disk down. Only the
// Files channel exists here; the Internet ones (and Custom searches) are not offered. The search is
// FileSearch's. Double-click (or Return) opens an item, as from the Finder.
DesktopWindow {
    id: sherlock
    objectName: "sherlock-window"
    title: "Sherlock 2"
    width: 490; height: 450
    resizable: true; zoomable: true; collapsible: true
    minimumSize: Qt.size(430, 400)
    required property var search
    property bool searchContents: false
    property bool searched: false
    property int selectedIndex: -1
    readonly property var results: search ? search.results : []
    readonly property var selected: selectedIndex >= 0 && selectedIndex < results.length ? results[selectedIndex] : null
    readonly property color highlight: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"
    onRaised: { field.forceActiveFocus(); if (search) search.refreshVolumes() }
    function find() {
        if (!search || field.text.trim() === "") return
        selectedIndex = -1
        searched = true
        search.search(field.text, searchContents)
    }
    // Find ▸ Find Files (Command-F) or Find by Content (Command-G): that kind of search, typing in the field.
    function findBy(byContents) {
        if (searchContents !== byContents) { searchContents = byContents; clearResults() }
        open(); field.forceActiveFocus(); field.selectAll()
    }
    // Back to the list of disks, as 9.2.2 does when the search text is changed.
    function clearResults() { if (search) search.cancel(); searched = false; selectedIndex = -1 }
    function openItem(item) {
        if (!item || typeof manager === "undefined" || !manager) return
        if (item.folder) manager.launchFinder(item.path)
        else manager.requestOpenDocument(item.path)
    }
    function sizeText(bytes) {
        if (bytes < 0) return "–"
        return bytes < 1024 * 1024 ? Math.max(1, Math.round(bytes / 1024)) + "K" : (bytes / 1024 / 1024).toFixed(1) + " MB"
    }
    // The search field's own Command keys (Copy, Paste, Select All) stay with it, not the desktop's.
    Keys.onShortcutOverride: function(event) {
        const command = event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)
        if (command && event.key >= Qt.Key_A && event.key <= Qt.Key_Z && event.key !== Qt.Key_W) event.accepted = true
    }
    Keys.onPressed: function(event) {
        const command = (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier)) !== 0
        if (command && event.key === Qt.Key_F) { sherlock.findBy(false); event.accepted = true }
        else if (command && event.key === Qt.Key_G) { sherlock.findBy(true); event.accepted = true }
        else if (!command && sherlock.searched && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && sherlock.selected) {
            sherlock.openItem(sherlock.selected); event.accepted = true
        } else if (!command && sherlock.searched && event.key === Qt.Key_Down) {
            sherlock.selectedIndex = Math.min(sherlock.results.length - 1, sherlock.selectedIndex + 1); event.accepted = true
        } else if (!command && sherlock.searched && event.key === Qt.Key_Up) {
            sherlock.selectedIndex = Math.max(0, sherlock.selectedIndex - 1); event.accepted = true
        }
    }
    // A column heading, as in the Finder's list view.
    component Heading: Rectangle {
        property alias text: label.text
        height: 18; color: "#ccc"
        Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#888" }
        Rectangle { x: parent.width - 1; y: 2; width: 1; height: parent.height - 4; color: "#888" }
        Text { id: label; x: 6; width: parent.width - 10; anchors.verticalCenter: parent.verticalCenter
               font.pixelSize: 11; textFormat: Text.PlainText; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
    }
    Item {
        anchors.fill: parent
        Column {
            id: top
            x: 10; y: 8; width: parent.width - 20
            spacing: 8
            // The channels: Files, then the empty places 9.2.2 kept for more.
            Rectangle {
                id: channels
                width: parent.width; height: 96; color: "#cfcfcf"; border.color: "#999"
                Grid {
                    x: 4; y: 4; columns: 9; spacing: 2
                    Repeater {
                        model: 18
                        delegate: Rectangle {
                            required property int index
                            width: Math.max(10, Math.floor((channels.width - 8 - 16) / 9)); height: 42
                            color: index === 0 ? "#9cc" : "#d8d8d8"
                            border.color: index === 0 ? "#333" : "#bbb"
                            Rectangle { anchors.fill: parent; anchors.margins: 1; color: "transparent"; border.color: "white"; opacity: 0.6 }
                            StripGlyph {
                                objectName: "sherlock-channel-files"
                                visible: parent.index === 0
                                anchors.centerIn: parent; width: 32; height: 32; kind: "finder"
                            }
                        }
                    }
                }
            }
            Row {
                width: parent.width; spacing: 10
                TextField {
                    id: field
                    objectName: "sherlock-field"
                    width: parent.width - magnifier.width - 10; height: 30
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 13; selectByMouse: true; color: "black"
                    selectionColor: sherlock.highlight
                    background: FieldBackground {}
                    onAccepted: sherlock.find()
                    onTextEdited: if (sherlock.searched) sherlock.clearResults()
                }
                // The round magnifying-glass button: Find.
                Rectangle {
                    id: magnifier
                    objectName: "sherlock-search"
                    width: 34; height: 34; radius: 17
                    color: press.pressed ? "#999" : "#ddd"; border.color: "#333"
                    Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: "#cde"; border.color: "#333"; border.width: 2
                                anchors.horizontalCenterOffset: -2; anchors.verticalCenterOffset: -2 }
                    Rectangle { x: 20; y: 19; width: 8; height: 3; radius: 1; color: "#333"; rotation: 45 }
                    MouseArea { id: press; anchors.fill: parent; onClicked: sherlock.find() }
                }
            }
            Row {
                spacing: 14
                PlatinumRadioButton { objectName: "sherlock-file-names"; text: "File Names"; checked: !sherlock.searchContents
                                      onClicked: { sherlock.searchContents = false; sherlock.clearResults() } }
                PlatinumRadioButton { objectName: "sherlock-contents"; text: "Contents"; checked: sherlock.searchContents
                                      onClicked: { sherlock.searchContents = true; sherlock.clearResults() } }
                PlatinumRadioButton { text: ""; enabled: false; autoExclusive: false }
                PlatinumPopup { anchors.verticalCenter: parent.verticalCenter; implicitWidth: 130; enabled: false
                                choices: [{name: "Custom…", value: "custom"}]; current: "custom" }
                PlatinumButton { text: "Edit…"; enabled: false; implicitWidth: 56 }
            }
        }
        // The middle list: the disks to search, or the items found.
        Rectangle {
            id: table
            x: 10; y: top.y + top.height + 8; width: parent.width - 20
            height: Math.max(110, (parent.height - y - 8) * 0.55)
            color: "white"; border.color: "#555"
            // Disks: On, Name, Index Status.
            Item {
                anchors.fill: parent; anchors.margins: 1
                visible: !sherlock.searched
                Row {
                    id: diskHeading
                    Heading { width: 34; text: "On" }
                    Heading { width: 160; text: "Name" }
                    Heading { width: diskHeading.parent.width - 194; text: "Index Status" }
                }
                ListView {
                    id: disks
                    objectName: "sherlock-volumes"
                    y: diskHeading.height; width: parent.width; height: parent.height - y; clip: true
                    model: sherlock.search ? sherlock.search.volumes : []
                    ScrollBar.vertical: PlatinumScrollBar {}
                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: disks.width; height: 20
                        Rectangle { x: 34; width: 160; height: parent.height; color: "#eee" }
                        PlatinumCheckBox {
                            objectName: "sherlock-volume-" + parent.index
                            x: 9; anchors.verticalCenter: parent.verticalCenter; text: ""
                            checked: parent.modelData.on
                            onToggled: sherlock.search.setVolumeOn(parent.modelData.path, checked)
                        }
                        MenuGlyph { x: 40; anchors.verticalCenter: parent.verticalCenter; kind: "disk" }
                        Text { x: 60; width: 130; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.name
                               font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Text { x: 202; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                               text: "Searched each time; not indexed" }
                    }
                }
            }
            // Items found: Name, Kind, Date Modified, Size.
            Item {
                anchors.fill: parent; anchors.margins: 1
                visible: sherlock.searched
                readonly property real nameWidth: width * 0.36
                readonly property real kindWidth: width * 0.26
                readonly property real dateWidth: width * 0.24
                Row {
                    id: resultHeading
                    Heading { width: resultHeading.parent.nameWidth; text: "Name" }
                    Heading { width: resultHeading.parent.kindWidth; text: "Kind" }
                    Heading { width: resultHeading.parent.dateWidth; text: "Date Modified" }
                    Heading { width: resultHeading.parent.width - resultHeading.parent.nameWidth - resultHeading.parent.kindWidth
                                     - resultHeading.parent.dateWidth; text: "Size" }
                }
                ListView {
                    id: found
                    objectName: "sherlock-results"
                    y: resultHeading.height; width: parent.width; height: parent.height - y; clip: true
                    model: sherlock.results
                    currentIndex: sherlock.selectedIndex
                    ScrollBar.vertical: PlatinumScrollBar {}
                    delegate: Rectangle {
                        id: hit
                        required property var modelData
                        required property int index
                        objectName: "sherlock-result-" + index
                        width: found.width; height: 18
                        color: index === sherlock.selectedIndex ? sherlock.highlight : "white"
                        readonly property real nameWidth: found.parent.nameWidth
                        MenuGlyph { x: 4; anchors.verticalCenter: parent.verticalCenter; kind: hit.modelData.folder ? "folder" : "document" }
                        Text { x: 24; width: hit.nameWidth - 28; anchors.verticalCenter: parent.verticalCenter; text: hit.modelData.name
                               font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Text { x: hit.nameWidth + 6; width: found.parent.kindWidth - 10; anchors.verticalCenter: parent.verticalCenter
                               text: hit.modelData.kind; font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Text { x: hit.nameWidth + found.parent.kindWidth + 6; width: found.parent.dateWidth - 10
                               anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12; elide: Text.ElideRight
                               text: Qt.formatDateTime(new Date(hit.modelData.modified), "M/d/yy, h:mm AP") }
                        Text { x: parent.width - 76; width: 54; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight
                               text: sherlock.sizeText(hit.modelData.size); font.pixelSize: 12 }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { sherlock.selectedIndex = hit.index; sherlock.forceActiveFocus() }
                            onDoubleClicked: sherlock.openItem(hit.modelData)
                        }
                    }
                }
            }
        }
        // The lower pane: how many were found, or where the chosen item is.
        Rectangle {
            id: lower
            objectName: "sherlock-lower-pane"
            x: 10; y: table.y + table.height + 8; width: parent.width - 20; height: parent.height - y - 10
            color: "white"; border.color: "#555"
            readonly property var where: sherlock.selected && sherlock.search ? sherlock.search.location(sherlock.selected.path) : []
            Text {
                objectName: "sherlock-items-found"
                visible: lower.where.length === 0
                x: 6; y: 5; font.pixelSize: 12
                text: !sherlock.searched ? "" : sherlock.search.searching ? "Searching… Items Found: " + sherlock.results.length
                      : "Items Found: " + sherlock.results.length + (sherlock.search.truncated ? " (the first " + sherlock.results.length + ")" : "")
            }
            ListView {
                objectName: "sherlock-location"
                anchors.fill: parent; anchors.margins: 4; clip: true
                visible: lower.where.length > 0
                model: lower.where
                ScrollBar.vertical: PlatinumScrollBar {}
                delegate: Item {
                    required property var modelData
                    width: ListView.view.width; height: 19
                    MenuGlyph { x: 4 + modelData.depth * 16; anchors.verticalCenter: parent.verticalCenter
                                kind: modelData.kind === "disk" ? "disk" : modelData.kind === "folder" ? "folder" : "document" }
                    Text { x: 24 + modelData.depth * 16; anchors.verticalCenter: parent.verticalCenter; text: modelData.name
                           font.pixelSize: 12; textFormat: Text.PlainText }
                    MouseArea { anchors.fill: parent; onDoubleClicked: if (modelData.kind !== "file" && sherlock.selected) {
                        // Opens that folder: the item's path up to this depth.
                        const parts = sherlock.selected.path.split("/")
                        manager.launchFinder(parts.slice(0, parts.length - (lower.where.length - 1 - modelData.depth)).join("/")) } }
                }
            }
        }
    }
}
