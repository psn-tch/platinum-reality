import QtQuick
import QtQuick.Controls

// Desktop Picture, a panel of the Control Panels window. Choosing a picture previews it here;
// Set Desktop (or Return in the file field) puts it on the desktop.
Item {
    id: panel
    required property var settings
    objectName: "desktop-picture-dialog"
    readonly property string panelId: "desktop-picture"
    readonly property string title: "Desktop Picture"
    readonly property string glyph: "picture"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 560
    implicitHeight: body.implicitHeight
    property string draftPath: ""
    property string draftLayout: "fill"
    property var pictureRows: []
    // The user's own collections (e.g. Mac OS 9's Desktop Pictures, imported by
    // scripts/import-macos9-assets.py), beside the Pictures folder.
    property var collections: []
    readonly property string personalPictures: typeof manager !== "undefined" && manager
                                               ? manager.personalAssets + "/desktop-pictures" : ""
    function showFolder(path) { folder.text = path; readFolder() }
    property string notice: ""
    readonly property url preview: settings.previewDesktopPicture(draftPath)
    readonly property bool canSet: draftPath === "" || preview.toString() !== ""
    readonly property bool changed: draftPath !== settings.wallpaperPath || draftLayout !== settings.wallpaperLayout

    function readFolder() {
        let result = settings.listDesktopPictures(folder.text)
        pictureRows = result.files
        pictures.currentIndex = -1
        notice = result.notice
        if (result.folder) folder.text = result.folder
    }
    function selectPicture(index) {
        if (index < 0 || index >= pictureRows.length) return
        pictures.currentIndex = index
        draftPath = pictureRows[index].path
        pictures.positionViewAtIndex(index, ListView.Contain)
    }
    function edit() { if (host) host.show(panelId) }
    function setDesktop() { if (canSet) settings.applyDesktopPicture(draftPath, draftLayout) }
    // Each time the panel comes on screen it starts from the desktop as it is.
    onShownChanged: if (shown) {
        draftPath = settings.wallpaperPath
        draftLayout = settings.wallpaperLayout
        folder.text = settings.picturesFolder
        collections = settings.pictureCollections(personalPictures)
        readFolder()
        pathField.forceActiveFocus()
    }
    Column {
        id: body
        width: parent.width
        spacing: 10
        Row {
            width: parent.width; spacing: 8
            TextField {
                id: folder; objectName: "picture-folder"
                width: parent.width - readButton.width - 8; height: 28
                font.pixelSize: 12; selectByMouse: true; placeholderText: "Picture folder"
                color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                background: FieldBackground {}
                // Return here shows the folder.
                onAccepted: panel.readFolder()
                Keys.onPressed: function(event) {if(event.key===Qt.Key_A && (event.modifiers & Qt.MetaModifier)){selectAll();event.accepted=true}}
            }
            PlatinumButton {id: readButton; objectName: "picture-read-folder"; text: "Show"; onClicked: panel.readFolder()}
        }
        Flow {
            objectName: "picture-collections"
            visible: panel.collections.length > 0
            width: parent.width; spacing: 6
            PlatinumButton {objectName: "picture-collection-pictures"; text: "Pictures"; implicitHeight: 24
                            onClicked: panel.showFolder(panel.settings.picturesFolder)}
            Repeater {
                model: panel.collections
                PlatinumButton {
                    required property var modelData
                    objectName: "picture-collection-" + modelData.name
                    text: modelData.name; implicitHeight: 24
                    onClicked: panel.showFolder(modelData.path)
                }
            }
        }
        Row {
            width: parent.width; height: 250; spacing: 12
            Rectangle {
                width: Math.round((parent.width - 12) * 0.43); height: parent.height
                color: "white"; border.color: "#555"
                ListView {
                    id: pictures; objectName: "picture-list"
                    anchors.fill: parent; anchors.margins: 1; clip: true
                    model: panel.pictureRows; currentIndex: -1; keyNavigationEnabled: false
                    ScrollBar.vertical: PlatinumScrollBar {}
                    Keys.onDownPressed: panel.selectPicture(Math.min(panel.pictureRows.length-1, currentIndex+1))
                    Keys.onUpPressed: panel.selectPicture(Math.max(0, currentIndex-1))
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property var modelData
                        objectName: "picture-row-" + index
                        width: pictures.width; height: 38
                        color: ListView.isCurrentItem ? "#202050" : "white"
                        Text {
                            x: 10; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 20
                            text: row.modelData.name; textFormat: Text.PlainText
                            font.pixelSize: 12; elide: Text.ElideMiddle
                            color: row.ListView.isCurrentItem ? "white" : "#111"
                        }
                        MouseArea {anchors.fill: parent; onClicked: {panel.selectPicture(row.index);pictures.forceActiveFocus()}}
                    }
                }
            }
            Rectangle {
                width: parent.width - parent.spacing - Math.round((parent.width - 12) * 0.43)
                height: parent.height; color: "#708b97"; border.color: "#555"; clip: true
                Image {
                    id: sample; objectName: "picture-preview"
                    anchors.fill: parent; anchors.margins: 1
                    source: panel.preview; asynchronous: true
                    sourceSize: Qt.size(640, 480)
                    fillMode: panel.draftLayout === "fit" ? Image.PreserveAspectFit
                              : panel.draftLayout === "center" ? Image.Pad
                              : panel.draftLayout === "tile" ? Image.Tile : Image.PreserveAspectCrop
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 24
                    visible: panel.preview.toString() === "" || sample.status === Image.Error
                    text: panel.draftPath === "" ? "Solid desktop color" : "Choose a PNG, JPEG or WebP picture."
                    textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 12; color: "#111"
                }
            }
        }
        TextField {
            id: pathField; objectName: "picture-path"
            width: parent.width; height: 28
            text: panel.draftPath; onTextEdited: panel.draftPath = text
            font.pixelSize: 12; selectByMouse: true; placeholderText: "Picture file (full path)"
            color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
            background: FieldBackground {}
            onAccepted: panel.setDesktop()
            Keys.onPressed: function(event) {if(event.key===Qt.Key_A && (event.modifiers & Qt.MetaModifier)){selectAll();event.accepted=true}}
        }
        Row {
            spacing: 8
            Repeater {
                model: [{label:"Fill",value:"fill"},{label:"Fit",value:"fit"},{label:"Center",value:"center"},{label:"Tile",value:"tile"}]
                PlatinumButton {
                    required property var modelData
                    objectName: "picture-layout-" + modelData.value
                    text: modelData.label; implicitWidth: 64
                    defaultAction: panel.draftLayout === modelData.value
                    onClicked: panel.draftLayout = modelData.value
                }
            }
            PlatinumButton {objectName:"picture-solid";text:"Solid Color";implicitWidth:90;onClicked:panel.draftPath=""}
            PlatinumButton {objectName:"picture-default";text:"Default";implicitWidth:74;onClicked:panel.draftPath=panel.settings.defaultWallpaperPath}
        }
        PlatinumButton {
            objectName: "picture-set"; text: "Set Desktop"; implicitWidth: 110
            defaultAction: panel.changed; enabled: panel.canSet && panel.changed
            onClicked: panel.setDesktop()
        }
        Text {
            width: parent.width; height: 32
            text: panel.notice; textFormat: Text.PlainText; font.pixelSize: 12
            color: "#333"; wrapMode: Text.Wrap
        }
    }
}
