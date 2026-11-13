import QtQuick
import QtQuick.Controls

// Help Center (Help ▸ Help Center, Command-?): modeless, like Mac OS 9's help viewer. Topics on the
// left, the chosen topic on the right; links inside a topic open other topics. It opens by itself
// once, on a new account's first login. The words are our own, adapted from docs/first-session.md.
FocusScope {
    id: help
    property var moveManager: typeof manager === "undefined" ? null : manager
    onActiveFocusChanged: if (!activeFocus && moveManager) moveManager.finishShellWindowMove(help, false)
    signal closeRequested()
    signal raised()
    objectName: "help-window"
    width: 640
    height: 440
    property string topic: "welcome"
    readonly property var topics: [
        {id: "welcome", title: "Welcome", body:
            "<h3>Welcome to Platinum Reality</h3>" +
            "<p>Platinum Reality is a desktop for Linux in the spirit of the classic Mac OS. There is one " +
            "menu bar at the top of the screen, windows move by their title bars, and your disks and the " +
            "Trash sit on the desktop.</p>" +
            "<p>On your keyboard, the <b>Command</b> key (⌘) is the Windows or Super key.</p>" +
            "<p>Good places to start:</p><ul>" +
            "<li><a href=\"topic:menus\">The menu bar and Control Strip</a></li>" +
            "<li><a href=\"topic:apps\">Opening applications</a></li>" +
            "<li><a href=\"topic:keys\">Keyboard shortcuts</a></li></ul>" +
            "<p>Open this window again at any time with <b>Help ▸ Help Center</b> or Command-?.</p>"},
        {id: "menus", title: "The menu bar", body:
            "<h3>The menu bar and Control Strip</h3>" +
            "<p>The menu at the far left of the menu bar is the <b>Apple menu</b>. It has About This " +
            "Computer, Applications…, Calculator, the Control Panels, Key Caps, Note Pad, Puzzle, Scrapbook, Snapshot, " +
            "Recent Applications and Terminal. Control Panels ▸ Apple Menu Options chooses whether its folders have " +
            "submenus and how many recent applications it remembers.</p>" +
            "<p>The menus next to it belong to the application in front. On the desktop they are File, " +
            "Edit, View, Window, Special and Help.</p>" +
            "<p>The <b>application menu</b> at the far right shows which application is in front. Choose " +
            "another one there to switch to it, or hide the others. Drag down past the open menu to " +
            "tear it off as the <b>Application Switcher</b>, a palette of running applications: " +
            "click one to switch to it.</p>" +
            "<p>The <b>Control Strip</b> in the bottom-left corner has volume, brightness, monitors, " +
            "battery, Wi-Fi and Bluetooth. Its <b>Camera</b> module turns the camera off so that nothing at all " +
            "can use it (turning it on asks for an administrator's password), and its <b>Clipboard</b> module " +
            "lists what you copied recently: choose one to paste it again. Click its tab to fold it away. Drag the tab sideways to make the " +
            "strip shorter or longer. Drag it up or down to move the strip, and across the screen to put it " +
            "on the other side; holding down Option (Alt) while you drag always moves it.</p>"},
        {id: "computer", title: "About This Computer", body:
            "<h3>About This Computer</h3>" +
            "<p><b>Apple menu ▸ About This Computer</b> shows your system version, built-in RAM, " +
            "swap and available memory, with live resident-memory bars for running applications. " +
            "The last row, Platinum Reality, is the desktop itself with the Finder, as Mac OS listed " +
            "itself.</p>" +
            "<p>Bars compare the listed applications. Shared pages can appear in more than one app; " +
            "helper processes are not included. Measurements that cannot be read say Unavailable. " +
            "Linux has no classic largest unused block, so that line shows the memory available.</p>" +
            "<p>The values update while the window is open. Click the box at the far right of the title " +
            "bar (or double-click the title bar) to roll it up, the box beside it to fit every " +
            "application, and drag the bottom-right corner to resize it. Close it with its close box, " +
            "Command-W or Escape.</p>"},
        {id: "control-panel", title: "Control Panel", body:
            "<h3>Control Panel</h3>" +
            "<p><b>Apple menu ▸ Control Panel</b> opens one window of the everyday settings, after the " +
            "System 6 Control Panel, that you can keep open while you work. Click an icon on the left.</p><ul>" +
            "<li><b>General</b>: click dots to draw a desktop pattern, or use the arrows above the pattern " +
            "on the right to step through patterns; click that pattern to put it on the desktop. Also the " +
            "insertion point and menu blinking, the time and date (12 or 24 hours), the speaker volume " +
            "and the menu bar clock.</li>" +
            "<li><b>Keyboard</b> and <b>Mouse</b>: key repeat and mouse tracking.</li>" +
            "<li><b>Sound</b>: the speaker volume. The other icons open their Control Panels panel.</li></ul>"},
        {id: "note-pad", title: "Note Pad", body:
            "<h3>Note Pad</h3>" +
            "<p>Choose <b>Apple menu ▸ Note Pad</b> for eight pages of notes. Type on a page; it is saved " +
            "as you go and is there at your next login.</p>" +
            "<p>To turn the page, click the turned-up corner at the bottom left. Click the corner below " +
            "the fold to go back a page. The page number is beside it. Command-W puts Note Pad away.</p>"},
        {id: "key-caps", title: "Key Caps", body:
            "<h3>Key Caps</h3>" +
            "<p><b>Apple menu ▸ Key Caps</b> shows a keyboard with what each key types. Hold down Shift " +
            "or Option to see what the keys type with them. Press keys, or click them, and the text " +
            "appears in the strip at the top: a handy way to find a character.</p>"},
        {id: "scrapbook", title: "Scrapbook", body:
            "<h3>Scrapbook</h3>" +
            "<p><b>Apple menu ▸ Scrapbook</b> keeps text and pictures you want to use again. Copy " +
            "something, open the Scrapbook and choose <b>Edit ▸ Paste</b> (Command-V): it goes in front " +
            "of the item shown. <b>Edit ▸ Copy</b> (Command-C) puts the item shown back on the clipboard " +
            "to paste into any application; <b>Clear</b> (Delete) removes it. The scroll bar, or the left " +
            "and right arrow keys, move through the items. The Scrapbook is kept between logins.</p>"},
        {id: "calculator", title: "Calculator", body:
            "<h3>Calculator</h3>" +
            "<p>Choose <b>Apple menu ▸ Calculator</b>. Click the keypad or type numbers and " +
            "+, −, * or /. Return or = gives the result; calculations happen in entry order.</p>" +
            "<p>Type <b>%</b> for a percentage: 200 + 10 % = gives 220. " +
            "Backspace corrects the number you are entering; <b>C</b> or Escape clears everything.</p>" +
            "<p><b>Edit ▸ Copy</b> (Command-C) copies the displayed number; <b>Edit ▸ Paste</b> types a copied " +
            "number in. Command-W or the close " +
            "box puts the calculator away. It keeps your calculation until you log out.</p>"},
        {id: "apps", title: "Opening applications", body:
            "<h3>Opening applications</h3><ul>" +
            "<li><b>Apple menu ▸ Applications…</b> (Alt-D) lists every installed application. Type to " +
            "search, and press Return to open the one you picked.</li>" +
            "<li><b>Apple menu ▸ Recent Applications</b> has the ones you used last.</li>" +
            "<li><b>Apple menu ▸ Terminal</b> opens a terminal.</li>" +
            "<li>Double-click a document to open it. <b>File ▸ Open With…</b> picks another application " +
            "and can remember your choice.</li></ul>" +
            "<p>Command-Tab switches to the next application, and Command-Shift-Tab goes back. Command-H " +
            "hides the application in front. Command-Q quits it.</p>"},
        {id: "windows", title: "Windows", body:
            "<h3>Windows</h3>" +
            "<p>Click a window once to bring it forward. Drag the title bar to move it. The close box " +
            "is at the left of the title bar, and the zoom and WindowShade boxes are at the right.</p>" +
            "<p>Double-click a title bar to roll the window up into it, and again to roll it down. Set the " +
            "speed in <b>Appearance</b>.</p>" +
            "<p>The <b>Window</b> menu lists the open windows of the application in front.</p>" +
            "<h3>Workspaces</h3>" +
            "<p>Each screen has nine workspaces, and shows one at a time. Command-1 to Command-9 show " +
            "one on the screen you are using. Command-Control-1 to Command-Control-9 send the window in " +
            "front to another workspace, with its dialogs.</p>" +
            "<p>Alt-Tab goes to the next workspace that has windows, and Alt-Shift-Tab goes back. " +
            "Empty workspaces are skipped, so pick a new one by its number. The <b>Workspaces</b> " +
            "module in the Control Strip shows the number of the one showing and lists them all.</p>" +
            "<p>Choosing an application whose windows are on another workspace, with Command-Tab or the " +
            "Application menu, shows that workspace.</p>"},
        {id: "finder", title: "Files and the Finder", body:
            "<h3>Files and the Finder</h3>" +
            "<p><b>Hard Disk</b> on the desktop is your home folder. Folders open in their own windows " +
            "and remember where you left them.</p><ul>" +
            "<li>Drag to move. Drag with Option (Alt) held down to copy. Hold a drag over a folder for " +
            "a second and it springs open, so you can drop further in.</li>" +
            "<li>Cut, Copy and Paste work on files too.</li>" +
            "<li><b>Get Info</b> (Command-I) shows what an item is and where it lives.</li>" +
            "<li><b>File ▸ Label</b> colours the selected items: one of seven colours, or None. The " +
            "label stays with the file, and Get Info shows it.</li>" +
            "<li>Command-Delete moves the selection to the Trash. <b>Special ▸ Empty Trash…</b> " +
            "deletes it for good, and <b>Put Away</b> brings an item back.</li>" +
            "<li>Select a disk and press Command-E to eject it.</li></ul>"},
        {id: "settings", title: "Control Panels", body:
            "<h3>Control Panels</h3>" +
            "<p>Choose <b>Apple menu ▸ Control Panels</b>. Every control panel is in one window: pick one " +
            "from the list on the left. Changes take effect as you make them; close the window when " +
            "you are done. The Control Strip's menus open the same window at their panel.</p><ul>" +
            "<li><b>Appearance</b>: highlight colour, sounds, WindowShade and the Control Strip.</li>" +
            "<li><b>Apple Menu Options</b>: submenus on or off, and how many recent applications to remember.</li>" +
            "<li><b>Bluetooth</b>: pair, connect and forget devices.</li>" +
            "<li><b>Date &amp; Time</b>: a calendar, the time zone, and the menu bar clock (24-hour, " +
            "seconds, the date). Click the clock in the menu bar to see the date.</li>" +
            "<li><b>Desktop Picture</b>: choose a picture, then Set Desktop.</li>" +
            "<li><b>Energy Saver</b>: when the display turns off and the computer sleeps.</li>" +
            "<li><b>File Exchange</b>: which application opens each kind of document from the Finder and the desktop.</li>" +
            "<li><b>General Controls</b>: whether the desktop's icons show behind other applications, " +
            "how many times a chosen menu item blinks, and how fast the insertion point blinks.</li>" +
            "<li><b>Keyboard</b>: your keyboard layouts (check more than one for a keyboard menu in the menu bar), " +
            "and how fast keys repeat when held down.</li>" +
            "<li><b>Monitors</b>: turn screens on or off, and change resolution, rotation, size and " +
            "arrangement. A change goes back by itself after 15 seconds unless you click Keep, so a " +
            "blank screen fixes itself.</li>" +
            "<li><b>Mouse &amp; Trackpad</b>: tracking speed, scrolling direction, tap to click.</li>" +
            "<li><b>Network</b>: join and forget Wi-Fi networks, and TCP/IP: this computer's addresses, router " +
            "and name servers.</li>" +
            "<li><b>Sound</b>: alert sounds, microphones, speakers, Main Volume and the startup sound. " +
            "<b>Mac sounds only</b> removes the Arch and KDE desktop sounds (or downloads them again).</li></ul>"},
        {id: "snapshot", title: "Snapshot", body:
            "<h3>Snapshot</h3>" +
            "<p>Choose <b>Apple menu ▸ Snapshot</b> to take pictures with your camera. The camera's picture " +
            "shows in the window; click <b>Take Picture</b> (or press Return). Pictures are saved in the " +
            "Snapshot folder in your Pictures folder, named for when they were taken; <b>Show Pictures</b> " +
            "opens it.</p>" +
            "<p>If no camera is available, Snapshot says so. Check that the Control Strip's Camera module " +
            "is On, or connect a camera: the picture appears by itself when one does.</p>"},
        {id: "stickies", title: "Stickies", body:
            "<h3>Stickies</h3>" +
            "<p>Choose <b>Apple menu ▸ Stickies ▸ New Note</b> for a note on your desktop, ready " +
            "for typing. Notes are saved as you type and come back where you left them each time you " +
            "log in.</p><ul>" +
            "<li>Drag a note by its top strip to move it. Drag its bottom-right corner to resize it.</li>" +
            "<li>Double-click the strip, or click the box at its right, to roll the note up to its strip; " +
            "its first line still shows. Do it again to roll it down.</li>" +
            "<li>Right-click a note to change its color or text size, or choose <b>Keep in Front</b> to " +
            "keep it above your applications.</li>" +
            "<li>The close box deletes a note. If it has text, you are asked first.</li>" +
            "<li>Command-N makes another note and Command-W closes the one you are in. " +
            "<b>Hide All Notes</b> puts them away without losing them.</li></ul>"},
        {id: "keys", title: "Keyboard shortcuts", body:
            "<h3>Keyboard shortcuts</h3>" +
            "<p>The Command key (⌘) is the Windows or Super key. Command shortcuts work in every " +
            "application. Applications receive them as their usual Ctrl shortcuts.</p><ul>" +
            "<li>Command-C, X, V: Copy, Cut, Paste</li>" +
            "<li>Command-Z: Undo. Command-A: Select All</li>" +
            "<li>Command-S, O, N, W: Save, Open, New, Close</li>" +
            "<li>Command-Q: Quit. Command-H: Hide</li>" +
            "<li>Command-. (period): Cancel</li>" +
            "<li>Command-Tab: next application</li>" +
            "<li>Command-1 to 9: show that workspace. Command-Control-1 to 9: send the front window there</li>" +
            "<li>Alt-Tab: next workspace with windows. Alt-Shift-Tab: the previous one</li>" +
            "<li>Command-Option-Space: next keyboard layout, when more than one is checked</li>" +
            "<li>Command-Shift-S: take a screenshot</li>" +
            "<li>Command-Shift-3: a picture of the whole screen on the clipboard</li>" +
            "<li>Command-Shift-4 or Command-Shift-P: drag across an area for a picture of it on the clipboard</li>" +
            "<li>Command-?: this Help Center</li>" +
            "<li>Alt-D: Applications…</li>" +
            "<li>Ctrl-Alt-L: lock the screen</li></ul>" +
            "<p>An unassigned Command chord leaves a desktop text field unchanged. Command-N " +
            "creates another sticky note; Command-W closes a note or Control Panels.</p>" +
            "<p>In a terminal, Command-C and Command-V copy and paste, so Ctrl-C still stops a " +
            "program.</p>"},
        {id: "software", title: "Software and updates", body:
            "<h3>Software and updates</h3>" +
            "<p>Platinum Reality is built on Arch Linux. Install and update software with " +
            "<b>pacman</b> in the Terminal (Apple menu ▸ Terminal):</p><ul>" +
            "<li>Update everything: <tt>sudo pacman -Syu</tt></li>" +
            "<li>Install an application: <tt>sudo pacman -S</tt> followed by its name, for example " +
            "<tt>sudo pacman -S gimp</tt></li>" +
            "<li>Find one: <tt>pacman -Ss</tt> followed by a word</li></ul>" +
            "<p>New applications appear in Applications… as soon as they are installed.</p>"},
        {id: "leaving", title: "Sleep and shutting down", body:
            "<h3>Sleep, restart and shutting down</h3>" +
            "<p><b>Special ▸ Sleep</b>, <b>Restart</b> and <b>Shut Down</b> do what they say. Restart and " +
            "Shut Down first ask each application to close, so you can save your work. <b>Special ▸ Log " +
            "Out</b> returns to the Log In window.</p>" +
            "<p>The screen locks after a while without activity and when the computer sleeps. Unlock it " +
            "with your password.</p>"},
        {id: "trouble", title: "If something goes wrong", body:
            "<h3>If something goes wrong</h3><ul>" +
            "<li>An application that misbehaves can be quit from its menu or its close box. The desktop " +
            "keeps running.</li>" +
            "<li>If the desktop itself stops responding, press Ctrl-Alt-F3, log in, and type " +
            "<tt>pkill -f 'standalone-session.py --launcher'</tt>. The session closes and the Log In " +
            "window comes back.</li>" +
            "<li>Each session keeps its logs in <tt>~/.local/state/platinum-reality/session-runs</tt>. " +
            "Please include the newest folder when you report a problem at " +
            "<tt>github.com/psn-tch/platinum-reality</tt>.</li></ul>"}
    ]
    readonly property var current: topics.find(function(t) { return t.id === help.topic }) || topics[0]
    function show(id) {
        if (topics.some(function(t) { return t.id === id })) topic = id
        contents.contentY = 0
    }
    Keys.onEscapePressed: help.closeRequested()
    Keys.onUpPressed: list.decrementCurrentIndex()
    Keys.onDownPressed: list.incrementCurrentIndex()
    Rectangle {
        anchors.fill: parent
        color: "#ddd"; border.color: "#222"
        Rectangle {x: 1; y: 1; width: parent.width - 2; height: 1; color: "white"}
        Rectangle {x: 1; y: 1; width: 1; height: parent.height - 2; color: "white"}
        Rectangle {x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#888"}
        Rectangle {x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#888"}
    }
    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) { help.raised(); help.forceActiveFocus(); mouse.accepted = false }
    }
    Item {
        id: titleBar
        x: 3; y: 3; width: parent.width - 6; height: 18
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 20; y: 3 + index * 3; width: titleBar.width - 24; height: 1
                color: help.activeFocus ? "#888" : "#bbb"
            }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) {
                help.raised(); help.forceActiveFocus()
                if (help.moveManager) help.moveManager.beginShellWindowMove(help, mapToItem(null, mouse.x, mouse.y))
            }
            onReleased: if (help.moveManager) help.moveManager.finishShellWindowMove(help)
            onCanceled: if (help.moveManager) help.moveManager.finishShellWindowMove(help, false)
        }
        Rectangle {
            objectName: "help-close"
            x: 2; y: 3; width: 12; height: 12
            color: closeMouse.pressed ? "#888" : "#ddd"; border.color: "#222"
            Rectangle {x: 1; y: 1; width: 9; height: 1; color: closeMouse.pressed ? "#666" : "white"}
            Rectangle {x: 1; y: 1; width: 1; height: 9; color: closeMouse.pressed ? "#666" : "white"}
            MouseArea {id: closeMouse; anchors.fill: parent; onClicked: help.closeRequested()}
        }
        Rectangle {
            anchors.centerIn: parent
            width: title.implicitWidth + 12; height: 18; color: "#ddd"
            Text {id: title; anchors.centerIn: parent; text: "Help Center"; font.pixelSize: 12; font.bold: true}
        }
    }
    // The topic list: a white inset well, the chosen topic highlighted.
    Rectangle {
        id: well
        x: 10; y: titleBar.y + titleBar.height + 8; width: 170; height: parent.height - y - 10
        color: "white"; border.color: "#222"
        ListView {
            id: list
            objectName: "help-topics"
            anchors.fill: parent; anchors.margins: 1
            clip: true
            model: help.topics
            currentIndex: help.topics.findIndex(function(t) { return t.id === help.topic })
            onCurrentIndexChanged: if (currentIndex >= 0 && help.topics[currentIndex].id !== help.topic) help.show(help.topics[currentIndex].id)
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "help-topic-" + modelData.id
                width: ListView.view.width; height: 20
                color: modelData.id === help.topic ? (help.activeFocus ? "#333" : "#999") : "transparent"
                Text {
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 12; elide: Text.ElideRight
                    text: modelData.title; textFormat: Text.PlainText; font.pixelSize: 12
                    color: modelData.id === help.topic ? "white" : "black"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { help.raised(); help.forceActiveFocus(); help.show(modelData.id) }
                }
            }
        }
    }
    Rectangle {
        id: page
        x: well.x + well.width + 8; y: well.y; width: parent.width - x - 10; height: well.height
        color: "white"; border.color: "#222"
        Flickable {
            id: contents
            objectName: "help-page"
            x: 14; y: 6; width: parent.width - x - 18; height: parent.height - y - 2
            clip: true
            contentWidth: width; contentHeight: body.height + 12
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PlatinumScrollBar {parent: page; x: page.width - width - 1; y: 1; height: page.height - 2}
            Text {
                id: body
                objectName: "help-body"
                width: contents.width - 6
                text: help.current.body
                textFormat: Text.RichText; wrapMode: Text.WordWrap
                font.pixelSize: 12; lineHeight: 1.15
                linkColor: "#0000cc"
                onLinkActivated: function(link) { if (link.startsWith("topic:")) help.show(link.slice(6)) }
                HoverHandler { cursorShape: body.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
        }
    }
}
