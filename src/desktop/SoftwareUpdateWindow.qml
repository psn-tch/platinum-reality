import QtQuick
import QtQuick.Controls

// Software Update (docs/software-update.md), a control panel laid out as 9.2.2's (measured in the
// reference VM, docs/fidelity/control-strip.md's sibling docs/software-update.md): a short
// explanation, a large Update Now button, and "Update software automatically" heading a group.
// Update Now asks
// GitHub for a newer Platinum Reality, shows its version, date and notes, and installs it only
// when asked (pacman, through an administrator's password). Install from Local Package takes a
// release folder from a disk instead. Restore Previous Version undoes the last update.
DesktopWindow {
    id: panel
    objectName: "software-update-window"
    title: "Software Update"
    width: 372
    // 9.2.2's window is 372 x 322; what was found opens below the group, as its own results window did.
    height: details ? 574 : 322
    readonly property bool details: choosingLocal || (service !== null && service !== undefined
        && (service.phase !== "" && service.phase !== "idle" || service.checked || service.error !== ""
            || service.available.version !== undefined || service.readyRelease.version !== undefined
            || service.restartNeeded || service.message !== ""))
    required property var service
    property bool choosingLocal: false
    readonly property bool busy: service ? service.busy : false
    function dateText(text) {
        const d = new Date(text)
        return isNaN(d.getTime()) ? "" : d.toLocaleDateString(Qt.locale(), "d MMMM yyyy")
    }
    onVisibleChanged: if (visible && service) service.refresh()
    Text {
        x: 22; y: 16; width: 300; wrapMode: Text.WordWrap; font.pixelSize: 12; textFormat: Text.PlainText
        text: "Software Update checks for new versions of Platinum Reality on the Internet, or on a disk you choose."
    }
    Text {
        objectName: "software-update-installed"
        x: 22; y: 62; width: 328; elide: Text.ElideRight; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
        text: panel.service && panel.service.installed
              ? "Installed: Platinum Reality " + panel.service.installed
              : "Platinum Reality is not installed as a package on this computer."
    }
    // Update Now: 9.2.2's 52 px square button with its picture, the bold label under it.
    Rectangle {
        id: updateNow
        objectName: "software-update-check"
        x: 160; y: 85; width: 52; height: 50
        readonly property bool down: nowMouse.pressed && nowMouse.containsMouse
        enabled: !panel.busy
        color: down ? "#999" : "#ccc"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 3; height: 1; color: updateNow.down ? "#666" : "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 3; color: updateNow.down ? "#666" : "white"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: updateNow.down ? "white" : "#777"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: updateNow.down ? "white" : "#777"}
        StripGlyph {anchors.centerIn: parent; width: 32; height: 32; kind: "computer"; opacity: updateNow.enabled ? 1 : 0.4}
        MouseArea {id: nowMouse; anchors.fill: parent; enabled: updateNow.enabled
                   onClicked: { panel.choosingLocal = false; panel.service.check() }}
    }
    Text {
        anchors.horizontalCenter: updateNow.horizontalCenter; y: 140
        font.pixelSize: 12; font.bold: true; text: "Update Now"; color: updateNow.enabled ? "black" : "#888"
    }
    // The group 9.2.2 heads with its "Update software automatically" box.
    Item {
        x: 20; y: 193; width: 328; height: 84
        Rectangle {x: 0; y: 8; width: parent.width - 1; height: parent.height - 9; color: "transparent"; border.color: "#888"}
        Rectangle {x: 1; y: 9; width: parent.width - 1; height: parent.height - 9; color: "transparent"; border.color: "white"; z: -1}
        Rectangle {
            x: 8; y: 0; width: automatic.implicitWidth + 6; height: 18; color: "#ddd"
            PlatinumCheckBox {
                id: automatic
                objectName: "software-update-automatic"
                x: 2; text: "Update software automatically"
                checked: panel.service ? panel.service.automatic : false
                onToggled: panel.service.automatic = checked
            }
        }
        // Where 9.2.2 shows its schedule: ours checks once a week and only tells you.
        Rectangle {
            x: 112; y: 28; width: 200; height: 46; color: "#eee"; border.color: "#888"
            Text {
                x: 6; y: 4; width: parent.width - 12; wrapMode: Text.WordWrap; font.pixelSize: 11; textFormat: Text.PlainText
                color: automatic.checked ? "black" : "#888"
                text: "Once a week. It only tells you; nothing installs without your click."
            }
        }
        PlatinumButton {
            objectName: "software-update-local"; x: 12; y: 30; text: "From Disk…"
            enabled: !panel.busy; onClicked: { panel.choosingLocal = true; panel.service.findBundles() }
        }
    }
    Column {
        x: 20; y: 292; width: 328; spacing: 8
        visible: panel.details
        // What was found, or the local release being looked at.
        Rectangle {
            width: parent.width; height: 170; color: "white"; border.color: "#555"
            Column {
                x: 10; y: 8; width: parent.width - 20; spacing: 6
                visible: !panel.choosingLocal
                Text {
                    objectName: "software-update-result"
                    width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 12; textFormat: Text.PlainText
                    readonly property var found: panel.service ? panel.service.available : ({})
                    readonly property var local: panel.service ? panel.service.readyRelease : ({})
                    font.bold: found.version !== undefined || local.version !== undefined || restart
                    readonly property bool restart: panel.service ? panel.service.restartNeeded : false
                    text: restart && panel.service.phase === "" ? panel.service.message
                        : panel.service && panel.service.phase === "checking" ? "Checking for updates…"
                        : local.version !== undefined ? "Platinum Reality " + local.version + " is ready to install."
                        : found.version !== undefined ? "Platinum Reality " + found.version + " is available"
                              + (found.date ? " (" + panel.dateText(found.date) + ")" : "") + "."
                        : panel.service && panel.service.checked ? "Your Platinum Reality is up to date."
                        : "Click Update Now to look for a newer Platinum Reality."
                }
                ScrollView {
                    id: notesView
                    width: parent.width; height: 120; clip: true
                    visible: notes.text !== ""
                    Text {
                        id: notes; objectName: "software-update-notes"
                        width: notesView.availableWidth; wrapMode: Text.WordWrap; font.pixelSize: 11; textFormat: Text.PlainText
                        text: panel.service ? (panel.service.readyRelease.notes || panel.service.available.notes || "") : ""
                    }
                }
            }
            // Install from Local Package: release folders found on disks, or a typed path.
            Column {
                x: 8; y: 8; width: parent.width - 16; spacing: 6
                visible: panel.choosingLocal
                Text {font.pixelSize: 12; textFormat: Text.PlainText
                      text: panel.service && panel.service.phase === "finding" ? "Looking on your disks…"
                                                                              : "Release folders found:"}
                ListView {
                    id: bundles; objectName: "software-update-bundles"
                    width: parent.width; height: 100; clip: true
                    model: panel.service ? panel.service.bundles : []
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "software-update-bundle-" + index
                        width: ListView.view.width; height: 20
                        color: localPath.text === modelData.path ? "#333399" : "transparent"
                        Text {
                            x: 4; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 8; elide: Text.ElideMiddle
                            font.pixelSize: 11; textFormat: Text.PlainText
                            color: localPath.text === row.modelData.path ? "white" : "black"
                            text: row.modelData.version + "  —  " + row.modelData.path
                        }
                        MouseArea {anchors.fill: parent; onClicked: localPath.text = row.modelData.path}
                    }
                    Text {
                        anchors.centerIn: parent; font.pixelSize: 11; color: "#555"
                        visible: bundles.count === 0 && panel.service && panel.service.phase !== "finding"
                        text: "None found. Type the folder's path below."
                    }
                }
                Row {
                    width: parent.width; spacing: 8
                    TextField {
                        id: localPath; objectName: "software-update-path"
                        width: parent.width - useButton.width - 8; height: 28
                        font.pixelSize: 12; selectByMouse: true; placeholderText: "Folder holding release.json"
                        color: "black"; background: FieldBackground {}
                        onAccepted: { panel.choosingLocal = false; panel.service.useLocal(text.trim()) }
                    }
                    PlatinumButton {
                        id: useButton; objectName: "software-update-use"; text: "Check Package"
                        enabled: !panel.busy && localPath.text.trim() !== ""
                        onClicked: { panel.choosingLocal = false; panel.service.useLocal(localPath.text.trim()) }
                    }
                }
            }
        }
        // Progress and messages.
        Rectangle {
            objectName: "software-update-progress"
            width: parent.width; height: 12; border.color: "#333"; color: "#eee"
            visible: panel.service && panel.service.phase === "downloading"
            Rectangle {x: 1; y: 1; height: parent.height - 2; color: "#6666cc"
                       width: (parent.width - 2) * (panel.service ? panel.service.progress : 0)}
        }
        Text {
            objectName: "software-update-message"
            width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 12; textFormat: Text.PlainText
            // After an install its message is in the box above, beside Restart Desktop.
            visible: !(panel.service && panel.service.restartNeeded && panel.service.phase === ""
                       && !panel.service.error)
            color: panel.service && panel.service.error ? "#900" : "black"
            text: !panel.service ? ""
                : panel.service.error ? panel.service.error
                : panel.service.phase === "downloading" ? "Downloading…"
                : panel.service.phase === "verifying" ? "Checking the package…"
                : panel.service.phase === "installing" || panel.service.phase === "restoring"
                    ? (panel.service.message || "Waiting for the administrator's password…")
                : panel.service.message
        }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        spacing: 10
        PlatinumButton {
            // The new version runs once the desktop restarts; the applications open again.
            objectName: "software-update-restart-desktop"; text: "Restart Desktop"
            visible: panel.service && panel.service.restartNeeded && typeof manager !== "undefined"
                     && manager.restartDesktopAvailable
            enabled: !panel.busy && typeof desktop !== "undefined" && desktop
            onClicked: desktop.restartDesktop()
        }
        PlatinumButton {
            objectName: "software-update-restore"; text: "Restore Previous Version…"
            visible: panel.service && panel.service.previousKept
            enabled: !panel.busy; onClicked: panel.service.restorePrevious()
        }
        PlatinumButton {
            objectName: "software-update-download"; text: "Download"
            visible: panel.service && panel.service.available.version !== undefined && panel.service.ready === ""
            enabled: !panel.busy; onClicked: panel.service.download()
        }
        PlatinumButton {
            objectName: "software-update-install"; text: "Install"
            visible: panel.service && panel.service.ready !== ""
            enabled: !panel.busy; onClicked: panel.service.install()
        }
    }
}
