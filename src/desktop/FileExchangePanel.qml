import QtQuick
import QtQuick.Controls

// File Exchange, a panel of the Control Panels window, after 9.2.2's PC Exchange (captured in the
// reference VM, 2026-11-12): a list of extensions, the application that opens them and their kind,
// with Add…, Remove and Change…. Here it chooses which application opens each kind of document
// from the Finder and the desktop (ApplicationCatalog's associations, profile/associations.json).
Item {
    id: panel
    required property var catalog
    objectName: "file-exchange-panel"
    readonly property string panelId: "file-exchange"
    readonly property string title: "File Exchange"
    readonly property string glyph: "general"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    function edit() { if (host) host.show(panelId) }
    onShownChanged: if (shown && catalog && !catalog.ready && !catalog.busy) catalog.refresh()
    // Kinds added with Add… that have no application yet (they join the list once they have one).
    property var added: []
    readonly property var rows: {
        const listed = catalog ? catalog.fileTypes : []
        const extra = added.filter(function(a) { return !listed.some(function(r) { return r.type === a.type }) })
        return listed.concat(extra)
    }
    property string selectedType: ""
    readonly property var selected: rows.find(function(r) { return r.type === panel.selectedType }) || null
    property string message: ""
    function choose(type, id) {
        message = catalog.setFileTypeApplication(type, id) ? "" : "That choice could not be saved."
    }
    Column {
        id: body
        width: Math.min(parent.width, 520)
        spacing: 10
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; textFormat: Text.PlainText
            text: "When you open a document in the Finder or on the desktop, it opens in the application chosen here."
        }
        Rectangle {
            id: table
            width: parent.width; height: 222; color: "white"; border.color: "#555"
            readonly property real extensionWidth: 90
            readonly property real applicationWidth: (width - extensionWidth) * 0.48
            Rectangle {
                id: heading
                x: 1; y: 1; width: parent.width - 2; height: 18; color: "#ccc"
                Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#888" }
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: "Extension"; font.pixelSize: 11; textFormat: Text.PlainText }
                Rectangle { x: table.extensionWidth; y: 2; width: 1; height: parent.height - 4; color: "#888" }
                Text { x: table.extensionWidth + 8; anchors.verticalCenter: parent.verticalCenter; text: "Application"; font.pixelSize: 11; textFormat: Text.PlainText }
                Rectangle { x: table.extensionWidth + table.applicationWidth; y: 2; width: 1; height: parent.height - 4; color: "#888" }
                Text { x: table.extensionWidth + table.applicationWidth + 8; anchors.verticalCenter: parent.verticalCenter; text: "File Kind"; font.pixelSize: 11; textFormat: Text.PlainText }
            }
            ListView {
                id: list
                objectName: "file-exchange-list"
                x: 1; y: heading.height + 1; width: parent.width - 2; height: parent.height - y - 1; clip: true
                model: panel.rows
                ScrollBar.vertical: PlatinumScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "file-exchange-" + modelData.extension
                    width: list.width; height: 20
                    readonly property bool chosen: panel.selectedType === modelData.type
                    color: chosen ? (typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff") : "white"
                    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#eee" }
                    Text { x: 8; width: table.extensionWidth - 12; anchors.verticalCenter: parent.verticalCenter
                           text: "." + row.modelData.extension; font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight }
                    Text { x: table.extensionWidth + 8; width: table.applicationWidth - 14; anchors.verticalCenter: parent.verticalCenter
                           text: row.modelData.applicationName || "None"; font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight
                           color: row.modelData.applicationName ? "#111" : "#777"; font.italic: !row.modelData.applicationName }
                    Text { x: table.extensionWidth + table.applicationWidth + 8; width: parent.width - x - 6; anchors.verticalCenter: parent.verticalCenter
                           text: row.modelData.kind; font.pixelSize: 12; textFormat: Text.PlainText; elide: Text.ElideRight }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.selectedType = row.modelData.type
                        onDoubleClicked: { panel.selectedType = row.modelData.type; chooser.popupFor(row) }
                    }
                }
            }
        }
        Row {
            spacing: 10
            PlatinumButton {
                objectName: "file-exchange-add"
                text: "Add…"; width: 90
                onClicked: { adding.visible = true; extension.text = ""; extension.forceActiveFocus(); panel.message = "" }
            }
            PlatinumButton {
                objectName: "file-exchange-remove"
                text: "Remove"; width: 90
                enabled: Boolean(panel.selected) && panel.selected.application !== ""
                onClicked: panel.choose(panel.selected.type, "")
            }
            PlatinumButton {
                id: changeButton
                objectName: "file-exchange-change"
                text: "Change…"; width: 90
                enabled: Boolean(panel.selected)
                onClicked: chooser.popupFor(changeButton)
            }
        }
        Row {
            id: adding
            visible: false
            spacing: 8
            Text { text: "Extension:"; font.pixelSize: 12; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter }
            TextField {
                id: extension
                objectName: "file-exchange-extension"
                width: 90; height: 26; font.pixelSize: 12; selectByMouse: true
                color: "black"; background: FieldBackground {}
                onAccepted: addButton.clicked()
            }
            PlatinumButton {
                id: addButton
                objectName: "file-exchange-add-kind"
                text: "Add"; width: 70
                onClicked: {
                    const kind = panel.catalog.fileTypeFor(extension.text)
                    if (!kind.type) { panel.message = "No kind of document has the extension “" + extension.text + "”."; return }
                    if (!panel.rows.some(function(r) { return r.type === kind.type }))
                        panel.added = panel.added.concat([{type: kind.type, kind: kind.kind, extension: kind.extension,
                                                           application: "", applicationName: ""}])
                    panel.selectedType = kind.type
                    adding.visible = false
                    chooser.popupFor(changeButton)
                }
            }
            PlatinumButton { text: "Cancel"; width: 70; onClicked: adding.visible = false }
        }
        Text {
            objectName: "file-exchange-message"
            visible: text !== ""
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#8b1010"; textFormat: Text.PlainText
            text: panel.message
        }
    }
    // Change…: the applications that say they open the chosen kind, and None.
    Menu {
        id: chooser
        objectName: "file-exchange-chooser"
        property var choices: []
        function popupFor(item) {
            if (!panel.selected) return
            choices = panel.catalog.applicationsFor(panel.selected.type)
            const point = item.mapToItem(panel, 0, item.height)
            popup(point.x, point.y)
        }
        background: Rectangle { implicitWidth: 260; color: "#eee"; border.color: "#333" }
        Instantiator {
            model: chooser.choices.length ? chooser.choices : [{id: "", name: "No application says it opens this kind", none: true}]
            delegate: MenuItem {
                id: option
                required property var modelData
                objectName: "file-exchange-choice-" + (modelData.id || "none")
                enabled: !modelData.none
                text: (panel.selected && panel.selected.application === modelData.id && modelData.id ? "✓ " : "   ") + modelData.name
                contentItem: Text { text: option.text; textFormat: Text.PlainText; font.pixelSize: 12
                                    color: !option.enabled ? "#888" : option.highlighted ? "white" : "#111" }
                background: Rectangle { color: option.highlighted ? "#202050" : "transparent" }
                onTriggered: panel.choose(panel.selected.type, modelData.id)
            }
            onObjectAdded: (index, object) => chooser.insertItem(index, object)
            onObjectRemoved: (index, object) => chooser.removeItem(object)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "file-exchange-choice-forget"
            text: "   None (choose each time)"
            enabled: Boolean(panel.selected) && panel.selected.application !== ""
            contentItem: Text { text: parent.text; textFormat: Text.PlainText; font.pixelSize: 12
                                color: !parent.enabled ? "#888" : parent.highlighted ? "white" : "#111" }
            background: Rectangle { color: parent.highlighted ? "#202050" : "transparent" }
            onTriggered: panel.choose(panel.selected.type, "")
        }
    }
}
