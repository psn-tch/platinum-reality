import QtQuick
import QtQuick.Controls

ShellDialog {
    id: browser
    objectName: "application-browser"
    required property var catalog
    property string documentPath: ""
    property var pendingLaunch: null
    readonly property var selected: {let count = catalog.count; return catalog.item(applicationList.currentIndex)}
    width: Math.min(620, parent.width - 32)
    height: Math.min(510, parent.height - 54)
    title: documentPath === "" ? "Applications" : "Open With"
    standardButtons: Dialog.Open | Dialog.Cancel
    acceptEnabled: Boolean(selected.launchable) && !catalog.busy
    function browse(document) {
        if (visible || sessionManager.shellModal) return
        documentPath = document || ""
        pendingLaunch = null
        catalog.document = documentPath
        catalog.query = ""
        catalog.showAll = false
        rememberChoice.checked = false
        search.text = ""
        applicationList.currentIndex = 0
        open()
        if (!catalog.ready) catalog.refresh()
    }
    function acceptSelection() { if (acceptEnabled) accept() }
    onOpened: search.forceActiveFocus()
    onAccepted: pendingLaunch = {application: selected.id, document: documentPath, remember: rememberChoice.checked}
    Connections {
        target: browser
        function onClosed() {
            let choice = browser.pendingLaunch
            browser.pendingLaunch = null
            // Idempotent explicit release makes ordering independent of the
            // inherited handler and finishes this launch before the next picker.
            browser.releaseInput()
            if (choice) browser.sessionManager.launchApplication(choice.application, choice.document, choice.remember)
        }
    }
    Connections {target: catalog; function onCountChanged() {applicationList.currentIndex = catalog.count > 0 ? 0 : -1}}
    contentItem: Column {
        spacing: 10
        Text {
            width: parent.width; visible: browser.documentPath !== ""
            text: "Choose an application for “" + browser.documentPath.split("/").pop() + "”."
            textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 12
        }
        Row {
            width: parent.width; spacing: 10
            TextField {
                id: search; objectName: "application-search"
                width: parent.width - refreshButton.width - 10; height: 28
                placeholderText: "Find an application"; font.pixelSize: 12; selectByMouse: true
                color: "black"; selectionColor: "#202050"; selectedTextColor: "white"
                background: Rectangle {color:"white"; border.color:"#555"}
                onTextChanged: catalog.query = text
                onAccepted: browser.acceptSelection()
                Keys.onDownPressed: {applicationList.forceActiveFocus();applicationList.currentIndex = Math.max(0, applicationList.currentIndex)}
                Keys.onPressed: function(event) {if(event.key===Qt.Key_A && (event.modifiers & Qt.MetaModifier)){selectAll();event.accepted=true}}
            }
            PlatinumButton {id:refreshButton; objectName:"application-refresh"; text:"Refresh"; enabled:!catalog.busy; onClicked:catalog.refresh()}
        }
        Rectangle {
            width: parent.width; height: browser.height - (browser.documentPath === "" ? 206 : 292)
            color: "white"; border.color: "#555"
            ListView {
                id: applicationList; objectName: "application-list"
                anchors.fill:parent; anchors.margins:1; clip:true
                model: browser.catalog; currentIndex:0; keyNavigationWraps:true
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string applicationId
                    required property string displayName
                    required property string description
                    required property bool launchable
                    width: applicationList.width; height:52
                    color: ListView.isCurrentItem ? "#202050" : "white"
                    DesktopIcon {x:9;y:8;kind:"application";selected:row.ListView.isCurrentItem;personalFolder:typeof manager!=="undefined" && manager?manager.personalAssets:""}
                    Column {
                        x:54;y:8;width:parent.width - 68; spacing:3
                        Text {width:parent.width;text:row.displayName;textFormat:Text.PlainText;font.pixelSize:12;font.bold:false;elide:Text.ElideRight;color:row.ListView.isCurrentItem?"white":row.launchable?"#111":"#888"}
                        Text {width:parent.width;text:row.description;textFormat:Text.PlainText;font.pixelSize:12;elide:Text.ElideRight;color:row.ListView.isCurrentItem?"#ddd":"#555"}
                    }
                    MouseArea {anchors.fill:parent;onClicked:applicationList.currentIndex=row.index;onDoubleClicked:{applicationList.currentIndex=row.index;browser.acceptSelection()}}
                }
                Keys.onReturnPressed: browser.acceptSelection()
                Keys.onEnterPressed: browser.acceptSelection()
                Text {anchors.centerIn:parent;visible:catalog.count===0;text:catalog.busy?"Reading applications…":"No matching applications";font.pixelSize:12;color:"#555"}
            }
        }
        Text {
            width:parent.width; height:32
            text: catalog.busy ? "Reading applications…" : browser.selected.reason || catalog.notice || catalog.count + " applications"
            textFormat:Text.PlainText;font.pixelSize:12;wrapMode:Text.Wrap;color:"#333"
        }
        PlatinumCheckBox {visible:browser.documentPath!=="";text:"Show all applications that accept documents";checked:catalog.showAll;onToggled:catalog.showAll=checked}
        PlatinumCheckBox {id:rememberChoice;objectName:"application-remember";visible:browser.documentPath!=="";text:"Always use for this kind of document"}
    }
}
