import QtQuick
import QtQuick.Controls

ShellDialog {
    id: panel
    required property var tool
    objectName: "screenshot-dialog"
    title: "Screenshot"
    width: Math.min(700, parent.width-32)
    height: Math.min(550, parent.height-54)
    function edit() {
        if (visible || sessionManager.shellModal) return
        filename.text = tool.suggestedPath
        open()
    }
    function savePicture() {if (tool.save(filename.text)) accept()}
    Connections {target:panel;function onClosed(){panel.releaseInput();panel.tool.discard()}}
    contentItem: Column {
        spacing: 10
        Text {text:"Drag on the preview to select an area, or keep the entire desktop.";font.pixelSize:12}
        Rectangle {
            id: frame; width: parent.width; height: panel.height-224
            color:"#888";border.color:"#333";clip:true
            Image {
                id: preview; objectName:"screenshot-preview"
                anchors.fill:parent; anchors.margins:1
                source:panel.tool.preview;cache:false;asynchronous:false
                fillMode:Image.PreserveAspectFit
                sourceSize:Qt.size(1000,700)
            }
            MouseArea {
                id: selection; objectName:"screenshot-selection"
                anchors.fill:preview
                property point start: Qt.point(0,0)
                property point end: Qt.point(0,0)
                readonly property real leftOffset: (width-preview.paintedWidth)/2
                readonly property real topOffset: (height-preview.paintedHeight)/2
                readonly property real ratio: panel.tool.imageSize.width>0 ? preview.paintedWidth/panel.tool.imageSize.width : 1
                function imagePoint(mouse) {
                    return Qt.point(Math.max(0,Math.min(panel.tool.imageSize.width,Math.round((mouse.x-leftOffset)/ratio))),
                                    Math.max(0,Math.min(panel.tool.imageSize.height,Math.round((mouse.y-topOffset)/ratio))))
                }
                onPressed:function(mouse) {
                    if(mouse.x<leftOffset || mouse.x>leftOffset+preview.paintedWidth || mouse.y<topOffset || mouse.y>topOffset+preview.paintedHeight){mouse.accepted=false;return}
                    start=imagePoint(mouse);end=start
                }
                onPositionChanged:function(mouse){if(pressed)end=imagePoint(mouse)}
                onReleased:function(mouse) {
                    end=imagePoint(mouse)
                    panel.tool.select(Math.min(start.x,end.x),Math.min(start.y,end.y),Math.abs(end.x-start.x),Math.abs(end.y-start.y))
                }
                Rectangle {
                    x:selection.leftOffset+(selection.pressed?Math.min(selection.start.x,selection.end.x):panel.tool.selection.x)*selection.ratio
                    y:selection.topOffset+(selection.pressed?Math.min(selection.start.y,selection.end.y):panel.tool.selection.y)*selection.ratio
                    width:(selection.pressed?Math.abs(selection.end.x-selection.start.x):panel.tool.selection.width)*selection.ratio
                    height:(selection.pressed?Math.abs(selection.end.y-selection.start.y):panel.tool.selection.height)*selection.ratio
                    color:"transparent";border.color:"black";border.width:1
                    Rectangle {anchors.fill:parent;anchors.margins:1;color:"transparent";border.color:"white"}
                }
            }
        }
        Row {
            spacing:12
            PlatinumButton {objectName:"screenshot-entire";text:"Entire Desktop";onClicked:panel.tool.select(0,0,panel.tool.imageSize.width,panel.tool.imageSize.height)}
            Text {anchors.verticalCenter:parent.verticalCenter;text:panel.tool.selection.width+" × "+panel.tool.selection.height+" pixels";font.pixelSize:12}
        }
        TextField {
            id:filename; objectName:"screenshot-path";width:parent.width;height:28
            font.pixelSize:12;selectByMouse:true;color:"black";selectionColor:"#202050";selectedTextColor:"white"
            background:Rectangle{color:"white";border.color:"#555"}
            onAccepted:panel.savePicture()
            Keys.onPressed:function(event){if(event.key===Qt.Key_A && (event.modifiers & Qt.MetaModifier)){selectAll();event.accepted=true}}
        }
        Text {width:parent.width;height:32;text:panel.tool.error;textFormat:Text.PlainText;font.pixelSize:12;wrapMode:Text.Wrap;color:"#800000"}
    }
    footer: Item {
        implicitHeight:60
        Row {
            anchors.right:parent.right;anchors.rightMargin:16;anchors.verticalCenter:parent.verticalCenter;spacing:12
            PlatinumButton {objectName:"screenshot-cancel";text:"Cancel";onClicked:panel.reject()}
            PlatinumButton {objectName:"screenshot-save";text:"Save";defaultAction:true;onClicked:panel.savePicture()}
        }
    }
}
