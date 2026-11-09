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
    // Sound Volume, as in 9.2.2: no menu, a vertical slider shown while the button is held over
    // the module; releasing sets the level. The volume is {available, value (0-100), muted}.
    property var volume: null
    // The user's own imported assets (scripts/import-macos9-assets.py): a module with an icon in
    // <personalFolder>/icons/control-strip/<command>.png shows it instead of our drawing.
    property string personalFolder: ""
    function personalIcon(command) {
        return personalFolder !== "" && /^[a-z]+$/.test(command)
            ? "file://" + encodeURI(personalFolder + "/icons/control-strip/" + command + ".png") : ""
    }
    signal sliderOpened()
    signal volumeChosen(int value)
    objectName: "control-strip"
    // Applications' tray items (StatusNotifierItem) follow the built-in modules:
    // {command: "status:<key>", name, icon, statusId}.
    property var extraModules: []
    readonly property rect screenRect: sessionManager.primaryDisplayRect
    // In the order 9.2.2 lists its modules (alphabetically by file), with Platinum Reality's own.
    readonly property var modules: [
        {command:"applications",name:"Applications"},
        {command:"appearance",name:"Appearance"}]
        .concat(battery && battery.present ? [{command:"battery",name:"Battery"}] : [])
        .concat([{command:"bluetooth",name:"Bluetooth"},
                 {command:"monitors",name:"Brightness"},
                 {command:"energy",name:"Energy Settings"}])
        .concat(handheld && handheld.available ? [{command:"handheld",name:"Handheld"}] : [])
        .concat([{command:"keychain",name:"Keychain"},
                 {command:"resolution",name:"Monitor Resolution"},
                 {command:"printer",name:"Printer Selector"},
                 {command:"sound",name:"Sound Volume",slider:true},
                 {command:"soundsource",name:"Sound Source"},
                 {command:"wifi",name:"Wi-Fi"}])
        .concat(extraModules)
    // The laptop's own battery (BatteryStatus); the module appears only when there is one.
    readonly property var battery: sessionManager.battery
    // Handheld Daemon (HandheldDaemon): shown while hhd runs on this computer.
    readonly property var handheld: sessionManager.handheld
    // Geometry measured pixel by pixel in the 9.2.2 reference VM (docs/fidelity/control-strip.md):
    // 24 px tall, flush with the screen edge; a 1 px black line at that edge and between
    // cells; a 13 px collapse box and 13 px scroll-arrow wells; modules of their own width (most are
    // 30 px, a 16 px icon at (2, 3) and the pop-up triangle 8 px from the right); an 18 px end tab.
    readonly property int stripHeight: 24
    readonly property int boxWidth: 13
    readonly property int tabWidth: 18
    // edge line, collapse box, line, back arrow, line, forward arrow, end tab (which starts with its line)
    readonly property int fixedWidth: 1 + boxWidth + 1 + boxWidth + 1 + boxWidth + tabWidth
    function moduleWidth(module) { return module && module.width ? module.width : 30 }
    // Where module `index` starts in the row of all modules, each followed by its black line.
    function moduleOffset(index) {
        let x = 0
        for (let i = 0; i < index && i < modules.length; ++i) x += moduleWidth(modules[i]) + 1
        return x
    }
    function spanOf(first, count) { return moduleOffset(first + count) - moduleOffset(first) }
    // How many whole modules, from the first, an outline `length` px long nearly covers.
    function modulesCovering(length) {
        let count = 0
        while (count < modules.length && spanOf(0, count + 1) - moduleWidth(modules[count]) / 2 <= length) ++count
        return count
    }
    readonly property int fittingModules: {
        let count = 0, used = fixedWidth
        while (count < modules.length && used + moduleWidth(modules[count]) + 1 <= screenRect.width)
            used += moduleWidth(modules[count++]) + 1
        return Math.max(1, count)
    }
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
    readonly property int viewportWidth: spanOf(firstModule, visibleModules)
    // Parts are placed from the screen edge outwards, mirrored on the right edge. Bindings rather
    // than a Row keep hit-testing correct before the next frame's polish.
    function place(offset, partWidth) { return rightEdge ? width - offset - partWidth : offset }
    width: collapsed ? tabWidth : fixedWidth + viewportWidth
    height: stripHeight
    x: screenRect.x + (rightEdge ? screenRect.width - width : 0)
    y: screenRect.y + Math.max(27, Math.min(settings.controlStripY < 0 ? screenRect.height - height
                                                                        : settings.controlStripY,
                                            screenRect.height - height))
    visible: settings.controlStripVisible
    enabled: !sessionManager.shellModal && !sessionManager.menuOpen

    // The strip's colours, as read from the reference: Platinum greys and the collapse box's lavender.
    readonly property var colours: ({"K": "#000000", "s": "#c0c0c0", "W": "#ffffff", "n": "#a0a0a0",
        "H": "#808080", "G": "#404040", "o": "#cccccc", "p": "#555555", "q": "#444444", "u": "#888888",
        "t": "#ccccff", "v": "#dddddd"})

    // Body: black behind everything, so the lines at the edge, the top, the bottom and between
    // cells are whatever the cells leave uncovered.
    Rectangle {
        visible: !strip.collapsed
        x: strip.place(0, width); width: parent.width - strip.tabWidth; height: parent.height
        color: "black"
    }
    Item {
        anchors.fill: parent
        Item {
            objectName: "control-strip-collapse"
            x: strip.place(1, width); y: 1
            visible: !strip.collapsed; width: strip.boxWidth; height: strip.height - 2
            RaisedCell {anchors.fill: parent; pressed: collapseMouse.pressed && collapseMouse.containsMouse}
            PixelArt {
                x: strip.rightEdge ? 3 : 2; y: 7; mirrored: strip.rightEdge
                offset: collapseMouse.pressed && collapseMouse.containsMouse ? 1 : 0
                rows: ["pppppppp", "pttttttt", "ptuuuuqW", "ptuuuuqW", "ptuuuuqW", "ptuuuuqW", "ptqqqqqW", "ptWWWWWW"]
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
            x: strip.place(1 + strip.boxWidth + 1, width); y: 1
            visible: !strip.collapsed; width: strip.boxWidth; height: strip.height - 2
            pointsBack: true
            active: strip.firstModule > 0
            onActivated: strip.firstModule -= 1
        }
        Item {
            x: strip.place(1 + 2 * (strip.boxWidth + 1), width); y: 1
            visible: !strip.collapsed; clip: true
            width: strip.viewportWidth; height: strip.height - 2
            Item {
                x: strip.rightEdge ? -(strip.spanOf(0, strip.modules.length) - strip.moduleOffset(strip.firstModule) - strip.viewportWidth)
                                   : -strip.moduleOffset(strip.firstModule)
                Repeater {
                    model: strip.modules
                    delegate: Item {
                        id: module
                        required property var modelData
                        required property int index
                        objectName: modelData.statusId !== undefined ? "control-strip-status-" + modelData.statusId
                                                                     : "control-strip-" + modelData.command
                        width: strip.moduleWidth(modelData); height: strip.height - 2
                        // Each module is followed by its black line, towards the end tab.
                        x: strip.rightEdge ? strip.spanOf(0, strip.modules.length) - strip.moduleOffset(index) - width - 1
                                           : strip.moduleOffset(index)
                        readonly property bool down: strip.openModule === modelData.command
                                                     || (moduleMouse.pressed && (moduleMouse.containsMouse || strip.sliderModule === module))
                        readonly property int shift: down ? 1 : 0
                        RaisedCell {anchors.fill: parent; pressed: module.down}
                        Image {
                            id: personal
                            objectName: "control-strip-personal-icon"
                            x: 2 + module.shift; y: 3 + module.shift; width: 16; height: 16
                            visible: !module.modelData.icon && status === Image.Ready
                            source: module.modelData.icon ? "" : strip.personalIcon(module.modelData.command)
                            smooth: false; cache: false
                        }
                        StripGlyph {x: 2 + module.shift; y: 3 + module.shift; width: 16; height: 16
                                    kind: module.modelData.statusId !== undefined ? "status"
                                          : module.modelData.command === "soundsource" ? "speaker"
                                          : module.modelData.command === "energy" ? "sleep" : module.modelData.command
                                    visible: !module.modelData.icon && !personal.visible
                                    level: strip.battery ? strip.battery.percent : -1
                                    charging: !!strip.battery && strip.battery.pluggedIn}
                        Image {
                            objectName: "control-strip-icon"
                            x: 2 + module.shift; y: 3 + module.shift; width: 16; height: 16
                            visible: Boolean(module.modelData.icon)
                            source: module.modelData.icon ? module.modelData.icon : ""
                            sourceSize: Qt.size(32, 32); smooth: true; fillMode: Image.PreserveAspectFit
                        }
                        // The pop-up triangle, 1 to 4 px wide over 8 rows, 8 px from the cell's right.
                        Repeater {
                            model: 8
                            Rectangle {
                                required property int index
                                x: module.width - 8 + module.shift; y: 7 + index + module.shift
                                width: index < 4 ? index + 1 : 8 - index; height: 1; color: "black"
                            }
                        }
                        MouseArea {
                            id: moduleMouse; anchors.fill: parent; hoverEnabled: true
                            onPressed: function(mouse) {
                                if (!module.modelData.slider) return
                                strip.sliderModule = module
                                strip.sliderValue = strip.volume && strip.volume.available
                                    ? (strip.volume.muted ? 0 : Math.round(strip.volume.value)) : -1
                                strip.sliderStart = strip.sliderValue
                                strip.sliderOpened()
                            }
                            onPositionChanged: function(mouse) {
                                if (strip.sliderModule === module && strip.sliderValue >= 0)
                                    strip.sliderValue = slider.valueAt(mapToItem(slider, mouse.x, mouse.y).y)
                            }
                            onReleased: {
                                if (strip.sliderModule !== module) return
                                if (strip.sliderValue >= 0 && strip.sliderValue !== strip.sliderStart)
                                    strip.volumeChosen(strip.sliderValue)
                                strip.sliderModule = null
                            }
                            onCanceled: strip.sliderModule = null
                            onClicked: if (!module.modelData.slider) strip.moduleRequested(module.modelData.command, module)
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
            x: strip.place(1 + 2 * (strip.boxWidth + 1) + strip.viewportWidth, width); y: 1
            visible: !strip.collapsed; width: strip.boxWidth; height: strip.height - 2
            pointsBack: false
            active: strip.firstModule + strip.visibleModules < strip.modules.length
            onActivated: strip.firstModule += 1
        }
        Item {
            objectName: "control-strip-tab"
            x: strip.collapsed ? 0 : strip.place(strip.width - strip.tabWidth, width)
            width: strip.tabWidth; height: strip.height
            // The end tab as measured: rounded by one-pixel steps, a ridged grip, and the lip at its end.
            PixelArt {
                mirrored: strip.rightEdge
                // Darkened while it is being dragged, as in 9.2.2.
                colors: strip.dragging ? Object.assign({}, strip.colours, {"o": "#888888", "W": "#aaaaaa", "u": "#555555"})
                                       : strip.colours
                rows: ["KKKKKKKKKK........",
                       "KKoooooooKK.......",
                       "KKoWWWWWWWKK......",
                       "KKoWoooooooKK.....",
                       "KKoWWoooooooKK....",
                       "KKoWopoooooooKK...",
                       "KKoWoooWooooooKK..",
                       "KKoWoooopooKKooKK.",
                       "KKoWWooooooKWKooKK",
                       "KKoWopoooooKWWKouK",
                       "KKoWoooWoooKWWKouK",
                       "KKoWoooopooKWWKouK",
                       "KKoWWooooooKWWKouK",
                       "KKoWopoooooKWWKouK",
                       "KKoWoooWoooKWWKouK",
                       "KKoWoooopooKWKooKK",
                       "KKoWWooooooKKooKK.",
                       "KKoWopooooooooKK..",
                       "KKoWoooooooooKK...",
                       "KKoWooooooooKK....",
                       "KKoWoooooooKK.....",
                       "KKouuuuuuuKK......",
                       "KKuqqqqqqKK.......",
                       "KKKKKKKKKK........"]
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
                        strip.outlineLength = Math.max(strip.tabWidth, Math.min(
                            strip.width + (strip.rightEdge ? -dx : dx), strip.fixedWidth + strip.spanOf(0, strip.fittingModules)))
                        strip.resizing = true
                    }
                }
                onReleased: {
                    const screen = strip.screenRect
                    if (strip.moving) {
                        strip.settings.setControlStripPosition(strip.outlineRight ? screen.width - strip.tabWidth : 0,
                                                               Math.round(strip.outlineY - screen.y))
                    } else if (strip.resizing) {
                        // Pulled back to the edge, the strip collapses to its tab; otherwise it
                        // keeps as many whole modules as the outline nearly covers.
                        if (strip.outlineLength < 2 * strip.tabWidth) {
                            strip.settings.setControlStripCollapsed(true)
                        } else {
                            const count = Math.max(1, Math.min(strip.fittingModules,
                                strip.modulesCovering(strip.outlineLength - strip.fixedWidth)))
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

    property Item sliderModule: null
    property int sliderValue: -1
    property int sliderStart: -1
    // The Sound Volume slider, measured in 9.2.2: a 21 px panel above the pressed module, 5 px in
    // from its left, running from 63 px above the strip into the module so the strip's top line
    // opens under it; a black track with grey sides; a 17x13 thumb with three grip lines. The
    // thumb's travel is from just under the track's top to the panel's bottom (0 at the bottom).
    Item {
        id: slider
        objectName: "control-strip-volume-slider"
        parent: strip.parent
        z: strip.z + 2
        visible: strip.sliderModule !== null
        readonly property point origin: strip.sliderModule
            ? strip.sliderModule.mapToItem(strip.parent, 0, 0) : Qt.point(0, 0)
        x: origin.x + 5; y: origin.y - 1 - 63
        width: 22; height: 66
        readonly property int thumbTop: 3
        readonly property int travel: 46
        function valueAt(y) { return Math.max(0, Math.min(100, Math.round((thumbTop + travel - (y - 6)) * 100 / travel))) }
        readonly property int thumbY: thumbTop + Math.round(travel * (100 - Math.max(0, strip.sliderValue)) / 100)
        Rectangle {width: 21; height: 66; color: "black"}
        Rectangle {x: 1; y: 1; width: 19; height: 64; color: "#dddddd"}
        // Shadow at the right.
        Rectangle {x: 21; y: 2; width: 1; height: 61; color: "black"}
        // Track: rounded top, grey left side, black edges, #aaa inside, white right side.
        Rectangle {x: 8; y: 2; width: 4; height: 1; color: "#aaaaaa"}
        Rectangle {x: 7; y: 3; width: 2; height: 1; color: "#aaaaaa"}
        Rectangle {x: 9; y: 3; width: 3; height: 1; color: "black"}
        Rectangle {x: 12; y: 3; width: 1; height: 1; color: "white"}
        Rectangle {x: 7; y: 4; width: 1; height: 58; color: "#aaaaaa"}
        Rectangle {x: 8; y: 4; width: 1; height: 58; color: "black"}
        Rectangle {x: 9; y: 4; width: 3; height: 58; color: "#aaaaaa"}
        Rectangle {x: 12; y: 4; width: 1; height: 58; color: "black"}
        Rectangle {x: 13; y: 4; width: 1; height: 58; color: "white"}
        Rectangle {x: 8; y: 62; width: 1; height: 1; color: "white"}
        Rectangle {x: 9; y: 62; width: 3; height: 1; color: "black"}
        Rectangle {x: 12; y: 62; width: 2; height: 1; color: "white"}
        Rectangle {x: 9; y: 63; width: 4; height: 1; color: "white"}
        PixelArt {
            objectName: "control-strip-volume-thumb"
            x: 2; y: slider.thumbY
            visible: strip.sliderValue >= 0
            colors: Object.assign({}, strip.colours, {"o": "#eeeeee", "p": "#cccccc", "b": "#aaaaaa",
                                                       "r": "#555555", "s": "#222222"})
            rows: [".KKKKKKKKKKKKKKK.",
                   "KopppppppppppppbK",
                   "KpbbbbbbbbbbbbbrK",
                   "KpbbopppppppbbbrK",
                   "KpbbbssssssssbbrK",
                   "KpbbopppppppbbbrK",
                   "KpbbbssssssssbbrK",
                   "KpbbopppppppbbbrK",
                   "KpbbbssssssssbbrK",
                   "KpbbbbbbbbbbbbbrK",
                   "KpbbbbbbbbbbbbbrK",
                   "KbrrrrrrrrrrrrrrK",
                   ".KKKKKKKKKKKKKKK."]
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

    // A raised Platinum cell (modules, collapse box): white inside its top and left, grey and dark
    // grey at its right and bottom; pressed, dark at the top and left and light at the right and bottom.
    component RaisedCell: Item {
        property bool pressed: false
        Rectangle {anchors.fill: parent; color: parent.pressed ? "#808080" : "#c0c0c0"}
        Item {
            anchors.fill: parent; visible: !parent.pressed
            Rectangle {x: 1; y: 1; width: parent.width - 3; height: 1; color: "white"}
            Rectangle {x: 1; y: 2; width: 1; height: parent.height - 4; color: "white"}
            Rectangle {x: parent.width - 2; y: 2; width: 1; height: parent.height - 3; color: "#a0a0a0"}
            Rectangle {x: 2; y: parent.height - 2; width: parent.width - 3; height: 1; color: "#a0a0a0"}
            Rectangle {x: parent.width - 1; y: 1; width: 1; height: parent.height - 1; color: "#808080"}
            Rectangle {x: 1; y: parent.height - 1; width: parent.width - 1; height: 1; color: "#808080"}
        }
        Item {
            anchors.fill: parent; visible: parent.pressed
            Rectangle {width: parent.width; height: 2; color: "#404040"}
            Rectangle {y: 2; width: 2; height: parent.height - 3; color: "#404040"}
            Rectangle {x: parent.width - 2; y: 2; width: 2; height: parent.height - 3; color: "#c0c0c0"}
            Rectangle {x: 1; y: parent.height - 1; width: parent.width - 1; height: 1; color: "#c0c0c0"}
            Rectangle {x: 2; y: parent.height - 2; width: parent.width - 4; height: 1; color: "#c0c0c0"}
        }
    }

    // Pixel drawings given as rows of palette letters ("." is clear), mirrored on the right edge.
    component PixelArt: Canvas {
        id: art
        property var rows: []
        property var colors: strip.colours
        property bool mirrored: false
        property int offset: 0
        width: rows.length ? rows[0].length + offset : 0; height: rows.length + offset
        antialiasing: false
        onRowsChanged: requestPaint()
        onColorsChanged: requestPaint()
        onMirroredChanged: requestPaint()
        onOffsetChanged: requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            let c = getContext("2d")
            c.reset()
            const w = rows.length ? rows[0].length : 0
            for (let y = 0; y < rows.length; ++y) {
                for (let x = 0; x < rows[y].length; ++x) {
                    const key = rows[y][x]
                    if (key === ".") continue
                    c.fillStyle = colors[key]
                    c.fillRect((mirrored ? w - 1 - x : x) + offset, y + offset, 1, 1)
                }
            }
        }
    }

    // A scroll arrow in its sunken well: grey at the top and left, white at the right and bottom.
    // The arrow is drawn grey while there is nothing more to scroll to.
    component StripArrow: Item {
        id: arrow
        property bool pointsBack: true
        property bool active: false
        signal activated()
        Rectangle {anchors.fill: parent; color: "#c0c0c0"}
        Rectangle {width: parent.width - 1; height: 1; color: "#808080"}
        Rectangle {y: 1; width: 1; height: parent.height - 2; color: "#808080"}
        Rectangle {x: parent.width - 1; y: 1; width: 1; height: parent.height - 1; color: "white"}
        Rectangle {x: 1; y: parent.height - 1; width: parent.width - 1; height: 1; color: "white"}
        PixelArt {
            x: 2; y: 6
            // Back points towards the screen edge: left on the left edge, right on the right edge.
            mirrored: arrow.pointsBack === strip.rightEdge
            colors: arrow.active ? Object.assign({}, strip.colours, {"p": "#000000", "v": "#ffffff"}) : strip.colours
            rows: ["....p...", "...pp...", "..pvpppp", ".pvvvvvp", "pvvvvvvp", "pvvvvvvp",
                   ".pvvvvvp", "..pvpppp", "...pp...", "....p..."]
        }
        MouseArea {anchors.fill: parent; enabled: arrow.active; onClicked: arrow.activated()}
    }
}
