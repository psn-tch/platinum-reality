import QtQuick
import QtQuick.Controls

// Platinum Asset Importer (docs/asset-importer.md): brings the sounds, icons, cursors and pictures
// from the user's own Mac OS 9 into their private assets folder. It lists the Mac OS 9 disks, CD
// images and System Folders it finds, or takes a path; nothing is downloaded and the source is
// only read. Afterwards it reports each kind of asset: imported, already there, or not found.
DesktopWindow {
    id: importer
    objectName: "asset-importer-window"
    title: "Platinum Asset Importer"
    width: 480; height: 540
    required property var service
    property string chosen: ""
    readonly property bool busy: service ? service.busy : false
    readonly property bool importing: busy && !service.finding
    property bool done: false
    function start() {
        const path = pathField.text.trim()
        if (path === "" || !service) return
        done = false
        service.importFrom(path, replaceBox.checked)
    }
    function sizeText(bytes) {
        if (!bytes) return ""
        return bytes >= 1073741824 ? (bytes / 1073741824).toFixed(1) + " GB" : Math.round(bytes / 1048576) + " MB"
    }
    onVisibleChanged: if (visible && service && !service.busy) { done = false; service.findSources() }
    Connections {
        target: importer.service
        function onFinished() { importer.done = true }
    }
    Column {
        x: 16; y: 12; width: parent.width - 32; spacing: 8
        Text {
            width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 12; textFormat: Text.PlainText
            text: "Bring the sounds, icons, cursors and desktop pictures from your own copy of Mac OS 9 "
                + "into Platinum Reality. Choose a Mac OS 9 disk, CD image or System Folder. Nothing is "
                + "downloaded, and your disk is only read, never changed."
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 10; color: "#555"; textFormat: Text.PlainText
            text: "Use a copy of Mac OS you own. Imported files stay in your own folder: " +
                  (importer.service ? importer.service.assets : "")
        }
        Text {font.pixelSize: 12; font.bold: true; text: importer.service && importer.service.finding
                                                          ? "Looking for Mac OS 9 disks…" : "Mac OS 9 disks found:"}
        Rectangle {
            width: parent.width; height: 112; color: "white"; border.color: "#555"
            ListView {
                id: sources
                objectName: "asset-importer-sources"
                anchors.fill: parent; anchors.margins: 1; clip: true
                model: importer.service ? importer.service.sources : []
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "asset-importer-source-" + index
                    width: ListView.view.width; height: 34
                    readonly property bool current: importer.chosen === modelData.path
                    color: current ? "#333399" : "transparent"
                    StripGlyph {x: 6; y: 9; width: 16; height: 16
                                kind: row.modelData.kind === "folder" ? "computer" : "display"}
                    Text {
                        x: 30; y: 3; width: parent.width - 36; elide: Text.ElideRight
                        font.pixelSize: 12; textFormat: Text.PlainText; color: row.current ? "white" : "black"
                        text: row.modelData.name + (row.modelData.size ? "  —  " + importer.sizeText(row.modelData.size) : "")
                    }
                    Text {
                        x: 30; y: 18; width: parent.width - 36; elide: Text.ElideMiddle
                        font.pixelSize: 10; textFormat: Text.PlainText; color: row.current ? "#ddd" : "#555"
                        text: row.modelData.path
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { importer.chosen = row.modelData.path; pathField.text = row.modelData.path }
                        onDoubleClicked: { importer.chosen = row.modelData.path; pathField.text = row.modelData.path; importer.start() }
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 24; wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter; font.pixelSize: 11; color: "#555"
                    visible: sources.count === 0 && importer.service && !importer.service.finding
                    text: "None found in your home folder or on mounted disks. Type the path below."
                }
            }
        }
        Row {
            width: parent.width; spacing: 8
            TextField {
                id: pathField; objectName: "asset-importer-path"
                width: parent.width - findButton.width - 8; height: 28
                font.pixelSize: 12; selectByMouse: true; placeholderText: "Path to a disk image, CD image or System Folder"
                color: "black"; background: FieldBackground {}
                onTextEdited: importer.chosen = text.trim()
                onAccepted: importer.start()
            }
            PlatinumButton {id: findButton; objectName: "asset-importer-find"; text: "Look Again"
                            enabled: !importer.busy; onClicked: importer.service.findSources()}
        }
        PlatinumCheckBox {id: replaceBox; objectName: "asset-importer-replace"; text: "Replace items imported before"}
        // The report: one line per kind of asset.
        Column {
            objectName: "asset-importer-report"
            width: parent.width; spacing: 1
            visible: importer.done || importer.importing
            Text {
                visible: importer.importing; font.pixelSize: 12; textFormat: Text.PlainText
                text: "Importing… A large disk can take a minute."
            }
            Text {
                objectName: "asset-importer-error"
                visible: importer.done && importer.service && importer.service.error !== ""
                width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 12; color: "#900"; textFormat: Text.PlainText
                text: importer.service ? importer.service.error : ""
            }
            Repeater {
                model: importer.done && importer.service ? importer.service.sections : []
                Text {
                    required property var modelData
                    required property int index
                    objectName: "asset-importer-section-" + index
                    width: parent.width; elide: Text.ElideRight; font.pixelSize: 11; textFormat: Text.PlainText
                    readonly property int total: modelData.imported + modelData.kept
                    color: total ? "black" : "#777"
                    text: modelData.name + ": " + (modelData.imported ? modelData.imported + " imported" : "")
                        + (modelData.imported && modelData.kept ? ", " : "")
                        + (modelData.kept ? modelData.kept + " already there" : "")
                        + (!total ? "not on this disk" : "")
                }
            }
            Text {
                visible: importer.done && importer.service && importer.service.error === ""
                width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 11; textFormat: Text.PlainText
                text: "Log out and back in to see and hear everything that was imported."
            }
        }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        spacing: 12
        PlatinumButton {objectName: "asset-importer-close"; text: "Close"; onClicked: importer.close()}
        PlatinumButton {
            objectName: "asset-importer-import"; text: "Import"; defaultAction: true
            enabled: !importer.busy && pathField.text.trim() !== ""
            onClicked: importer.start()
        }
    }
}
