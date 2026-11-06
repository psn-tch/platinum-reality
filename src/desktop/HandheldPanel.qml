import QtQuick
import QtQuick.Controls

// Handheld, a panel of the Control Panels window: the built-in controller On or Off
// (psn_tch, 2026-11-06). With an external controller plugged in, games often made the built-in
// one player 1; Off hides it from games (Handheld Daemon's Hidden mode), On brings back the mode
// it had. Listed only when this computer's Handheld Daemon offers that (HandheldDaemon).
Item {
    id: panel
    required property var service
    objectName: "handheld-panel"
    readonly property string panelId: "handheld"
    readonly property string title: "Handheld"
    readonly property string glyph: "handheld"
    readonly property bool listed: service !== null && service.controllerSwitchable
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    onListedChanged: if (host) host.updatePanels()
    onShownChanged: if (shown && service) service.refresh()
    width: parent ? parent.width : 540
    implicitHeight: body.implicitHeight
    readonly property bool ready: listed && !service.busy
    readonly property var controller: service && service.state ? service.state.controller : undefined
    readonly property string modeTitle: {
        if (!controller) return ""
        const current = controller.options.find(function(o) { return o.value === controller.value })
        return current ? current.title : controller.value
    }
    function edit() { if (host && listed) host.show(panelId) }
    Column {
        id: body
        width: Math.min(parent.width, 500)
        spacing: 14
        PlatinumGroup {
            title: "Built-in Controller"
            Row {
                spacing: 24
                PlatinumRadioButton {
                    objectName: "handheld-builtin-on"
                    autoExclusive: false  // hhd's answer decides, not the click.
                    text: "On"
                    enabled: panel.ready
                    checked: panel.listed && panel.service.controllerOn
                    onClicked: { checked = Qt.binding(function() { return panel.listed && panel.service.controllerOn })
                                 panel.service.setControllerOn(true) }
                }
                PlatinumRadioButton {
                    objectName: "handheld-builtin-off"
                    autoExclusive: false
                    text: "Off"
                    enabled: panel.ready
                    checked: panel.listed && !panel.service.controllerOn
                    onClicked: { checked = Qt.binding(function() { return panel.listed && !panel.service.controllerOn })
                                 panel.service.setControllerOn(false) }
                }
            }
            Text {
                objectName: "handheld-builtin-detail"
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#444"; textFormat: Text.PlainText
                text: !panel.listed ? ""
                    : panel.service.busy ? "Changing…"
                    : panel.service.controllerOn ? "Games see the built-in controller (as " + panel.modeTitle + ")."
                    : "Games don’t see the built-in controller, so an external controller is player 1."
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"; textFormat: Text.PlainText
            text: "Turn the built-in controller off when you play with an external one, then start the game. " +
                  "It stays off until you turn it on again, even after restarting. The Control Strip’s Handheld " +
                  "module has the same switch."
        }
    }
}
