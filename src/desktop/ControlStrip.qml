import QtQuick
import QtQuick.Controls

// Mac OS 8.5-9 Control Strip (docs/historical-fidelity-review.md F09): attached to the left or
// right screen edge, a collapse box and scroll arrow at the edge end, pop-up modules, and a
// ridged end tab. Desktop.qml owns the module menus so they share the shell menu rows.
Item {
    id: strip
    required property var sessionManager
    required property var settings
    // The module whose menu is open, drawn pressed while it is.
    property string openModule: ""
    signal moduleRequested(string module, Item anchor)
    objectName: "control-strip"
    // Applications' tray items (StatusNotifierItem) follow the built-in modules:
    // {command: "status:<key>", name, icon, statusId}.
    property var extraModules: []
    readonly property rect screenRect: sessionManager.primaryDisplayRect
    readonly property var modules: [
        {command:"applications",name:"Applications"},
        {command:"appearance",name:"Appearance"},
        {command:"sound",name:"Sound"},
        {command:"monitors",name:"Brightness"},
        {command:"wifi",name:"Wi-Fi"},
        {command:"bluetooth",name:"Bluetooth"}]
        .concat(battery && battery.present ? [{command:"battery",name:"Battery"}] : [])
        .concat(handheld && handheld.available ? [{command:"handheld",name:"Handheld"}] : [])
        .concat(extraModules)
    // The laptop's own battery (BatteryStatus); the module appears only when there is one.
    readonly property var battery: sessionManager.battery
    // Handheld Daemon (HandheldDaemon): shown while hhd runs on this computer.
    readonly property var handheld: sessionManager.handheld
    readonly property int moduleWidth: 36
    readonly property int endWidth: 16
    readonly property int arrowWidth: 13
    readonly property int fixedWidth: 2*endWidth + 2*arrowWidth
    readonly property int fittingModules: Math.max(1, Math.min(modules.length,
        Math.floor((screenRect.width - fixedWidth) / moduleWidth)))
    // The length chosen by dragging the end tab, in whole modules (0 shows all that fit).
    readonly property int visibleModules: settings.controlStripModules > 0
        ? Math.min(settings.controlStripModules, fittingModules) : fittingModules
    property int firstModule: 0
    onVisibleModulesChanged: firstModule = Math.max(0, Math.min(firstModule, modules.length - visibleModules))
    readonly property bool collapsed: settings.controlStripCollapsed
    // Verified in the 9.2.2 reference VM (2026-11-04): a plain drag of the end tab changes the
    // strip's length, in whole modules, and dragging it right back to the edge collapses it.
    // Option-drag moves the strip to any height on either side edge (whichever half of the screen
    // the pointer is in), from just below the menu bar to the bottom. Either way the strip stays
    // put while an outline follows the pointer, and changes when the button is released.
    property bool moving: false
    property bool resizing: false
    readonly property bool dragging: moving || resizing
    property point pressOrigin
    property real grabY
    property real outlineY
    property bool outlineRight
    property real outlineLength
    readonly property real topLimit: screenRect.y + 27
    // The edge comes from the saved position, so a strip saved on the right stays there when the
    // screen changes size; free positions saved by older versions go to the nearer edge.
    readonly property bool rightEdge: settings.controlStripX + width / 2 > screenRect.width / 2
    readonly property int viewportWidth: visibleModules * moduleWidth
    // Parts are placed from the screen edge outwards, mirrored on the right edge. Bindings rather
    // than a Row keep hit-testing correct before the next frame's polish.
    function place(offset, partWidth) { return rightEdge ? width - offset - partWidth : offset }
    width: collapsed ? endWidth : fixedWidth + viewportWidth
    height: 28
    x: screenRect.x + (rightEdge ? screenRect.width - width : 0)
    y: screenRect.y + Math.max(27, Math.min(settings.controlStripY < 0 ? screenRect.height - height
                                                                        : settings.controlStripY,
                                            screenRect.height - height))
    visible: settings.controlStripVisible
    enabled: !sessionManager.shellModal && !sessionManager.menuOpen

    // Body: open at the screen edge, closed by the end tab.
    Rectangle {
        visible: !strip.collapsed
        x: strip.rightEdge ? strip.endWidth - 1 : 0; width: parent.width - strip.endWidth + 1; height: parent.height
        color: "#ccc"
        Rectangle {width: parent.width; height: 1; color: "#222"}
        Rectangle {y: 1; width: parent.width; height: 1; color: "white"}
        Rectangle {y: parent.height - 2; width: parent.width; height: 1; color: "#888"}
        Rectangle {y: parent.height - 1; width: parent.width; height: 1; color: "#222"}
    }
    Item {
        anchors.fill: parent
        Item {
            objectName: "control-strip-collapse"
            x: strip.place(0, width)
            visible: !strip.collapsed; width: strip.endWidth; height: strip.height
            Rectangle {
                x: 2; y: 8; width: 12; height: 12; border.color: "#222"
                color: collapseMouse.pressed ? "#888" : "#ddd"
                Rectangle {x: 1; y: 1; width: 9; height: 1; color: collapseMouse.pressed ? "#666" : "white"}
                Rectangle {x: 1; y: 1; width: 1; height: 9; color: collapseMouse.pressed ? "#666" : "white"}
                Rectangle {x: 3; y: 3; width: 6; height: 6; color: "transparent"; border.color: "#555"}
            }
            MouseArea {
                id: collapseMouse; anchors.fill: parent; hoverEnabled: true
                onClicked: strip.settings.setControlStripCollapsed(true)
            }
            ToolTip.visible: collapseMouse.containsMouse
            ToolTip.delay: 600
            ToolTip.text: "Collapse the Control Strip"
        }
        StripArrow {
            objectName: "control-strip-scroll-back"
            x: strip.place(strip.endWidth, width)
            visible: !strip.collapsed; width: strip.arrowWidth; height: strip.height
            pointsLeft: !strip.rightEdge
            active: strip.firstModule > 0
            onActivated: strip.firstModule -= 1
        }
        Item {
            x: strip.place(strip.endWidth + strip.arrowWidth, width)
            visible: !strip.collapsed; clip: true
            width: strip.viewportWidth; height: strip.height
            Item {
                x: -strip.firstModule * strip.moduleWidth
                Repeater {
                    model: strip.modules
                    delegate: Item {
                        id: module
                        required property var modelData
                        required property int index
                        objectName: modelData.statusId !== undefined ? "control-strip-status-" + modelData.statusId
                                                                     : "control-strip-" + modelData.command
                        x: index * strip.moduleWidth
                        width: strip.moduleWidth; height: strip.height
                        readonly property bool down: strip.openModule === modelData.command || moduleMouse.pressed
                        Rectangle {
                            x: 3; y: 3; width: 24; height: 22; border.color: "#222"
                            color: module.down ? "#888" : "#ddd"
                            Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: module.down ? "#555" : "white"}
                            Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: module.down ? "#555" : "white"}
                            Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: module.down ? "#aaa" : "#999"}
                            Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: module.down ? "#aaa" : "#999"}
                            StripGlyph {x: 2; y: 1; kind: module.modelData.statusId !== undefined ? "status" : module.modelData.command
                                        visible: !module.modelData.icon
                                        level: strip.battery ? strip.battery.percent : -1
                                        charging: !!strip.battery && strip.battery.pluggedIn}
                            Image {
                                objectName: "control-strip-icon"
                                x: 2; y: 1; width: 20; height: 20
                                visible: Boolean(module.modelData.icon)
                                source: module.modelData.icon ? module.modelData.icon : ""
                                sourceSize: Qt.size(40, 40); smooth: true; fillMode: Image.PreserveAspectFit
                            }
                        }
                        // The small pop-up indicator to the right of every module (V922 E04).
                        Repeater {
                            model: 4
                            Rectangle {
                                required property int index
                                x: 29 + index; y: 10 + index; width: 1; height: 7 - 2*index; color: "#222"
                            }
                        }
                        MouseArea {
                            id: moduleMouse; anchors.fill: parent; hoverEnabled: true
                            onClicked: strip.moduleRequested(module.modelData.command, module)
                        }
                        ToolTip.visible: moduleMouse.containsMouse && strip.openModule === ""
                        ToolTip.delay: 600
                        ToolTip.text: modelData.name
                    }
                }
            }
        }
        StripArrow {
            objectName: "control-strip-scroll-forward"
            x: strip.place(strip.endWidth + strip.arrowWidth + strip.viewportWidth, width)
            visible: !strip.collapsed; width: strip.arrowWidth; height: strip.height
            pointsLeft: strip.rightEdge
            active: strip.firstModule + strip.visibleModules < strip.modules.length
            onActivated: strip.firstModule += 1
        }
        Item {
            objectName: "control-strip-tab"
            x: strip.collapsed ? 0 : strip.place(strip.width - strip.endWidth, width)
            width: strip.endWidth; height: strip.height
            Canvas {
                id: tab
                anchors.fill: parent
                transform: Scale {origin.x: tab.width / 2; xScale: strip.rightEdge ? -1 : 1}
                onPaint: {
                    let c = getContext("2d")
                    c.clearRect(0, 0, width, height)
                    const w = width, h = height, r = 7
                    // Pressed (darkened) while it is being dragged, as in 9.2.2.
                    c.fillStyle = strip.dragging ? "#888" : "#ccc"; c.strokeStyle = "#222"; c.lineWidth = 1
                    c.beginPath()
                    c.moveTo(0, 0.5); c.lineTo(w - r - 0.5, 0.5)
                    c.arcTo(w - 0.5, 0.5, w - 0.5, r + 0.5, r)
                    c.lineTo(w - 0.5, h - r - 0.5)
                    c.arcTo(w - 0.5, h - 0.5, w - r - 0.5, h - 0.5, r)
                    c.lineTo(0, h - 0.5)
                    if (strip.collapsed) c.lineTo(0.5, h - 0.5), c.lineTo(0.5, 0.5)
                    c.fill(); c.stroke()
                    for (const x of [3, 6, 9]) {
                        c.fillStyle = strip.dragging ? "#555" : "#777"; c.fillRect(x, 8, 1, h - 16)
                        c.fillStyle = strip.dragging ? "#aaa" : "white"; c.fillRect(x + 1, 8, 1, h - 16)
                    }
                }
                Connections {
                    target: strip
                    function onCollapsedChanged() {tab.requestPaint()}
                    function onDraggingChanged() {tab.requestPaint()}
                }
                // A canvas created while hidden paints nothing until asked again.
                onAvailableChanged: if (available) requestPaint()
                onVisibleChanged: if (visible) requestPaint()
            }
            MouseArea {
                id: tabMouse; anchors.fill: parent; hoverEnabled: true
                property bool optionDrag: false
                // What the drag does, decided as it starts: "move" (Option-drag as in 9.2.2, or a
                // plain drag that starts up or down), else "resize" (a sideways drag, as in 9.2.2).
                property string mode: ""
                onPressed: function(mouse) {
                    mode = ""
                    strip.pressOrigin = mapToItem(strip.parent, mouse.x, mouse.y)
                    optionDrag = Boolean(mouse.modifiers & Qt.AltModifier)
                    strip.grabY = strip.pressOrigin.y - strip.y
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    let point = mapToItem(strip.parent, mouse.x, mouse.y)
                    let dx = point.x - strip.pressOrigin.x, dy = point.y - strip.pressOrigin.y
                    if (!strip.dragging && Math.abs(dx) + Math.abs(dy) < 4) return
                    if (mode === "") mode = optionDrag || Math.abs(dy) > Math.abs(dx) ? "move" : "resize"
                    const screen = strip.screenRect
                    if (mode === "move") {
                        strip.outlineRight = point.x >= screen.x + screen.width / 2
                        strip.outlineY = Math.max(strip.topLimit, Math.min(point.y - strip.grabY,
                                                  screen.y + screen.height - strip.height))
                        strip.moving = true
                    } else {
                        // Only the length follows the pointer; the height stays.
                        strip.outlineLength = Math.max(strip.endWidth, Math.min(
                            strip.width + (strip.rightEdge ? -dx : dx), strip.fixedWidth + strip.fittingModules * strip.moduleWidth))
                        strip.resizing = true
                    }
                }
                onReleased: {
                    const screen = strip.screenRect
                    if (strip.moving) {
                        strip.settings.setControlStripPosition(strip.outlineRight ? screen.width - strip.endWidth : 0,
                                                               Math.round(strip.outlineY - screen.y))
                    } else if (strip.resizing) {
                        // Pulled back to the edge, the strip collapses to its tab; otherwise it
                        // keeps as many whole modules as the outline nearly covers.
                        if (strip.outlineLength < 2 * strip.endWidth) {
                            strip.settings.setControlStripCollapsed(true)
                        } else {
                            const count = Math.max(1, Math.min(strip.fittingModules,
                                Math.round((strip.outlineLength - strip.fixedWidth) / strip.moduleWidth)))
                            strip.settings.setControlStripModules(count >= strip.modules.length ? 0 : count)
                        }
                    } else {
                        strip.settings.setControlStripCollapsed(!strip.collapsed)
                    }
                    strip.moving = false; strip.resizing = false
                }
                onCanceled: {strip.moving = false; strip.resizing = false}
            }
            ToolTip.visible: tabMouse.containsMouse && !strip.dragging
            ToolTip.delay: 600
            ToolTip.text: strip.collapsed ? "Click or drag out to show the Control Strip; drag up or down to move it."
                                          : "Click to collapse; drag sideways to change the length; drag up or down to move it."
        }
    }

    // The drag outline: the strip's silhouette as a two-pixel band one pixel outside its edge.
    // 9.2.2 inverts the screen under it; black and white keep it visible on any background.
    Item {
        id: outline
        objectName: "control-strip-outline"
        parent: strip.parent
        z: strip.z + 1
        visible: strip.dragging
        readonly property bool onRight: strip.moving ? strip.outlineRight : strip.rightEdge
        readonly property real span: strip.moving ? strip.width : strip.outlineLength
        x: (onRight ? strip.screenRect.x + strip.screenRect.width - span : strip.screenRect.x) - 1
        y: (strip.moving ? strip.outlineY : strip.y) - 1
        width: span + 2; height: strip.height + 2
        Canvas {
            id: outlineShape
            anchors.fill: parent
            transform: Scale {origin.x: outlineShape.width / 2; xScale: outline.onRight ? -1 : 1}
            onPaint: {
                let c = getContext("2d")
                c.clearRect(0, 0, width, height)
                const trace = function(inset, colour) {
                    const w = width - inset, h = height - inset, r = 8 - inset
                    c.strokeStyle = colour; c.lineWidth = 1
                    c.beginPath()
                    c.moveTo(inset, inset); c.lineTo(w - r, inset)
                    c.arcTo(w, inset, w, inset + r, r)
                    c.lineTo(w, h - r)
                    c.arcTo(w, h, w - r, h, r)
                    c.lineTo(inset, h); c.closePath(); c.stroke()
                }
                trace(0.5, "black"); trace(1.5, "white")
            }
            onWidthChanged: requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onAvailableChanged: if (available) requestPaint()
        }
    }

    component StripArrow: Item {
        id: arrow
        property bool pointsLeft: true
        property bool active: false
        signal activated()
        Canvas {
            id: arrowShape
            anchors.fill: parent
            transform: Scale {origin.x: arrowShape.width / 2; xScale: arrow.pointsLeft ? 1 : -1}
            onPaint: {
                let c = getContext("2d")
                c.clearRect(0, 0, width, height)
                c.strokeStyle = arrow.active ? "#222" : "#999"; c.fillStyle = arrow.active ? "white" : "#ddd"
                c.beginPath()
                c.moveTo(2.5, 14); c.lineTo(7.5, 9.5); c.lineTo(7.5, 12.5); c.lineTo(11.5, 12.5)
                c.lineTo(11.5, 15.5); c.lineTo(7.5, 15.5); c.lineTo(7.5, 18.5); c.closePath()
                c.fill(); c.stroke()
            }
            Connections {target: arrow; function onActiveChanged() {arrowShape.requestPaint()}}
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
        }
        MouseArea {anchors.fill: parent; enabled: arrow.active; onClicked: arrow.activated()}
    }
}
