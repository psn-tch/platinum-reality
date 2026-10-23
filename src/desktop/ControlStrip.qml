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
        .concat(extraModules)
    // The laptop's own battery (BatteryStatus); the module appears only when there is one.
    readonly property var battery: sessionManager.battery
    readonly property int moduleWidth: 36
    readonly property int endWidth: 16
    readonly property int arrowWidth: 13
    readonly property int visibleModules: Math.max(1, Math.min(modules.length,
        Math.floor((screenRect.width - 2*endWidth - 2*arrowWidth) / moduleWidth)))
    property int firstModule: 0
    onVisibleModulesChanged: firstModule = Math.max(0, Math.min(firstModule, modules.length - visibleModules))
    readonly property bool collapsed: settings.controlStripCollapsed
    property bool dragging: false
    property bool dragFromRight: false
    property point pressOrigin
    property point initialPosition
    property point dragPosition
    // The edge comes from the saved position, so a strip saved on the right stays there when the
    // screen changes size; free positions saved by older versions go to the nearer edge.
    readonly property bool rightEdge: dragging ? dragFromRight
        : settings.controlStripX + width / 2 > screenRect.width / 2
    readonly property int viewportWidth: visibleModules * moduleWidth
    // Parts are placed from the screen edge outwards, mirrored on the right edge. Bindings rather
    // than a Row keep hit-testing correct before the next frame's polish.
    function place(offset, partWidth) { return rightEdge ? width - offset - partWidth : offset }
    width: collapsed ? endWidth : 2*endWidth + 2*arrowWidth + viewportWidth
    height: 28
    x: dragging ? Math.max(screenRect.x, Math.min(dragPosition.x, screenRect.x + screenRect.width - width))
                : screenRect.x + (rightEdge ? screenRect.width - width : 0)
    y: screenRect.y + Math.max(27, Math.min(dragging ? dragPosition.y-screenRect.y
                     : settings.controlStripY < 0 ? screenRect.height - height : settings.controlStripY,
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
                    c.fillStyle = "#ccc"; c.strokeStyle = "#222"; c.lineWidth = 1
                    c.beginPath()
                    c.moveTo(0, 0.5); c.lineTo(w - r - 0.5, 0.5)
                    c.arcTo(w - 0.5, 0.5, w - 0.5, r + 0.5, r)
                    c.lineTo(w - 0.5, h - r - 0.5)
                    c.arcTo(w - 0.5, h - 0.5, w - r - 0.5, h - 0.5, r)
                    c.lineTo(0, h - 0.5)
                    if (strip.collapsed) c.lineTo(0.5, h - 0.5), c.lineTo(0.5, 0.5)
                    c.fill(); c.stroke()
                    for (const x of [3, 6, 9]) {
                        c.fillStyle = "#777"; c.fillRect(x, 8, 1, h - 16)
                        c.fillStyle = "white"; c.fillRect(x + 1, 8, 1, h - 16)
                    }
                }
                Connections {target: strip; function onCollapsedChanged() {tab.requestPaint()}}
                // A canvas created while hidden paints nothing until asked again.
                onAvailableChanged: if (available) requestPaint()
                onVisibleChanged: if (visible) requestPaint()
            }
            MouseArea {
                id: tabMouse; anchors.fill: parent; hoverEnabled: true
                onPressed: function(mouse) {
                    strip.pressOrigin = mapToItem(strip.parent, mouse.x, mouse.y)
                    strip.initialPosition = Qt.point(strip.x, strip.y)
                    strip.dragPosition = strip.initialPosition
                    strip.dragFromRight = strip.rightEdge
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    let point = mapToItem(strip.parent, mouse.x, mouse.y)
                    let dx = point.x - strip.pressOrigin.x, dy = point.y - strip.pressOrigin.y
                    if (!strip.dragging && Math.abs(dx) + Math.abs(dy) < 5) return
                    strip.dragging = true
                    strip.dragPosition = Qt.point(strip.initialPosition.x + dx, strip.initialPosition.y + dy)
                }
                onReleased: {
                    if (!strip.dragging) {strip.settings.setControlStripCollapsed(!strip.collapsed); return}
                    // Dropping attaches the strip to whichever side edge is nearer.
                    let right = strip.x-strip.screenRect.x + strip.width / 2 > strip.screenRect.width / 2
                    let y = Math.round(strip.y-strip.screenRect.y)
                    strip.settings.setControlStripPosition(right ? Math.round(strip.screenRect.width - strip.width) : 0, y)
                    strip.dragging = false
                }
                onCanceled: strip.dragging = false
            }
            ToolTip.visible: tabMouse.containsMouse && !strip.dragging
            ToolTip.delay: 600
            ToolTip.text: strip.collapsed ? "Click to show the Control Strip; drag to move it."
                                          : "Click to collapse the Control Strip; drag it to either side edge."
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
