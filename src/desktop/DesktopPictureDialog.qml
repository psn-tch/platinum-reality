import QtQuick
import QtQuick.Controls

ShellDialog {
    id: panel
    required property var settings
    objectName: "desktop-picture-dialog"
    title: "Desktop Picture"
    width: Math.min(660, parent.width - 32)
    height: Math.min(610, parent.height - 54)
    standardButtons: Dialog.Ok | Dialog.Cancel
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
    property var pendingPicture: null
    readonly property url preview: settings.previewDesktopPicture(draftPath)
    acceptEnabled: draftPath === "" || preview.toString() !== ""

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
    function edit() {
        if (visible || sessionManager.shellModal) return
        pendingPicture = null
        draftPath = settings.wallpaperPath
        draftLayout = settings.wallpaperLayout
        folder.text = settings.picturesFolder
        collections = settings.pictureCollections(personalPictures)
        readFolder()
        open()
    }
    onOpened: pathField.forceActiveFocus()
    onAccepted: pendingPicture = {path: draftPath, layout: draftLayout}
    Connections {
        target: panel
        function onClosed() {
            let choice = panel.pendingPicture
            panel.pendingPicture = null
            panel.releaseInput()
            if (choice) panel.settings.applyDesktopPicture(choice.path, choice.layout)
        }
    }
    contentItem: Column {
        spacing: 10
        Row {
            width: parent.width; spacing: 8
            TextField {
                id: folder; objectName: "picture-folder"
                width: parent.width - readButton.width - 8; height: 28
                font.pixelSize: 12; selectByMouse: true; placeholderText: "Picture folder"
                color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                background: Rectangle {color: "white"; border.color: "#555"}
                // Return here shows the folder rather than pressing OK.
                property bool ownsReturn: true
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
            width: parent.width; height: Math.max(120, panel.height - 280); spacing: 12
            Rectangle {
                width: Math.round((parent.width - 12) * 0.43); height: parent.height
                color: "white"; border.color: "#555"
                ListView {
                    id: pictures; objectName: "picture-list"
                    anchors.fill: parent; anchors.margins: 1; clip: true
                    model: panel.pictureRows; currentIndex: -1; keyNavigationEnabled: false
                    ScrollBar.vertical: ScrollBar {}
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
            background: Rectangle {color: "white"; border.color: "#555"}
            onAccepted: {if(panel.acceptEnabled)panel.accept()}
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
        Text {
            width: parent.width; height: 32
            text: panel.notice; textFormat: Text.PlainText; font.pixelSize: 12
            color: "#333"; wrapMode: Text.Wrap
        }
    }
}
