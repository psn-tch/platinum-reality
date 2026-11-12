import QtQuick

// Puts one of the desktop's own windows (a control panel, a desk accessory, an Info window) on
// workspaces like an application's (Lily Hewitt, 2026-11-12; docs/modern-conveniences.md). It
// joins the workspace showing where it opens, waits there while another one shows, and moves
// with Command-Control-1…9. Carried to another screen while showing, by a drag or a display
// change, it joins the workspace showing there. Choosing it while it waits shows its workspace.
// The window hides with `away`: its `visible` stays whether it is open.
QtObject {
    id: member
    required property Item window
    readonly property var sessionManager: typeof manager === "undefined" ? null : manager
    // What the Control Strip's Workspaces menu calls it.
    property string name: window && typeof window.title === "string" ? window.title : ""
    // The screen's connector name and the workspace number; 0 until the window opens.
    property string screen: ""
    property int workspace: 0
    readonly property bool away: {
        if (!sessionManager || workspace <= 0) return false
        const showing = sessionManager.screenWorkspaces[screen]
        return showing !== undefined && showing !== workspace
    }
    function bounds() { return Qt.rect(window.x, window.y, window.width, window.height) }
    function place() {
        if (!sessionManager || !window.visible) return
        const now = sessionManager.screenAt(bounds())
        if (workspace > 0 && now === screen) return
        const showing = sessionManager.screenWorkspaces[now] || 1
        // A window waiting on a hidden workspace keeps its number wherever it is put.
        if (workspace === 0 || !away) workspace = showing
        screen = now
    }
    function moveTo(number) {
        if (number >= 1 && number <= 9 && window.visible) workspace = number
    }
    function bringForward() {
        if (!sessionManager || !away) return
        // Showing the workspace brings its front application window forward; come forward again
        // after that, once the window's own raise handlers have run.
        sessionManager.showWorkspaceOn(screen, workspace)
        Qt.callLater(function() { if (member.window.visible && !member.away) member.window.raised() })
    }
    property Connections watch: Connections {
        target: member.window
        function onVisibleChanged() {
            member.workspace = 0
            member.place()
        }
        function onXChanged() { member.place() }
        function onYChanged() { member.place() }
        function onRaised() { member.bringForward() }
    }
    Component.onCompleted: if (typeof desktop !== "undefined" && desktop && desktop.workspaceMembers) {
        desktop.workspaceMembers.push(member)
        place()
    }
    Component.onDestruction: if (typeof desktop !== "undefined" && desktop && desktop.workspaceMembers) {
        const at = desktop.workspaceMembers.indexOf(member)
        if (at >= 0) desktop.workspaceMembers.splice(at, 1)
    }
}
