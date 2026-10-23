import QtQuick

// 16-pixel original menu glyphs (Application menu). Plain rectangles stay crisp
// and paint immediately inside menus that are created while hidden.
Item {
    id: glyph
    property string kind: "application"
    width: 16; height: 16
    // Volume: a drive with a light bezel and an activity lamp.
    Rectangle {visible:glyph.kind==="disk";x:1;y:4;width:14;height:9;color:"#ddd";border.color:"#222"}
    Rectangle {visible:glyph.kind==="disk";x:2;y:10;width:12;height:2;color:"#999"}
    Rectangle {visible:glyph.kind==="disk";x:11;y:10;width:2;height:1;color:"#2a2"}
    // Application: a small window with a title band.
    Rectangle {visible:glyph.kind!=="disk";x:1;y:2;width:14;height:12;color:"#eee";border.color:"#222"}
    Rectangle {visible:glyph.kind!=="disk";x:2;y:3;width:12;height:3;color:"#999"}
    Rectangle {visible:glyph.kind!=="disk";x:3;y:8;width:10;height:1;color:"#777"}
    Rectangle {visible:glyph.kind!=="disk";x:3;y:10;width:7;height:1;color:"#777"}
}
