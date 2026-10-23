import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    required property var sessionManager
    property var inputToken: 0
    property bool acceptEnabled: true
    // Destructive confirmations: the default ring and Return belong to Cancel.
    property bool cancelIsDefault: false
    // Alerts (confirmations, messages, pairing questions) play the alert sound when they open.
    property bool alertSound: false
    function releaseInput() {
        let token = inputToken
        inputToken = 0
        if (token) sessionManager.endShellModal(token)
    }
    modal: true
    focus: true
    x:sessionManager.primaryDisplayRect.x+Math.max(0,(sessionManager.primaryDisplayRect.width-width)/2)
    y:sessionManager.primaryDisplayRect.y+Math.max(0,(sessionManager.primaryDisplayRect.height-height)/2)
    padding: 16
    font.pixelSize: 12
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle {color:"transparent"}
    // A click outside a modal dialog does nothing but sound the alert, as on a Mac. The overlay
    // reports every press while a popup is open, so presses inside the dialog are marked first
    // (this area passes them on) and the overlay's report is judged after delivery. Only the
    // dialog holding focus answers.
    property bool pressedInside: false
    MouseArea {
        parent: dialog.contentItem ? dialog.contentItem.parent : null
        anchors.fill: parent; z: 100000
        onPressed: function(mouse) { dialog.pressedInside = true; mouse.accepted = false }
    }
    Connections {
        target: dialog.background ? dialog.background.Overlay.overlay : null
        enabled: dialog.visible
        function onPressed() {
            Qt.callLater(function() {
                const inside = dialog.pressedInside
                dialog.pressedInside = false
                if (!inside && dialog.holdsFocus && dialog.sessionManager.alertSounds) dialog.sessionManager.alertSounds.play()
            })
        }
    }
    // 9.2.2's alert frame (measured in Sammy's reference VM): a black outline with a black drop
    // shadow below and right, and a two-step bevel, #bbb then white above and left, #555 then #999
    // below and right, around a #ddd face.
    background: Rectangle {
        color: "#ddd"; border.color: "black"
        Rectangle {x:1;y:parent.height;width:parent.width;height:1;color:"black"}
        Rectangle {x:parent.width;y:1;width:1;height:parent.height;color:"black"}
        Rectangle {x:1;y:1;width:parent.width-2;height:1;color:"#bbb"}
        Rectangle {x:1;y:1;width:1;height:parent.height-2;color:"#bbb"}
        Rectangle {x:2;y:2;width:parent.width-4;height:1;color:"white"}
        Rectangle {x:2;y:2;width:1;height:parent.height-4;color:"white"}
        Rectangle {x:1;y:parent.height-2;width:parent.width-2;height:1;color:"#555"}
        Rectangle {x:parent.width-2;y:1;width:1;height:parent.height-2;color:"#555"}
        Rectangle {x:2;y:parent.height-3;width:parent.width-4;height:1;color:"#999"}
        Rectangle {x:parent.width-3;y:2;width:1;height:parent.height-4;color:"#999"}
    }
    header: Rectangle {
        implicitHeight: 30; color: "transparent"
        Text {anchors.centerIn:parent;text:dialog.title;textFormat:Text.PlainText;font.pixelSize:12;font.bold:false}
        Rectangle {x:8;y:parent.height-2;width:parent.width-16;height:1;color:"#888"}
    }
    footer: DialogButtonBox {
        padding: 16; spacing: 10
        buttonLayout: DialogButtonBox.MacLayout
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        delegate: PlatinumButton {
            objectName: dialog.objectName + (DialogButtonBox.buttonRole===DialogButtonBox.RejectRole?"-cancel":"-accept")
            enabled: DialogButtonBox.buttonRole===DialogButtonBox.RejectRole || dialog.acceptEnabled
            defaultAction: dialog.cancelIsDefault ? DialogButtonBox.buttonRole===DialogButtonBox.RejectRole
                                                  : DialogButtonBox.buttonRole===DialogButtonBox.AcceptRole
        }
        background: Rectangle {color:"transparent"}
    }
    // Return and Enter press the default (ringed) button, which is Cancel for destructive
    // commands; Command-period (Super, psn_tch's Command key) and Escape cancel (Finishing School
    // FS-D01/FS-E01). Only the dialog holding keyboard focus answers, so a dialog opened over
    // another takes the key. A focused field that handles Return itself sets `ownsReturn`.
    readonly property bool hasAcceptButton:
        (standardButtons & (Dialog.Ok | Dialog.Open | Dialog.Save | Dialog.Yes | Dialog.Apply)) !== 0
    readonly property var focusedItem: background ? background.Window.activeFocusItem : null
    readonly property bool holdsFocus: {
        for (let item = focusedItem; item; item = item.parent)
            if (item === contentItem || item === background || item === footer
                    || (contentItem && item === contentItem.parent)) return true
        return false
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: dialog.visible && dialog.holdsFocus && !(dialog.focusedItem && dialog.focusedItem.ownsReturn === true)
                 && (dialog.cancelIsDefault || (dialog.hasAcceptButton && dialog.acceptEnabled))
        onActivated: dialog.cancelIsDefault ? dialog.reject() : dialog.accept()
    }
    Shortcut {
        sequence: "Meta+."
        enabled: dialog.visible && dialog.holdsFocus
        onActivated: dialog.reject()
    }
    onAboutToShow: inputToken = sessionManager.beginShellModal()
    Connections {
        target: dialog
        function onOpened() { if (dialog.alertSound && dialog.sessionManager.alertSounds) dialog.sessionManager.alertSounds.play() }
    }
    onClosed: releaseInput()
}
