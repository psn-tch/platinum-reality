import QtQuick
import QtQuick.Controls

DesktopWindow {
    id: window
    required property var sessionManager
    objectName: "about-computer-window"
    title: "About This Computer"
    width: 560; height: 392
    // As 9.2.2's window: it rolls up, zooms to show every application, and resizes.
    collapsible: true; zoomable: true; resizable: true
    minimumSize: Qt.size(470, 260)
    // Zoomed: tall enough for every application, and never less than the standard size.
    zoomedSize: Qt.size(Math.max(560, width), Math.max(392, Math.min(780, list.y + 32 + 4 + rows.length * 20 + 6 + 35)))
    property var snapshot: ({})
    property int samples: 0
    readonly property var rows: snapshot.applications || []
    function refresh() { snapshot = sessionManager.computerInformation(); samples++ }
    onVisibleChanged: if (visible) refresh()
    Timer { objectName:"about-refresh"; interval:1000; repeat:true; running:window.visible && !window.collapsed; onTriggered:window.refresh() }

    // The Platinum Reality mark (packaging/platinum-reality.svg, an original drawing), at any size.
    component Mark: Item {
        id: mark
        property real size: 64
        width: size; height: size
        readonly property real u: size / 64
        Rectangle { x: 3 * mark.u; y: 5 * mark.u; width: 58 * mark.u; height: 51 * mark.u; color: "#ccc"; border.color: "#222"; border.width: Math.max(1, 2 * mark.u) }
        Rectangle { x: 5 * mark.u; y: 7 * mark.u; width: 54 * mark.u; height: Math.max(1, mark.u); color: "white" }
        Rectangle { x: 5 * mark.u; y: 7 * mark.u; width: Math.max(1, mark.u); height: 47 * mark.u; color: "white" }
        Repeater {
            model: 3
            Rectangle { required property int index; x: 18 * mark.u; y: (11 + 4 * index) * mark.u; width: 39 * mark.u; height: Math.max(1, 0.5 * mark.u); color: "#777" }
        }
        Rectangle { x: 8 * mark.u; y: 10 * mark.u; width: 8 * mark.u; height: 8 * mark.u; color: "#ddd"; border.color: "#333" }
        Rectangle { x: 8 * mark.u; y: 24 * mark.u; width: 48 * mark.u; height: 26 * mark.u; color: "white"; border.color: "#777" }
        Text {
            x: 8 * mark.u; y: 24 * mark.u; width: 48 * mark.u; height: 26 * mark.u
            text: "P"; font.pixelSize: 24 * mark.u; font.bold: true; color: "#444"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }

    // Laid out as Mac OS 9.2's About This Computer (psn_tch's reference): a white banner with the
    // mark and the system's name set large, a grey strip of facts in two columns, then the running
    // applications, each with its memory and a bar, the system's own row last. Original drawings.
    Rectangle {
        id: banner
        x: -12; y: -11; width: window.width - 6; height: 96
        color: "white"; border.color: "#444"
        Row {
            anchors.centerIn: parent; spacing: 16
            // The Mac OS face from the user's own 9.2.2 Finder, when they imported it (scripts/
            // import-macos9-assets.py, never shipped); otherwise the original mark.
            Image {
                id: importedLogo
                objectName: "about-imported-logo"
                anchors.verticalCenter: parent.verticalCenter
                visible: status === Image.Ready
                source: window.sessionManager.personalAssets ? "file://" + window.sessionManager.personalAssets + "/icons/about-logo.png" : ""
                smooth: false; cache: false
            }
            Mark { visible: !importedLogo.visible; size: 68; anchors.verticalCenter: parent.verticalCenter }
            Text {
                objectName: "about-banner"
                anchors.verticalCenter: parent.verticalCenter
                text: "Platinum Reality"; textFormat: Text.PlainText
                font.family: "serif"; font.pixelSize: 50; color: "black"
            }
        }
    }
    Item {
        id: facts
        x: banner.x; y: banner.y + banner.height; width: banner.width; height: 72
        Grid {
            x: 4; y: 8; columns: 2; columnSpacing: 8; rowSpacing: 2
            Repeater {
                model: ["Version:", window.snapshot.system || "Unavailable",
                        "Built-in Memory:", window.snapshot.ram || "Unavailable",
                        "Virtual Memory:", window.snapshot.swap === "Off" ? "Off"
                            : (window.snapshot.swap || "Unavailable") + (window.snapshot.swapUsed ? ", " + window.snapshot.swapUsed + " used" : ""),
                        // Linux keeps no largest unused block; the memory available is the nearest fact.
                        "Largest Unused Block:", window.snapshot.available || "Not reported"]
                Text {
                    required property int index
                    required property var modelData
                    objectName: index === 1 ? "about-system" : ""
                    text: String(modelData); textFormat: Text.PlainText
                    font.pixelSize: 11; font.bold: index % 2 === 0
                }
            }
        }
        Text {
            objectName: "about-kernel"
            x: parent.width / 2 + 20; y: 8
            text: window.snapshot.version ? "Linux " + window.snapshot.version : ""
            textFormat: Text.PlainText; font.pixelSize: 11
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 6; y: 8 + 3 * 15
            text: "An original desktop by psn_tch, 2026"; textFormat: Text.PlainText; font.pixelSize: 11
        }
    }
    // The applications' list: white, edge to edge, with a scroll bar above the size box.
    Rectangle {
        id: list
        x: banner.x; y: facts.y + facts.height; width: banner.width; height: window.height - 32 - y - 4
        color: "white"; border.color: "#444"
        ListView {
            id: applications
            objectName: "about-applications"
            x: 1; y: 1; width: parent.width - 2 - 16; height: parent.height - 2; clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: window.rows
            ScrollBar.vertical: PlatinumScrollBar {
                parent: list
                x: list.width - 17; y: 1; height: list.height - 2 - 15
            }
            delegate: Item {
                required property var modelData
                required property int index
                objectName: "about-row-" + index
                width: ListView.view.width; height: 20
                Rectangle { anchors.fill: parent; color: index % 2 ? "#f4f4f4" : "white" }
                Mark { visible: modelData.system === true; size: 16; x: 12; anchors.verticalCenter: parent.verticalCenter }
                MenuGlyph {
                    visible: modelData.system !== true
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    kind: "application"
                }
                Text {
                    x: 36; width: 170; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 11
                }
                Text {
                    x: 206; width: 64; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.rssText; textFormat: Text.PlainText; font.pixelSize: 11; horizontalAlignment: Text.AlignRight
                }
                // 9.2's bar: a dark-framed groove, the part in use a raised blue bar.
                Rectangle {
                    x: 280; y: 5; width: Math.max(16, parent.width - 292); height: 11
                    color: "#ddd"; border.color: "#333"
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "#999" }
                    Rectangle {
                        x: 1; y: 1; height: parent.height - 2
                        width: Math.max(4, (parent.width - 2) * Math.max(0, Math.min(1, modelData.fraction || 0)))
                        visible: modelData.known === true
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#aab4ff" }
                            GradientStop { position: 0.35; color: "#4a5ee8" }
                            GradientStop { position: 1; color: "#1c2a99" }
                        }
                        Rectangle { width: parent.width; height: 1; color: "#d8ddff" }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#10186a" }
                    }
                }
            }
        }
    }
}
