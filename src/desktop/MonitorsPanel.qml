import QtQuick
import QtQuick.Controls

// Mac OS 9's Monitors control panel, laid out as 9.2.2's (captured 2026-11-12): a picture of the
// display and a row of view buttons. Monitor shows Color Depth (always Millions here: a Wayland
// desktop has no 256-colour mode, so it is shown, not offered) beside Resolution with its Show
// pop-up, then our Rotation and Size; Arrange (when there is more than one display, or the
// next-login choice to use them) shows the displays drawn to scale, with a small menu bar on the
// main display. Dragging that menu bar to another display makes it the main one,
// as on a Mac. The request goes to the private session supervisor; the desktop follows the
// layout it then publishes. In a managed session (every real login) each connected display has
// an On box that takes effect at once and then waits for Keep (the supervisor undoes it after
// 15 seconds otherwise; one display always stays on). Mirroring is shown but not offered until
// its input and cursor handling are complete (docs/multi-display-development.md).
// It is a panel of the Control Panels window: choosing the main display applies at once.
Item {
    id: panel
    required property var sessionManager
    objectName: "monitors-panel"
    readonly property string panelId: "monitors"
    readonly property string title: "Monitors"
    readonly property string glyph: "display"
    // Set by the Control Panels window: it, and whether this panel is the one on screen.
    property var host: null
    property bool shown: false
    readonly property bool opened: shown
    width: parent ? parent.width : 560
    implicitHeight: body.implicitHeight
    readonly property var outputs: sessionManager.displayOutputs
    readonly property bool arrangeable: sessionManager.displayArrangementAvailable && outputs.length > 1
    readonly property bool switchable: sessionManager.displaySwitchingAvailable
    readonly property bool mirroring: sessionManager.displayMirrors.length > 0
    // Every connected display, on ones first: {name, label, on, internal, lidClosed, width, height}.
    readonly property var connected: {
        let all = []
        for (const o of outputs) all.push({name: o.name, label: o.label || o.name, on: true, internal: o.internal,
                                           lidClosed: false, width: o.width, height: o.height})
        if (switchable)
            for (const o of sessionManager.displayMirrors)
                all.push({name: o.name, label: o.label, on: true, lidClosed: false, mirrored: true,
                          internal: /^(eDP|LVDS|DSI)/.test(o.name), width: 0, height: 0})
        if (switchable)
            for (const o of sessionManager.displaysOff)
                all.push({name: o.name, label: o.label, on: false, lidClosed: o.lidClosed,
                          internal: /^(eDP|LVDS|DSI)/.test(o.name), width: 0, height: 0})
        return all
    }
    function displayName(d) { return d.internal ? "Built-in" : d.label }
    // The display whose resolution, rotation and size are shown: the one clicked, else the main one.
    readonly property var chosenOutput: {
        for (const o of outputs) if (o.name === selected) return o
        for (const o of outputs) if (o.primary) return o
        return outputs.length ? outputs[0] : null
    }
    readonly property bool settingsEnabled: switchable && !sessionManager.displayRequestBusy
                                            && !sessionManager.displayChangePending
    function refreshText(r) {
        const hz = r / 1000
        return Math.abs(hz - Math.round(hz)) < 0.01 ? Math.round(hz) + " Hz" : hz.toFixed(2) + " Hz"
    }
    function sameMode(a, b) { return !!a && !!b && a.width === b.width && a.height === b.height && a.refresh === b.refresh }
    readonly property var scales: {
        let list = [1, 1.25, 1.5, 1.75, 2, 2.5, 3]
        const current = chosenOutput ? Math.round(chosenOutput.scale * 100) / 100 : 1
        if (!list.some(function(v) { return Math.abs(v - current) < 0.005 })) list.push(current)
        return list.sort(function(a, b) { return a - b })
    }
    // "monitor" or "arrange", as 9.2.2's view buttons; Arrange only when it has something to show.
    property string view: "monitor"
    readonly property bool arrangeOffered: connected.length > 1 || !switchable
    readonly property string shownView: arrangeOffered ? view : "monitor"
    // Show: Recommended lists each size once, at its fastest refresh (and the current mode); All lists every mode.
    property bool allModes: false
    readonly property var shownModes: {
        const modes = chosenOutput && chosenOutput.modes ? chosenOutput.modes : []
        if (allModes) return modes
        return modes.filter(function(m) {
            if (chosenOutput && sameMode(m, chosenOutput.mode)) return true
            return !modes.some(function(o) { return o.width === m.width && o.height === m.height && o.refresh > m.refresh })
        })
    }
    // The main display as shown: the one asked for until the supervisor publishes the new layout.
    property string draftPrimary: ""
    property string selected: ""
    // "Use all displays" as this session started; a different saved choice applies at the next login.
    property bool useAllAtStart: false
    Component.onCompleted: useAllAtStart = sessionManager.useAllDisplays
    function currentPrimary() {
        for (const output of outputs) if (output.primary) return output.name
        return outputs.length ? outputs[0].name : ""
    }
    function edit() { if (host) host.show(panelId) }
    function makeMain(name) {
        if (!arrangeable || name === "" || name === draftPrimary) return
        draftPrimary = name
        selected = name
        if (name !== currentPrimary()) sessionManager.requestPrimaryDisplay(name)
    }
    onShownChanged: if (shown) { draftPrimary = currentPrimary(); selected = draftPrimary }
    onOutputsChanged: if (!sessionManager.displayRequestBusy) draftPrimary = currentPrimary()
    // A view button, as 9.2.2's: a square well with the picture, sunken while its view is shown, the name below.
    component ViewButton: Item {
        id: viewButton
        property string kind: ""
        property string label: ""
        property string value: ""
        width: 56; height: 54
        Rectangle {
            id: well
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36; height: 36
            readonly property bool sunk: panel.shownView === viewButton.value || press.pressed
            color: sunk ? "#777" : "#ddd"; border.color: "black"
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: well.sunk ? "#444" : "white" }
            Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: well.sunk ? "#444" : "white" }
            Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: well.sunk ? "#999" : "#888" }
            Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: well.sunk ? "#999" : "#888" }
            StripGlyph { anchors.centerIn: parent; width: 24; height: 24; kind: viewButton.kind }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; y: 38
            text: viewButton.label; textFormat: Text.PlainText; font.pixelSize: 11
        }
        MouseArea { id: press; anchors.fill: parent; onClicked: panel.view = viewButton.value }
    }
    Connections {
        target: panel.sessionManager
        function onDisplayRequestChanged() { if (!panel.sessionManager.displayRequestBusy) panel.draftPrimary = panel.currentPrimary() }
    }
    Column {
        id: body
        width: parent.width
        spacing: 10
        // The display's picture, the view buttons, and which display the Monitor view is for.
        Row {
            width: parent.width
            spacing: 12
            StripGlyph { anchors.verticalCenter: parent.verticalCenter; width: 48; height: 48; kind: "display" }
            ViewButton { objectName: "monitors-view-monitor"; kind: "monitors"; label: "Monitor"; value: "monitor" }
            ViewButton { objectName: "monitors-view-arrange"; kind: "display"; label: "Arrange"; value: "arrange"
                         visible: panel.arrangeOffered }
            Item { width: 8; height: 1 }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                visible: panel.chosenOutput !== null && panel.shownView === "monitor"
                Text { objectName: "monitors-settings-title"; font.pixelSize: 13; font.bold: true
                       visible: panel.outputs.length < 2
                       text: panel.chosenOutput ? panel.displayName(panel.chosenOutput) : "" }
                // With more than one display on, the Monitor view is for the one chosen here (or clicked in Arrange).
                PlatinumPopup {
                    objectName: "monitors-choose-display"
                    visible: panel.outputs.length > 1
                    implicitWidth: 160
                    choices: panel.outputs.map(function(o) { return {name: panel.displayName(o), value: o.name} })
                    current: panel.chosenOutput ? panel.chosenOutput.name : ""
                    onChosen: function(value) { panel.selected = value }
                }
            }
        }
        // Monitor: the chosen display's colour depth, resolution, rotation and size. Like the On boxes,
        // each takes effect at once and waits for Keep.
        Column {
            objectName: "monitors-settings"
            visible: panel.shownView === "monitor"
            width: parent.width
            spacing: 8
            Row {
                spacing: 10
                width: parent.width
                PlatinumGroup {
                    title: "Color Depth"; objectName: "monitors-color-depth"
                    width: 200; spacing: 6
                    Row {
                        spacing: 8
                        Column {
                            PlatinumRadioButton { text: "Grays"; enabled: false; autoExclusive: false }
                            PlatinumRadioButton { text: "Colors"; enabled: false; checked: true; autoExclusive: false }
                        }
                        Rectangle {
                            width: 84; height: 56; color: "white"; border.color: "#555"
                            Column {
                                x: 1; y: 1; width: parent.width - 2
                                Repeater {
                                    model: ["256", "Thousands", "Millions"]
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: parent.width; height: 18
                                        color: modelData === "Millions" ? "#ccf" : "white"
                                        Text { x: 4; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 11
                                               text: parent.modelData; color: parent.modelData === "Millions" ? "black" : "#888" }
                                    }
                                }
                            }
                        }
                    }
                    // The colour ramp under the depths: grey, red, green and blue from black.
                    Rectangle {
                        width: parent.width; height: 22; border.color: "black"; color: "black"
                        Column {
                            x: 1; y: 1; width: parent.width - 2
                            Repeater {
                                model: ["white", "red", "#0c0", "blue"]
                                delegate: Rectangle {
                                    required property string modelData
                                    width: parent.width; height: 5
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: "black" }
                                        GradientStop { position: 1; color: modelData }
                                    }
                                }
                            }
                        }
                    }
                }
                PlatinumGroup {
                    title: "Resolution"; objectName: "monitors-resolution"
                    width: parent.width - 210; spacing: 6
                    Row {
                        spacing: 6
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "Show:"; font.pixelSize: 12; font.bold: true }
                        PlatinumPopup {
                            objectName: "monitors-resolution-show"
                            implicitWidth: 150
                            choices: [{name: "Recommended", value: "recommended"}, {name: "All", value: "all"}]
                            current: panel.allModes ? "all" : "recommended"
                            onChosen: function(value) { panel.allModes = value === "all" }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 92; color: "white"; border.color: "#555"
                        ListView {
                            id: resolutions
                            objectName: "monitors-resolutions"
                            anchors.fill: parent; anchors.margins: 1; clip: true
                            model: panel.shownModes
                            ScrollBar.vertical: PlatinumScrollBar {}
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool current: panel.sameMode(modelData, panel.chosenOutput.mode)
                                objectName: "monitors-resolution-" + index
                                width: resolutions.width - 16; height: 18
                                color: current ? "#ccf" : "white"
                                Text {x: 6; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                                      color: parent.current || panel.settingsEnabled ? "#111" : "#888"
                                      text: modelData.width + " × " + modelData.height + ", " + panel.refreshText(modelData.refresh)}
                                MouseArea {anchors.fill: parent; enabled: panel.settingsEnabled && !parent.current
                                           onClicked: sessionManager.setDisplayMode(panel.chosenOutput.name, modelData.width,
                                                                                    modelData.height, modelData.refresh)}
                            }
                        }
                    }
                }
            }
            Row {
                spacing: 10
                width: parent.width
                visible: panel.switchable && panel.chosenOutput !== null
                PlatinumGroup {
                    title: "Rotation"; objectName: "monitors-rotation"
                    width: 200; spacing: 0
                    Repeater {
                        model: [{value: "normal", text: "Standard"}, {value: "90", text: "90°"},
                                {value: "180", text: "180°"}, {value: "270", text: "270°"}]
                        delegate: PlatinumRadioButton {
                            required property var modelData
                            objectName: "monitors-rotate-" + modelData.value
                            autoExclusive: false  // The published rotation decides, not the click.
                            text: modelData.text
                            enabled: panel.settingsEnabled
                            checked: panel.chosenOutput !== null && panel.chosenOutput.transform === modelData.value
                            onClicked: {
                                const value = modelData.value
                                checked = Qt.binding(function() { return panel.chosenOutput !== null && panel.chosenOutput.transform === value })
                                sessionManager.setDisplayRotation(panel.chosenOutput.name, value)
                            }
                        }
                    }
                }
                PlatinumGroup {
                    title: "Size"; objectName: "monitors-size"
                    width: parent.width - 210; spacing: 0
                    Grid {
                        columns: 2; columnSpacing: 8
                        Repeater {
                            model: panel.scales
                            delegate: PlatinumRadioButton {
                                required property var modelData
                                objectName: "monitors-scale-" + Math.round(modelData * 100)
                                autoExclusive: false
                                text: Math.round(modelData * 100) + "%"
                                enabled: panel.settingsEnabled
                                checked: panel.chosenOutput !== null && Math.abs(panel.chosenOutput.scale - modelData) < 0.005
                                onClicked: {
                                    const value = modelData
                                    checked = Qt.binding(function() { return panel.chosenOutput !== null && Math.abs(panel.chosenOutput.scale - value) < 0.005 })
                                    sessionManager.setDisplayScale(panel.chosenOutput.name, value)
                                }
                            }
                        }
                    }
                }
            }
        }
        // Arrange: the displays to scale, the main display's menu bar, which displays are on, mirroring.
        Column {
            objectName: "monitors-arrange-view"
            visible: panel.shownView === "arrange"
            width: parent.width
            spacing: 8
            Text {
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12
                text: panel.arrangeable ? (panel.switchable ? "Drag displays to arrange them; drag the menu bar to choose the main display."
                                                             : "To change the main display, drag the menu bar to another display.")
                      : panel.switchable ? "Only one display is on. To use another one, turn it on below."
                      : sessionManager.useAllDisplays ? "Only one display is currently active."
                      : "To use more than one display, turn on “Use all displays” and log in again."
                objectName: panel.arrangeable ? "monitors-instructions" : "monitors-unavailable"
            }
            Rectangle {
                id: arrange
                objectName: "monitors-arrange"
                width: parent.width; height: 150
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
                // A display dropped at panel position (px, py): its new logical top-left corner, flush against
                // the nearest side of another display (tops or left edges line up when close), never overlapping.
                function snap(name, px, py) {
                    const dragged = panel.outputs.find(function(o) { return o.name === name })
                    const others = panel.outputs.filter(function(o) { return o.name !== name })
                    const w = dragged.width, h = dragged.height
                    const rx = bounds.x + (px - offsetX) / factor, ry = bounds.y + (py - offsetY) / factor
                    const clamp = function(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
                    const overlaps = function(x, y) {
                        return others.some(function(o) { return x < o.x + o.width && o.x < x + w && y < o.y + o.height && o.y < y + h })
                    }
                    let best = null
                    for (const o of others) {
                        let ay = clamp(ry, o.y - h + 1, o.y + o.height - 1), ax = clamp(rx, o.x - w + 1, o.x + o.width - 1)
                        if (Math.abs(ay - o.y) < h * 0.08) ay = o.y
                        if (Math.abs(ax - o.x) < w * 0.08) ax = o.x
                        for (const c of [{x: o.x + o.width, y: ay}, {x: o.x - w, y: ay}, {x: ax, y: o.y + o.height}, {x: ax, y: o.y - h}]) {
                            const d = Math.hypot(c.x - rx, c.y - ry)
                            if (!overlaps(c.x, c.y) && (best === null || d < best.d)) best = {x: Math.round(c.x), y: Math.round(c.y), d: d}
                        }
                    }
                    return best
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
                                  text: screen.modelData.internal ? "Built-in" : (screen.modelData.label || screen.modelData.name)}
                            Text {anchors.horizontalCenter: parent.horizontalCenter; color: "white"; font.pixelSize: 11
                                  text: screen.modelData.width + " × " + screen.modelData.height}
                        }
                        // Click to choose; drag (with two or more displays on) to arrange.
                        MouseArea {
                            anchors.fill: parent; enabled: panel.arrangeable || panel.switchable
                            drag.target: panel.switchable && panel.outputs.length > 1 && panel.settingsEnabled ? screen : null
                            drag.threshold: 4
                            onClicked: panel.selected = screen.modelData.name
                            onReleased: {
                                const moved = Math.abs(screen.x - screen.area.x) > 2 || Math.abs(screen.y - screen.area.y) > 2
                                const place = moved ? arrange.snap(screen.modelData.name, screen.x, screen.y) : null
                                screen.x = Qt.binding(function() { return screen.area.x })
                                screen.y = Qt.binding(function() { return screen.area.y })
                                if (place !== null) {
                                    panel.selected = screen.modelData.name
                                    sessionManager.setDisplayPosition(screen.modelData.name, place.x, place.y)
                                }
                            }
                        }
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
                            if (target !== "") panel.makeMain(target)
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
                    onClicked: panel.makeMain(panel.selected)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12
                    text: {
                        for (const d of panel.connected) if (d.name === panel.draftPrimary) return "Main display: " + panel.displayName(d)
                        return ""
                    }
                    objectName: "monitors-main-label"
                }
            }
            // Each connected display, on or off now. Not a draft: OK and Cancel do not undo it, Keep does.
            Column {
                objectName: "monitors-switches"
                visible: panel.switchable
                spacing: 2
                Text {font.pixelSize: 12; text: "Show the desktop on:"}
                Repeater {
                    model: panel.switchable ? panel.connected : []
                    delegate: PlatinumCheckBox {
                        required property var modelData
                        implicitHeight: 22
                        objectName: "monitors-on-" + modelData.name
                        leftPadding: 12
                        text: panel.displayName(modelData) + (modelData.lidClosed ? " (the lid is closed)" : "")
                        checked: modelData.on
                        // The last display that is on stays on; a shut lid keeps the built-in panel off.
                        enabled: !sessionManager.displayRequestBusy && !sessionManager.displayChangePending
                                 && !modelData.lidClosed && !(modelData.on && panel.outputs.length + sessionManager.displayMirrors.length < 2)
                        onToggled: {
                            const name = modelData.name, on = checked
                            checked = Qt.binding(function() { return modelData.on })
                            sessionManager.setDisplayOn(name, on)
                        }
                    }
                }
            }
            Row {
                spacing: 16
                // One desktop across connected displays, retaining useful saved positions. Takes effect at
                // the next login; if it cannot start, the session falls back to one display. A managed
                // session has an On box per display instead.
                PlatinumCheckBox {objectName: "monitors-use-all"; text: "Use all displays"; visible: !panel.switchable
                                  checked: sessionManager.useAllDisplays; onToggled: sessionManager.setUseAllDisplays(checked)}
                // Every display shows the main display; applied at once, then Keep or Revert.
                PlatinumCheckBox {
                    objectName: "monitors-mode-mirror"; text: "Mirror displays"
                    checked: panel.mirroring
                    enabled: panel.settingsEnabled && (panel.mirroring || panel.outputs.length > 1)
                    onToggled: {
                        const on = checked
                        checked = Qt.binding(function() { return panel.mirroring })
                        sessionManager.setDisplayMirroring(on)
                    }
                }
            }
            Text {
                objectName: "monitors-use-all-note"
                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#333"
                text: panel.switchable
                      ? (panel.mirroring ? "Every display shows the main display. " : "")
                        + "Display changes happen at once; you then have 15 seconds to keep them."
                      : (panel.useAllAtStart !== sessionManager.useAllDisplays
                         ? "“Use all displays” changes the next time you log in."
                         : panel.outputs.length > 1 ? "One desktop across the displays." : "")
            }
        }
    }
}
