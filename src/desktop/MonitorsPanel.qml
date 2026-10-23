import QtQuick
import QtQuick.Controls

// Mac OS 9's Monitors control panel, Arrange view: the displays drawn to scale, with a small
// menu bar on the main display. Dragging that menu bar to another display makes it the main one,
// as on a Mac. The request goes to the private session supervisor; the desktop follows the
// layout it then publishes. Mirroring is shown but not offered until its input and cursor
// handling are complete (docs/multi-display-development.md).
ShellDialog {
    id: panel
    objectName: "monitors-panel"
    title: "Monitors"
    width: 580
    standardButtons: Dialog.Ok | Dialog.Cancel
    readonly property var outputs: sessionManager.displayOutputs
    readonly property bool arrangeable: sessionManager.displayArrangementAvailable && outputs.length > 1
    property string draftPrimary: ""
    property string selected: ""
    property bool draftUseAll: false
    function currentPrimary() {
        for (const output of outputs) if (output.primary) return output.name
        return outputs.length ? outputs[0].name : ""
    }
    function edit() {
        if (visible || sessionManager.shellModal) return
        draftPrimary = currentPrimary()
        selected = draftPrimary
        draftUseAll = sessionManager.useAllDisplays
        open()
    }
    onAccepted: {
        if (arrangeable && draftPrimary !== currentPrimary()) sessionManager.requestPrimaryDisplay(draftPrimary)
        if (draftUseAll !== sessionManager.useAllDisplays) sessionManager.setUseAllDisplays(draftUseAll)
    }
    contentItem: Column {
        spacing: 12
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12
            text: panel.arrangeable ? "To change the main display, drag the menu bar to another display."
                  : sessionManager.useAllDisplays ? "Only one display is connected."
                  : "To use more than one display, turn on “Use all displays” and log in again."
            objectName: panel.arrangeable ? "monitors-instructions" : "monitors-unavailable"
        }
        Rectangle {
            id: arrange
            objectName: "monitors-arrange"
            width: parent.width; height: 230
            color: "#eee"; border.color: "#222"
            // Scale the union of the displays into the arrange area with a margin.
            readonly property rect bounds: {
                let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity
                for (const o of panel.outputs) {
                    left = Math.min(left, o.x); top = Math.min(top, o.y)
                    right = Math.max(right, o.x + o.width); bottom = Math.max(bottom, o.y + o.height)
                }
                return panel.outputs.length ? Qt.rect(left, top, right - left, bottom - top) : Qt.rect(0, 0, 1, 1)
            }
            readonly property real factor: Math.min((width - 60) / bounds.width, (height - 50) / bounds.height)
            readonly property real offsetX: (width - bounds.width * factor) / 2
            readonly property real offsetY: (height - bounds.height * factor) / 2
            function place(o) {
                return Qt.rect(offsetX + (o.x - bounds.x) * factor, offsetY + (o.y - bounds.y) * factor,
                               o.width * factor, o.height * factor)
            }
            function displayAt(x, y) {
                for (const o of panel.outputs) {
                    const r = place(o)
                    if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return o.name
                }
                return ""
            }
            Repeater {
                model: panel.outputs
                delegate: Rectangle {
                    id: screen
                    required property var modelData
                    readonly property rect area: arrange.place(modelData)
                    objectName: "monitors-display-" + modelData.name
                    x: area.x; y: area.y; width: area.width; height: area.height
                    color: "#4f6f8f"; border.color: "#111"; border.width: panel.selected === modelData.name ? 3 : 1
                    Rectangle {anchors.fill: parent; anchors.margins: 3; color: "transparent"; border.color: "#9fb4c8"}
                    Column {
                        anchors.centerIn: parent; spacing: 2
                        Text {anchors.horizontalCenter: parent.horizontalCenter; color: "white"; font.pixelSize: 12; font.bold: true
                              text: screen.modelData.internal ? "Built-in" : screen.modelData.name}
                        Text {anchors.horizontalCenter: parent.horizontalCenter; color: "white"; font.pixelSize: 11
                              text: screen.modelData.width + " × " + screen.modelData.height}
                    }
                    MouseArea {anchors.fill: parent; enabled: panel.arrangeable; onClicked: panel.selected = screen.modelData.name}
                }
            }
            // The main display's menu bar; drag it to another display.
            Rectangle {
                id: menuBarHandle
                objectName: "monitors-menubar"
                visible: panel.outputs.length > 0
                readonly property var home: {
                    for (const o of panel.outputs) if (o.name === panel.draftPrimary) return arrange.place(o)
                    return Qt.rect(0, 0, 0, 0)
                }
                x: home.x + 1; y: home.y + 1; width: Math.max(0, home.width - 2); height: 6
                color: "white"; border.color: "#111"
                MouseArea {
                    anchors.fill: parent; anchors.margins: -4
                    enabled: panel.arrangeable
                    drag.target: menuBarHandle; drag.threshold: 2
                    cursorShape: Qt.OpenHandCursor
                    onReleased: {
                        const target = arrange.displayAt(menuBarHandle.x + menuBarHandle.width / 2, menuBarHandle.y + 3)
                        if (target !== "") {panel.draftPrimary = target; panel.selected = target}
                        // Return to the (possibly new) main display.
                        menuBarHandle.x = Qt.binding(function() {return menuBarHandle.home.x + 1})
                        menuBarHandle.y = Qt.binding(function() {return menuBarHandle.home.y + 1})
                    }
                }
            }
        }
        Row {
            spacing: 10
            PlatinumButton {
                objectName: "monitors-make-main"; text: "Use as Main Display"; implicitWidth: 170
                enabled: panel.arrangeable && panel.selected !== "" && panel.selected !== panel.draftPrimary
                onClicked: panel.draftPrimary = panel.selected
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                text: panel.draftPrimary === "" ? "" : "Main display: " + panel.draftPrimary
                objectName: "monitors-main-label"
            }
        }
        Row {
            spacing: 16
            // One desktop across every connected display, as KDE arranged them. Takes effect at
            // the next login; if it cannot start, the session falls back to one display.
            PlatinumCheckBox {objectName: "monitors-use-all"; text: "Use all displays"
                              checked: panel.draftUseAll; onToggled: panel.draftUseAll = checked}
            PlatinumCheckBox {objectName: "monitors-mode-mirror"; text: "Mirror displays"; checked: false; enabled: false}
        }
        Text {
            objectName: "monitors-use-all-note"
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
            text: (panel.draftUseAll !== sessionManager.useAllDisplays
                   ? "“Use all displays” changes the next time you log in. "
                   : panel.outputs.length > 1 ? "One desktop across the displays. " : "")
                  + "Mirroring is not available yet: its pointer, cursor and drag handling are still being built."
        }
    }
}
