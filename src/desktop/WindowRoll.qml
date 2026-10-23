import QtQuick

// Animate only the presentation clip. The full-sized client buffer and observed
// content geometry never change. Expanded popups must not remain clipped.
FocusScope {
    id: roll
    property bool collapsed: false
    property bool serverDecorated: true
    property real contentHeight: 430
    property int animationDuration: 240
    property bool initialized: false
    property real reveal: collapsed && serverDecorated ? 0 : 1
    readonly property bool animating: motion.running
    height: serverDecorated ? Math.round(23 + (contentHeight + 4) * reveal) : contentHeight
    clip: serverDecorated && (collapsed || animating)
    Component.onCompleted: initialized = true
    Behavior on reveal {
        enabled: roll.initialized
        NumberAnimation {id:motion;duration:Math.max(0,roll.animationDuration);easing.type:Easing.Linear}
    }
}
