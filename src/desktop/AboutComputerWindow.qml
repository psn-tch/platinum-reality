import QtQuick
import QtQuick.Controls

DesktopWindow {
    id: window
    required property var sessionManager
    objectName: "about-computer-window"
    title: "About This Computer"
    width: 520; height: 330
    property var snapshot: ({})
    property int samples: 0
    function refresh() { snapshot = sessionManager.computerInformation(); samples++ }
    onVisibleChanged: if (visible) refresh()
    Timer { objectName:"about-refresh"; interval:1000; repeat:true; running:window.visible; onTriggered:window.refresh() }
    // Laid out as 9.2.2's About This Computer (reference VM, 2026-11-04): a white banner with the
    // system's name set large, a grey strip of facts in two columns, then the running applications
    // with their memory and a bar each. The banner is original type, not Apple's artwork.
    Rectangle {
        id: banner
        x: -14; y: -11; width: window.width - 2; height: 82; color: "white"
        Text {
            anchors.centerIn: parent
            text: "Platinum Reality"; textFormat: Text.PlainText
            font.family: "serif"; font.pixelSize: 46; color: "black"
        }
    }
    Rectangle {
        id: facts
        x: banner.x; y: banner.y + banner.height; width: banner.width; height: 66; color: "#ddd"
        Rectangle { width: parent.width; height: 1; color: "#888" }
        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: "#888" }
        Grid {
            x: 10; y: 7; columns: 2; columnSpacing: 6; rowSpacing: 1
            Repeater {
                model: ["Version:", (window.snapshot.system || "Unavailable"),
                        "Built-in Memory:", window.snapshot.ram || "Unavailable",
                        "Virtual Memory:", (window.snapshot.swap || "Unavailable")
                            + (window.snapshot.swapUsed ? ", " + window.snapshot.swapUsed + " in use" : ""),
                        "Largest Unused Block:", (window.snapshot.largestUnusedBlock || "Not reported")
                            + (window.snapshot.available ? " (about " + window.snapshot.available + " available)" : "")]
                Text {
                    required property int index
                    required property var modelData
                    objectName: index === 1 ? "about-system" : ""
                    text: String(modelData); textFormat: Text.PlainText
                    font.pixelSize: 11; font.bold: index % 2 === 0
                }
            }
        }
        Column {
            anchors.right: parent.right; anchors.rightMargin: 12; y: 7; spacing: 1
            Text { text: window.snapshot.version || ""; textFormat: Text.PlainText; font.pixelSize: 11; anchors.right: parent.right }
        }
    }
    Rectangle {
        x: banner.x + 8; y: facts.y + facts.height + 6; width: banner.width - 16; height: parent.height - y + 6
        color: "white"
        ListView {
            objectName:"about-applications"
            anchors.fill:parent; anchors.margins:1; clip:true
            model:window.snapshot.applications || []
            ScrollBar.vertical:PlatinumScrollBar {}
            delegate:Item {
                required property var modelData
                width:ListView.view.width-18; height:20
                MenuGlyph {
                    x: 6; anchors.verticalCenter: parent.verticalCenter
                    kind: modelData.name === "Finder" ? "disk" : "application"
                }
                Text {
                    x:28; width:150; anchors.verticalCenter:parent.verticalCenter
                    text:modelData.name; textFormat:Text.PlainText; elide:Text.ElideRight; font.pixelSize:11
                }
                Text {
                    x:180; width:70; anchors.verticalCenter:parent.verticalCenter
                    text:modelData.rssText; textFormat:Text.PlainText; font.pixelSize:11; horizontalAlignment:Text.AlignRight
                }
                // 9.2.2's bar: a dark frame, filled for the part in use.
                Rectangle {
                    x:260; y:4; width:Math.max(12,parent.width-270); height:12
                    color:"white"; border.color:"#222"
                    Rectangle {
                        x:1; y:1; height:parent.height-2
                        width:Math.max(3, (parent.width-2)*Math.max(0,Math.min(1,modelData.fraction || 0)))
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#ccccff" }
                            GradientStop { position: 0.5; color: "#6666cc" }
                            GradientStop { position: 1; color: "#333399" }
                        }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#222" }
                    }
                }
            }
        }
    }
}
