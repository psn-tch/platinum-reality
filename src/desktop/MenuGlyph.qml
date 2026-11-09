import QtQuick

// 16px application images, with original geometric fallback glyphs.
// Plain rectangles paint immediately even inside menus created while hidden.
Item {
    id: glyph
    property string kind: "application"
    property url source: ""
    readonly property bool useFallback: applicationImage.status !== Image.Ready
    width: 16; height: 16
    Image {
        id: applicationImage; objectName:"application-icon-image"
        anchors.fill:parent;source:glyph.source;sourceSize:Qt.size(16,16)
        fillMode:Image.PreserveAspectFit;smooth:false
    }
    Item {
        anchors.fill:parent;visible:glyph.useFallback
        // Volume: a drive with a light bezel and an activity lamp.
        Rectangle {visible:glyph.kind==="disk";x:1;y:4;width:14;height:9;color:"#ddd";border.color:"#222"}
        Rectangle {visible:glyph.kind==="disk";x:2;y:10;width:12;height:2;color:"#999"}
        Rectangle {visible:glyph.kind==="disk";x:11;y:10;width:2;height:1;color:"#2a2"}
        // Folder: a tab and a body in folder violet-grey.
        Rectangle {visible:glyph.kind==="folder";x:1;y:3;width:6;height:3;color:"#b8b8e0";border.color:"#222"}
        Rectangle {visible:glyph.kind==="folder";x:1;y:5;width:14;height:9;color:"#ccccff";border.color:"#222"}
        // Control panel: a panel with two slider tracks and their knobs.
        Rectangle {visible:glyph.kind==="control-panel";x:1;y:2;width:14;height:12;color:"#ddd";border.color:"#222"}
        Rectangle {visible:glyph.kind==="control-panel";x:3;y:5;width:10;height:1;color:"#555"}
        Rectangle {visible:glyph.kind==="control-panel";x:3;y:10;width:10;height:1;color:"#555"}
        Rectangle {visible:glyph.kind==="control-panel";x:5;y:3;width:3;height:5;color:"#99c";border.color:"#222"}
        Rectangle {visible:glyph.kind==="control-panel";x:9;y:8;width:3;height:5;color:"#99c";border.color:"#222"}
        // Application: a small window with a title band.
        readonly property bool window:!["disk","folder","control-panel"].includes(glyph.kind)
        Rectangle {visible:parent.window;x:1;y:2;width:14;height:12;color:"#eee";border.color:"#222"}
        Rectangle {visible:parent.window;x:2;y:3;width:12;height:3;color:"#999"}
        Rectangle {visible:parent.window;x:3;y:8;width:10;height:1;color:"#777"}
        Rectangle {visible:parent.window;x:3;y:10;width:7;height:1;color:"#777"}
    }
}
