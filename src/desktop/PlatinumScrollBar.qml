import QtQuick
import QtQuick.Controls
import Frankenstein 1.0

// Mac OS 9.2.2's scroll bar with Smart Scrolling, always shown (never fades): the picture comes from
// the same code as the Qt style (ScrollBarArt); ScrollBar keeps dragging and paging. The scroll box
// travels the track above the two 14 px arrow boxes at the end, which step the view (repeating
// while held). Inactive windows show an empty strip.
ScrollBar {
    id: bar
    policy: ScrollBar.AlwaysOn
    readonly property int arrows: 30
    implicitWidth: horizontal ? 100 : 16
    implicitHeight: horizontal ? 16 : 100
    padding: 0
    rightPadding: horizontal ? arrows : 0
    bottomPadding: horizontal ? 0 : arrows
    minimumSize: 16 / Math.max(17, (horizontal ? width : height) - arrows)
    stepSize: 0.05
    contentItem: Item { implicitWidth: 16; implicitHeight: 16 }
    background: ScrollBarArt {
        orientation: bar.orientation
        position: bar.position
        size: bar.size
        active: Window.active
        enabled: bar.enabled
        pressedPart: arrowMouse.pressed ? arrowMouse.part : bar.pressed ? "box" : ""
        MouseArea {
            id: arrowMouse
            property string part: ""
            x: bar.horizontal ? parent.width - bar.arrows : 0
            y: bar.horizontal ? 0 : parent.height - bar.arrows
            width: bar.horizontal ? bar.arrows : parent.width
            height: bar.horizontal ? parent.height : bar.arrows
            enabled: bar.size < 1
            function step() { if (part === "sub") bar.decrease(); else bar.increase() }
            onPressed: function(mouse) {
                part = (bar.horizontal ? mouse.x : mouse.y) < 15 ? "sub" : "add"
                step(); repeat.interval = 400; repeat.start()
            }
            onReleased: repeat.stop()
            onCanceled: repeat.stop()
            Timer { id: repeat; repeat: true; onTriggered: { arrowMouse.step(); interval = 60 } }
        }
    }
}
