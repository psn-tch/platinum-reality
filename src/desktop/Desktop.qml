import QtQuick
import QtQuick.Controls
import QtWayland.Compositor

Rectangle {
    id: desktop
    width: 1120; height: 760
    color: "#708b97"
    property int openMenu: -1
    property int openSubmenu: -1
    property var submenuEntries: []
    property real submenuY: 0
    // Control Strip module whose pop-up menu is open, and that module's rectangle.
    property string stripModule: ""
    property rect stripAnchor
    property string statusMessage: ""
    onStatusMessageChanged: if(statusMessage!=="")messageDialog.open()
    // Low battery, as a PowerBook's reserve-power warning: once at 10% and once at 5% while on
    // battery (the alert sound plays with the message). Plugging in re-arms both.
    property int batteryWarned: 101
    function checkBattery() {
        let battery=manager.battery
        if(!battery || !battery.present || battery.pluggedIn || battery.percent<0){batteryWarned=101;return}
        for(let level of [5,10]) if(battery.percent<=level && batteryWarned>level) {
            batteryWarned=level
            let left=battery.minutesRemaining>=0?" (about "+battery.minutesRemaining+" minutes left)":""
            statusMessage="Your battery is low: "+battery.percent+"%"+left+". Connect the power adapter soon, "+
                "or save your work; the computer will go to sleep when the battery runs out."
            return
        }
    }
    Connections {target:manager.battery;function onChanged(){desktop.checkBattery()}
                 Component.onCompleted:desktop.checkBattery()}
    property date clockTime: new Date()
    Timer {interval:1000;running:true;repeat:true;onTriggered:desktop.clockTime=new Date()}
    // The menu bar clock, as Date & Time sets it (AppearanceSettings.clock). Flashing separators
    // are drawn in the menu bar's own colour every other second, so the time never shifts.
    function clockText(time, options, showDate) {
        let c=options||({})
        if(showDate)return Qt.formatDate(time,"dddd, MMMM d, yyyy")
        let separator=c.flash && time.getSeconds()%2?"<font color=\"#dddddd\">:</font>":":"
        // The hour by hand: Qt's "h" alone is a 24-hour hour; only a format with AP makes it 12-hour.
        let hours=time.getHours()
        let parts=[c.hours24?(hours<10?"0":"")+hours:String(hours%12||12),Qt.formatTime(time,"mm")]
        if(c.seconds)parts.push(Qt.formatTime(time,"ss"))
        let text=parts.join(separator)+(c.hours24?"":(hours<12?" AM":" PM"))
        let day=(c.weekday?Qt.formatDate(time,"ddd")+" ":"")+(c.date?Qt.formatDate(time,"MMM d")+" ":"")
        return (day?day+" ":"")+text
    }
    // Clicking the clock shows the date for a few seconds, as in Mac OS 9.
    property bool clockShowsDate:false
    Timer {id:clockDate;interval:4000;onTriggered:desktop.clockShowsDate=false}
    property var broker: manager.menuBroker
    property var exportPopup: null
    property int exportedRoot: -1
    property var desktopFiles: manager.desktopItems
    property var appearance: manager.appearance
    // Mac OS 9 highlights menu titles and items with the Appearance accent: blue-violet by
    // default, or the graphite variation (sampled from 9.2 captures; historical review F01/F10).
    readonly property color menuAccent: desktop.appearance.accentColor
    // The desktop picture on each monitor by itself (psn_tch, 2026-11-06: one picture spread across
    // two monitors looked wrong), or the monitor's own picture (AppearanceSettings.monitorPictures).
    // The first is "desktop-wallpaper"; the others add their output's name.
    readonly property var wallpaperScreens: manager.displayOutputs.length > 0 ? manager.displayOutputs
        : [{name:"",x:0,y:0,width:desktop.width,height:desktop.height}]
    Repeater {
        model:desktop.wallpaperScreens
        Image {
            required property var modelData
            required property int index
            readonly property var own: modelData.name ? desktop.appearance.monitorPictures[modelData.name] : undefined
            readonly property string layout: own ? own.layout : desktop.appearance.wallpaperLayout
            objectName:index===0 ? "desktop-wallpaper" : "desktop-wallpaper-"+modelData.name
            x:modelData.x;y:modelData.y;width:modelData.width;height:modelData.height;clip:true
            source:own ? own.url : desktop.appearance.wallpaper
            fillMode:layout==="fit" ? Image.PreserveAspectFit : layout==="center" ? Image.Pad
                     : layout==="tile" ? Image.Tile : Image.PreserveAspectCrop
            asynchronous:true
            // A tiled pattern keeps its dots sharp.
            smooth:layout!=="tile"
            sourceSize:layout==="center" || layout==="tile"
                       ? Qt.size(0,0) : Qt.size(Math.min(4096,width*manager.hostScale),Math.min(4096,height*manager.hostScale))
        }
    }
    property var barEntries: [{label:"◆",kind:"apple"}].concat(manager.desktopActive ? [{label:"File",kind:"desktop-file"},{label:"Edit",kind:"desktop-edit"},{label:"View",kind:"desktop-view"},{label:"Window",kind:"window"},{label:"Special",kind:"special"},{label:"Help",kind:"help"}] : broker.menus.length > 0 ? broker.menus.filter(function(m){return m.visible && !m.separator}).map(function(m){return {label:m.label,kind:"export",id:m.id,children:m.children,enabled:m.enabled && !broker.interactionBlocked}}) : [{label:"Edit",kind:"app-edit"},{label:"Window",kind:"window"},{label:"Special",kind:"special"},{label:"Help",kind:"help"}])
    function updateDesktopSize(){if(desktopFiles)desktopFiles.setViewport(width,height)}
    function isDesktopDropPoint(px, py) {
        if(!desktopFiles.acceptsPoint(px,py) || manager.shellModal || manager.menuOpen)return false
        let strip=desktopControlStrip
        if(strip.visible && px>=strip.x && px<strip.x+strip.width && py>=strip.y && py<strip.y+strip.height)return false
        return true
    }
    function isWindowPoint(px, py) {
        for(let i=0;i<windowFrames.count;++i){
            let frame=windowFrames.itemAt(i)
            if(frame && frame.visible && px>=frame.x && px<frame.x+frame.width && py>=frame.y && py<frame.y+frame.height)return true
        }
        return false
    }
    function showDesktopContext(px, py, selected) {
        closeMenu()
        if(manager.shellModal || !manager.activateDesktop())return
        if(!selected)desktopFiles.clearSelection()
        let ready=!desktopFiles.busy
        let entries=selected ? [
            {label:"Open",action:"selection",enabled:ready},
            {label:"Open With…",action:"open-with",enabled:ready && desktopFiles.selectedDocument!==""},
            {separator:true},
            {label:"Get Info…",action:"get-info",enabled:desktopFiles.selectionCount===1},
            {separator:true},
            {label:"Rename…",action:"rename",enabled:ready && Boolean(desktopFiles.renameTarget.key)},
            {label:"Duplicate",action:"duplicate",enabled:ready && desktopFiles.canModifySelection},
            {separator:true},
            desktopFiles.selectedDrive!=="" ? {label:"Eject",action:"eject",enabled:manager.removableMedia.canEject(desktopFiles.selectedDrive)}
                                            : {label:"Move to Trash",action:"trash",enabled:ready && desktopFiles.canModifySelection}
        ] : [
            {label:"New Folder…",action:"new-folder",enabled:ready},
            {separator:true},
            {label:"Select All",action:"select-all",enabled:ready},
            {label:"Clean Up",action:"clean-up",enabled:ready},
            {label:"Open Desktop Folder",action:"desktop-folder",enabled:ready},
            {separator:true},
            {label:"Appearance…",action:"appearance",enabled:ready},
            {label:"Desktop Picture…",action:"desktop-picture",enabled:ready}
        ]
        manager.beginMenu()
        exportPopup=menuFactory.createObject(desktop,{rootMenu:true,objectName:"desktopContextMenu"})
        for(let entry of entries) {
            if(entry.separator){exportPopup.addItem(separatorFactory.createObject(exportPopup.contentItem));continue}
            let item=itemFactory.createObject(exportPopup.contentItem,{objectName:"context-"+entry.action,text:entry.label,enabled:entry.enabled})
            let action=entry.action
            item.triggered.connect(function(){desktop.dispatchShellCommand(action,0)})
            exportPopup.addItem(item)
        }
        exportPopup.popup(px,py)
    }
    function promptName(operation) {
        if(!manager.desktopActive || manager.shellModal || desktopFiles.busy)return
        let target=desktopFiles.renameTarget
        if(operation==="rename" && !target.key)return
        closeMenu()
        nameDialog.operation=operation
        nameDialog.targetKey=operation==="rename"?target.key:""
        nameDialog.title=operation==="rename"?"Rename":"New Folder"
        nameField.text=operation==="rename"?target.name:"untitled folder"
        nameDialog.open()
    }
    function dispatchShellCommand(action, applicationId) {
        // Closing the menu destroys its delegates; finish in the root's scope.
        closeMenu()
        if(action==="hide")manager.hideApplication()
        else if(action==="hide-others")manager.hideOtherApplications()
        else if(action==="show-all")manager.showAllApplications()
        else if(action==="app")activateListedApplication(applicationId)
        else if(action==="keyboard-layout")manager.input.set("keyboard","layout",applicationId)
        else if(action==="window")manager.activate(applicationId,true)
        else if(action==="launch")manager.launchApplication(applicationId)
        else if(action==="control-strip")desktop.appearance.apply(desktop.appearance.shadeDuration,!desktop.appearance.controlStripVisible)
        else if(action==="quick-controls")quickControls.showControls()
        else if(action==="finder")manager.showFinder()
        // Strip modules use the Quick Controls service so tests can substitute a fake helper.
        else if(action.startsWith("set-"))quickControls.service.set(action.slice(4),applicationId)
        else if(action==="status-activate")manager.statusItems.activate(applicationId)
        else if(action==="status-invoke"){let split=String(applicationId).lastIndexOf("#");manager.statusItems.invoke(applicationId.slice(0,split),parseInt(applicationId.slice(split+1)))}
        else if(action==="empty-trash")confirmEmptyTrash()
        else if(action==="selection")desktopFiles.openSelected()
        else if(action==="open-with")manager.requestOpenDocument(desktopFiles.selectedDocument, true)
        else if(action==="desktop-folder")manager.launchFinder(desktopFiles.directory)
        else if(action==="select-all")desktopFiles.selectAll()
        else if(action==="clean-up")desktopFiles.cleanUp()
        else if(action==="new-folder")promptName("create")
        else if(action==="rename")promptName("rename")
        else if(action==="duplicate")desktopFiles.duplicateSelected()
        else if(action==="label")desktopFiles.labelSelected(applicationId)
        else if(action==="trash")desktopFiles.trashSelected()
        else if(action==="get-info")showInfo()
        else if(action==="open")manager.launchFinder()
        else if(action==="terminal")manager.launchTerminal()
        else if(action==="puzzle")manager.launchPuzzle()
        else if(action==="screenshot")takeScreenshot()
        else if(action==="applications")applicationBrowser.browse("")
        else if(action==="appearance")appearanceDialog.edit()
        else if(action==="desktop-picture")desktopPictureDialog.edit()
        else if(action==="monitors")monitorsPanel.edit()
        else if(action==="network")networkPanel.edit()
        else if(action==="date-time")dateTimePanel.edit()
        else if(action==="keyboard")keyboardPanel.edit()
        else if(action==="energy")energyPanel.edit()
        else if(action==="mouse")mousePanel.edit()
        else if(action==="control-panels-window")controlPanels.show("")
        else if(action==="calculator")calculator.open()
        else if(action==="control-panel")classicControlPanel.open()
        else if(action==="note-pad")notePadWindow.open()
        else if(action==="scrapbook")scrapbookWindow.open()
        else if(action==="key-caps")keyCapsWindow.open()
        else if(action==="sticky-new")newStickyNote()
        else if(action==="sticky-toggle")manager.stickies.setHidden(!manager.stickies.hidden)
        else if(action==="sound")soundPanel.edit()
        else if(action==="bluetooth")bluetoothPanel.edit()
        else if(action==="bluetooth-add")bluetoothPanel.addDevice()
        else if(action==="bluetooth-power")bluetoothPanel.service.setPowered(applicationId===1)
        else if(action==="bluetooth-toggle")bluetoothPanel.toggle(bluetoothPanel.find(bluetoothPanel.devices,applicationId))
        else if(action==="wifi-join")wifiAlert.service.connectNetwork(applicationId)
        else if(action==="wifi-disconnect")wifiAlert.service.disconnectNetwork()
        else if(action==="wifi-scan")wifiAlert.service.scan()
        else if(action==="wifi-forget"){let n=wifiNetworks().find(function(w){return w.path===applicationId})
            if(n){forgetWifiDialog.path=n.path;forgetWifiDialog.name=n.name;forgetWifiDialog.open()}}
        else if(action==="edit"){let active=manager.windowMenu().find(function(w){return w.active});if(active)desktop.editCommand(active.id,applicationId)}
        else if(action==="show-clipboard")showClipboard()
        else if(action==="copy-names")copySelectedNames()
        else if(action==="accessory-edit"){let w=desktop.editAccessory();if(w){w.edit(applicationId);w.forceActiveFocus()}}
        else if(action==="quit")manager.quitSession()
        else if(action==="eject")manager.removableMedia.eject(desktopFiles.selectedDrive)
        else if(action==="handheld-set")manager.handheld.set(applicationId.key,applicationId.value)
        else if(action==="handheld-builtin")manager.handheld.setControllerOn(applicationId===1)
        else if(action==="handheld")handheldPanel.edit()
        else if(action==="sleep")manager.sleepComputer()
        else if(action==="restart")manager.restartComputer()
        else if(action==="shut-down")manager.shutDownComputer()
        else if(action==="about")aboutComputer.open()
        else if(action==="help")showHelp("welcome")
    }
    // Get Info windows, one per item (FS-G01): above the desktop icons, below application windows
    // (z 0.6 to 1), as Finder windows are.
    property var infoWindows: ({})
    property real infoOrder: 0.6
    Component {id:infoFactory;InfoWindow {}}
    // Edit menu commands for the application window with this id (see shellMenuEntries).
    signal editCommand(var id, int key)
    property var clipboardWindow: null
    function copySelectedNames() {
        let names=desktopFiles.selectedNames()
        if(names!=="" && manager.clipboard)manager.clipboard.copyText(names)
    }
    Component {id:clipboardFactory;ClipboardWindow {}}
    // Help Center: one modeless window, above application windows and under the menus like the
    // Clipboard. It opens by itself once, on a new account's first login (manager.takeWelcome).
    property var helpWindow: null
    Component {id:helpFactory;HelpWindow {}}
    function showHelp(topic) {
        if(!helpWindow) {
            let area=manager.primaryDisplayRect
            helpWindow=helpFactory.createObject(desktop,{z:4000,
                x:area.x+Math.max(8,Math.round((area.width-640)/2)),y:area.y+Math.max(30,Math.round((area.height-440)/3))})
            helpWindow.closeRequested.connect(function(){
                let window=desktop.helpWindow
                desktop.helpWindow=null
                window.destroy()
                desktop.forceActiveFocus()
            })
        }
        helpWindow.show(topic)
        helpWindow.forceActiveFocus()
    }
    Timer {interval:1500;running:true;onTriggered:if(manager.takeWelcome())desktop.showHelp("welcome")}
    function showClipboard() {
        if(!manager.clipboard)return
        if(!clipboardWindow) {
            // Low on the left, two thirds of the screen wide, where the 9.2.2 Finder opens it (above
            // the Control Strip here). Every application shares this clipboard, so the window
            // floats above application windows, under the menus.
            let area=manager.primaryDisplayRect
            let width=Math.min(area.width-16,Math.round(area.width*0.675))
            clipboardWindow=clipboardFactory.createObject(desktop,{clipboard:manager.clipboard,width:width,height:180,z:4000,
                x:area.x+2,y:area.y+area.height-180-34})
            clipboardWindow.closeRequested.connect(function(){
                let window=desktop.clipboardWindow
                desktop.clipboardWindow=null
                window.destroy()
                desktop.forceActiveFocus()
            })
        }
        clipboardWindow.forceActiveFocus()
    }
    function raiseInfo(window) {
        infoOrder=Math.min(0.999,infoOrder+0.0001)
        window.z=infoOrder
    }
    // File ▸ Label: None and the seven colours, the selection's own ticked.
    function labelEntries() {
        const current=desktopFiles.selectionLabel()
        return [{text:"None",action:"label",id:0,name:"label-0",checked:current===0},{separator:true,action:"separator"}]
            .concat(desktopFiles.labelChoices().map(function(l){
                return {text:l.name,action:"label",id:l.index,name:"label-"+l.index,checked:current===l.index,swatch:l.color}}))
    }
    function showInfo() {
        let details=desktopFiles.selectedDetails()
        if(!details.key)return
        let open=infoWindows[details.key]
        if(open){raiseInfo(open);open.forceActiveFocus();return}
        let count=Object.keys(infoWindows).length
        let area=manager.primaryDisplayRect
        let window=infoFactory.createObject(desktop,{details:details,
            x:area.x+140+24*(count%8),y:area.y+60+24*(count%8)})
        infoWindows[details.key]=window
        window.closeRequested.connect(function(){
            delete desktop.infoWindows[details.key]
            window.destroy()
            desktop.forceActiveFocus()
        })
        window.raised.connect(function(){desktop.raiseInfo(window)})
        raiseInfo(window)
        window.forceActiveFocus()
    }
    function formatSize(bytes) {
        if(bytes<1024)return bytes+" bytes"
        if(bytes<1024*1024)return Math.max(1,Math.round(bytes/1024))+"K"
        if(bytes<1024*1024*1024)return (bytes/1024/1024).toFixed(1)+" MB"
        return (bytes/1024/1024/1024).toFixed(2)+" GB"
    }
    function confirmEmptyTrash() {
        if(manager.shellModal || desktopFiles.busy)return
        let summary=desktopFiles.trashSummary()
        if(summary.items===0){statusMessage="The Trash is empty.";return}
        emptyTrashDialog.message="The Trash contains "+summary.items+(summary.items===1?" item":" items")+". It uses "+
            (summary.complete?"":"at least ")+formatSize(summary.bytes)+" of disk space. "+
            "Are you sure you want to permanently remove "+(summary.items===1?"this item?":"these items?")
        emptyTrashDialog.open()
    }
    // The Application menu lists applications by name, as 9.2.2 does (checked in psn_tch's 9.2.2 VM),
    // and Command-Tab / Command-Shift-Tab bring the next / previous one in that list forward at
    // once, wrapping, with no switcher palette.
    // In 9.2.2 control panels and desk accessories were applications of their own: open, each is
    // listed in the Application menu and the Application Switcher (psn_tch, 2026-11-07: Control Panel
    // wasn't). Their ids are "shell:<action>"; choosing one brings that window forward.
    function openShellApplications() {
        const windows=[{name:"Control Panels",action:"control-panels-window",item:controlPanels},
                       {name:"Control Panel",action:"control-panel",item:classicControlPanel},
                       {name:"Calculator",action:"calculator",item:calculator},
                       {name:"Key Caps",action:"key-caps",item:keyCapsWindow},
                       {name:"Note Pad",action:"note-pad",item:notePadWindow},
                       {name:"Scrapbook",action:"scrapbook",item:scrapbookWindow}]
        return windows.filter(function(w){return w.item && w.item.visible}).map(function(w){
            return {id:"shell:"+w.action,name:w.name,hidden:false,attention:false,shell:true,
                    active:manager.desktopActive && Boolean(w.item.front)}})
    }
    function applicationsByName() {
        const shell=openShellApplications()
        // Finder is active only while no built-in window is in front.
        const apps=manager.applications.map(function(a){
            return a.name==="Finder" && shell.some(function(w){return w.active}) ? Object.assign({},a,{active:false}) : a})
        return apps.concat(shell).sort(function(a,b){
            return String(a.name).localeCompare(String(b.name),undefined,{sensitivity:"base"}) || String(a.id).localeCompare(String(b.id))})
    }
    function activateListedApplication(id) {
        if(String(id).startsWith("shell:"))dispatchShellCommand(String(id).slice(6),0)
        else manager.activateApplication(id)
    }
    function switchApplication(step) {
        let apps=applicationsByName()
        if(apps.length<2)return
        let current=apps.findIndex(function(a){return a.active})
        let next=apps[((current<0?0:current)+step+apps.length)%apps.length]
        closeMenu()
        activateListedApplication(next.id)
    }
    Shortcut {sequence:"Meta+Tab";enabled:!manager.shellModal && !manager.nativeDragging;onActivated:desktop.switchApplication(1)}
    // Several spellings: a real keyboard's layout makes more than one match (see the screenshot keys).
    Shortcut {sequences:["Meta+Shift+Tab","Meta+Shift+Backtab","Meta+Backtab"];enabled:!manager.shellModal && !manager.nativeDragging
              onActivated:desktop.switchApplication(-1);onActivatedAmbiguously:activated()}
    // The keyboard menu, as 9.2.2's: in the menu bar while more than one Keyboard Layout is
    // checked (Keyboard control panel), listing them with the one in use checked; Command-Option-
    // Space moves to the next. It is the menu after the Application menu (keyboardMenuIndex).
    readonly property int keyboardMenuIndex:barEntries.length+1
    readonly property var keyboardLayouts:manager.input.keyboard.layouts
    readonly property bool keyboardMenuShown:keyboardLayouts.length>1
    // The menu's title: no flags here, the layout's short name ("DE", "CH-FR").
    function keyboardShortName(id) {
        const named={"ca(multix)":"CSA","us(dvorak)":"DV","us(intl)":"US-INTL","ch(fr)":"CH-FR"}
        return named[id] || String(id).toUpperCase()
    }
    function nextKeyboardLayout() {
        const layouts=keyboardLayouts
        if(layouts.length<2)return
        manager.input.set("keyboard","layout",layouts[(layouts.indexOf(manager.input.keyboard.layout)+1)%layouts.length])
    }
    Shortcut {sequences:["Meta+Alt+Space"];enabled:desktop.keyboardMenuShown && !manager.shellModal;onActivated:desktop.nextKeyboardLayout();onActivatedAmbiguously:activated()}
    // Shell menus in the 9.2.2 Finder layout (docs/historical-fidelity-review.md F07/F08).
    function shellMenuEntries(index) {
        if(index===keyboardMenuIndex)return keyboardLayouts.map(function(id){
            return {text:manager.input.layoutName(id),name:"keyboard-layout-"+id,action:"keyboard-layout",id:id,checked:id===manager.input.keyboard.layout}})
        if(index===barEntries.length)return [
            {text:"Hide "+manager.activeApplication,action:"hide",enabled:manager.applications.some(function(a){return !a.active && !a.hidden})},
            {text:"Hide Others",action:"hide-others",enabled:manager.applications.some(function(a){return !a.active && !a.hidden})},
            {text:"Show All",action:"show-all",enabled:manager.applications.some(function(a){return a.hidden})},
            {separator:true,action:"separator"}].concat(desktop.applicationsByName().map(function(a){return {text:a.name,action:"app",id:a.id,checked:a.active,marked:a.attention && !a.active,glyph:a.name==="Finder"?"disk":"application"}}))
        if(index===0){
            let recent=manager.recentApplications.map(function(a){return {text:a.name,action:"launch",id:a.id,glyph:"application"}})
            return [
                {text:"About This Computer",action:"about"},
                {separator:true,action:"separator"},
                {text:"Applications…",action:"applications",glyph:"application"},
                {text:"Calculator",action:"calculator",glyph:"application"},
                {text:"Control Panel",action:"control-panel",glyph:"application"},
                {text:"Control Panels",action:"control-panels",glyph:"application",children:[
                    {text:"All Control Panels…",action:"control-panels-window"},
                    {separator:true,action:"separator"},
                    {text:"Appearance…",action:"appearance"},
                    {text:"Bluetooth…",action:"bluetooth"},
                    {text:"Control Strip",action:"control-strip",checked:desktop.appearance.controlStripVisible},
                    {text:"Date & Time…",action:"date-time"},
                    {text:"Desktop Picture…",action:"desktop-picture"},
                    {text:"Energy Saver…",action:"energy"},
                    {text:"Keyboard…",action:"keyboard"},
                    {text:"Monitors…",action:"monitors"},
                    {text:"Mouse & Trackpad…",action:"mouse"},
                    {text:"Network…",action:"network"},
                    {text:"Quick Controls…",action:"quick-controls"},
                    {text:"Sound…",action:"sound"}]},
                {text:"Key Caps",action:"key-caps",glyph:"application"},
                {text:"Note Pad",action:"note-pad",glyph:"application"},
                {text:"Puzzle",action:"puzzle",glyph:"application"},
                {text:"Scrapbook",action:"scrapbook",glyph:"application"},
                {text:"Stickies",action:"sticky-notes",glyph:"application",children:[
                    {text:"New Note",action:"sticky-new"},
                    {separator:true,action:"separator"},
                    {text:manager.stickies.hidden?"Show All Notes":"Hide All Notes",action:"sticky-toggle",enabled:manager.stickies.count>0}]},
                {text:"Recent Applications",action:"recent-applications",glyph:"application",
                 children:recent.length?recent:[{text:"No Recent Applications",action:"none",enabled:false}]},
                {text:"Terminal",action:"terminal",glyph:"application"}]
        }
        let kind=barEntries[index]?barEntries[index].kind:""
        let ready=!desktopFiles.busy
        if(kind==="desktop-file")return [
            {text:"New Folder…",action:"new-folder",enabled:ready},
            {text:"Open",action:"selection",enabled:desktopFiles.selectionCount>0 && ready},
            {text:"Open With…",action:"open-with",enabled:desktopFiles.selectedDocument!=="" && ready},
            {text:"Move To Trash",action:"trash",enabled:desktopFiles.canModifySelection && ready},
            {text:"Close Window",action:"close-window",enabled:false},
            {separator:true,action:"separator"},
            {text:"Get Info",action:"get-info",enabled:desktopFiles.selectionCount===1},
            {text:"Label",action:"label-menu",enabled:desktopFiles.canModifySelection && ready,children:desktop.labelEntries()},
            {text:"Duplicate",action:"duplicate",enabled:desktopFiles.canModifySelection && ready},
            {text:"Rename…",action:"rename",enabled:Boolean(desktopFiles.renameTarget.key) && ready},
            {separator:true,action:"separator"},
            {text:"Open Desktop Folder",action:"desktop-folder"}]
        // Edit as in the 9.2.2 Finder (checked in psn_tch's 9.2.2 VM): no separator before Show Clipboard.
        // Copy with icons selected copies their names as text, one per line, as the Finder does.
        // For an application without menus of its own, each command is that application's shortcut,
        // sent as Command and the key shown (command_keys.hpp); Clear sends Delete.
        // A desk accessory in front that has edit commands (Scrapbook) takes Cut, Copy, Paste and Clear.
        let accessory=desktop.editAccessory()
        if(kind==="desktop-edit" && accessory)return [
            {text:"Undo",key:"⌘Z",action:"none",enabled:false},
            {separator:true,action:"separator"},
            {text:"Cut",key:"⌘X",action:"accessory-edit",id:"cut",name:"edit-cut",enabled:accessory.editCommands.cut===true},
            {text:"Copy",key:"⌘C",action:"accessory-edit",id:"copy",name:"edit-copy",enabled:accessory.editCommands.copy===true},
            {text:"Paste",key:"⌘V",action:"accessory-edit",id:"paste",name:"edit-paste",enabled:accessory.editCommands.paste===true},
            {text:"Clear",action:"accessory-edit",id:"clear",name:"edit-clear",enabled:accessory.editCommands.clear===true},
            {text:"Select All",key:"⌘A",action:"none",enabled:false},
            {text:"Show Clipboard",action:"show-clipboard",name:"show-clipboard",enabled:Boolean(manager.clipboard)}]
        if(kind==="desktop-edit")return [
            {text:"Undo",key:"⌘Z",action:"none",enabled:false},
            {separator:true,action:"separator"},
            {text:"Cut",key:"⌘X",action:"none",enabled:false},
            {text:"Copy",key:"⌘C",action:"copy-names",name:"edit-copy",enabled:desktopFiles.selectionCount>0 && Boolean(manager.clipboard)},
            {text:"Paste",key:"⌘V",action:"none",enabled:false},
            {text:"Clear",action:"none",enabled:false},
            {text:"Select All",key:"⌘A",action:"select-all",name:"edit-select-all"},
            {text:"Show Clipboard",action:"show-clipboard",name:"show-clipboard",enabled:Boolean(manager.clipboard)}]
        if(kind==="app-edit")return [
            {text:"Undo",key:"⌘Z",action:"edit",id:44,name:"edit-undo"},
            {separator:true,action:"separator"},
            {text:"Cut",key:"⌘X",action:"edit",id:45,name:"edit-cut"},
            {text:"Copy",key:"⌘C",action:"edit",id:46,name:"edit-copy"},
            {text:"Paste",key:"⌘V",action:"edit",id:47,name:"edit-paste"},
            {text:"Clear",action:"edit",id:-111,name:"edit-clear"},
            {text:"Select All",key:"⌘A",action:"edit",id:30,name:"edit-select-all"},
            {text:"Show Clipboard",action:"show-clipboard",name:"show-clipboard",enabled:Boolean(manager.clipboard)}]
        if(kind==="desktop-view")return [{text:"Clean Up",action:"clean-up"}]
        if(kind==="window"){
            let windows=manager.windowMenu().map(function(w){return {text:w.title,action:"window",id:w.id,checked:w.active}})
            return windows.length?windows:[{text:"No Open Windows",action:"none",enabled:false}]
        }
        // As in 9.2.2 (Eject and Erase Disk aside): Sleep, Restart and Shut Down only in a whole session.
        if(kind==="special")return [
            // As the 9.2.2 Finder's (reference VM, 2026-11-04): Empty Trash, a line, Eject, a line, power.
            {text:"Empty Trash…",key:"⇧⌘⌫",action:"empty-trash",name:"empty-trash",enabled:ready},
            {separator:true,action:"separator"},
            {text:"Eject",key:"⌘E",action:"eject",enabled:desktopFiles.selectedDrive!=="" && manager.removableMedia.canEject(desktopFiles.selectedDrive)},
            {separator:true,action:"separator"}].concat(manager.powerAvailable ? [
            {text:"Sleep",action:"sleep"},
            {text:"Restart",action:"restart"},
            {text:"Shut Down",action:"shut-down"},
            {separator:true,action:"separator"}] : []).concat([
            {text:"Log Out",action:"quit"}])
        return [{text:"Help Center",key:"⌘?",action:"help"},
            {separator:true,action:"separator"},
            {text:"About Platinum Reality…",action:"about"}]
    }
    // A strip menu lays itself out under the still pointer as it appears; the row that passes under
    // it isn't being pointed at. Until the pointer has had a moment, entering a row opens no submenu
    // (every Handheld menu capture showed Charge Limit's open by itself, and a click there was lost).
    property real stripMenuOpenedAt:0
    function openStripModule(module, anchor) {
        if(manager.shellModal)return
        if(stripModule===module){closeMenu();return}
        closeMenu(false);manager.beginMenu()
        let origin=anchor.mapToItem(desktop,0,0)
        stripAnchor=Qt.rect(origin.x,origin.y,anchor.width,anchor.height)
        stripMenuOpenedAt=Date.now()
        stripModule=module
        if(["sound","monitors","wifi"].includes(module))quickControls.service.refresh()
        if(module==="handheld")manager.handheld.refresh()
        // Opening the Wi-Fi module looks for networks, as later AirPort menus do.
        if(module==="wifi")wifiAlert.service.scan()
        else if(module==="battery" && manager.battery)manager.battery.refresh()
        else if(module.startsWith("status:"))manager.statusItems.prepare(module.slice(7))
    }
    // Hardware volume, mute and brightness keys step the same controls as the Control Strip,
    // through the Quick Controls service (which tests replace with a fake helper).
    property var pendingLevel: null
    function adjustLevel(control, delta, retried = false) {
        let service=quickControls.service
        let item=(service.state||({}))[control==="mute"?"volume":control]
        if(!item) {
            // Levels are not known yet: read them once, then apply this key press.
            if(!retried){pendingLevel={control:control,delta:delta};service.refresh()}
            return
        }
        if(!item.available)return
        // Read before changing: QML sees the service's live state, so set() alters item.
        const muted=Boolean(item.muted), current=Math.round(item.value)
        if(control==="mute") {
            service.set("mute",muted?0:1)
            showLevel("volume",current,!muted)
            return
        }
        let value=Math.max(control==="brightness"?5:0,Math.min(100,current+delta))
        if(control==="volume" && muted)service.set("mute",0)
        service.set(control,value)
        showLevel(control,value,false)
    }
    function showLevel(control, value, muted) {
        levelIndicator.control=control;levelIndicator.value=value;levelIndicator.muted=muted
        levelIndicator.visible=true;levelTimer.restart()
    }
    Connections {
        target:quickControls.service
        function onStateChanged() {
            let pending=desktop.pendingLevel
            if(!pending)return
            desktop.pendingLevel=null
            desktop.adjustLevel(pending.control,pending.delta,true)
        }
    }
    Shortcut {sequence:"Volume Up";onActivated:desktop.adjustLevel("volume",5)}
    Shortcut {sequence:"Volume Down";onActivated:desktop.adjustLevel("volume",-5)}
    Shortcut {sequence:"Volume Mute";autoRepeat:false;onActivated:desktop.adjustLevel("mute",0)}
    Shortcut {sequence:"Monitor Brightness Up";onActivated:desktop.adjustLevel("brightness",5)}
    Shortcut {sequence:"Monitor Brightness Down";onActivated:desktop.adjustLevel("brightness",-5)}
    // DBusMenu labels mark mnemonics with "_" ("__" is a literal underscore).
    function plainLabel(label) {
        return String(label).replace(/__/g,"\u0001").replace(/_/g,"").replace(/\u0001/g,"_")
    }
    // A tray item's menu (docs/tray-and-notifications.md): "Open <Title>" first, as strip modules
    // open their owner, then the application's own entries. Submenus are shown flattened under
    // a disabled heading, since strip menus do not cascade.
    function statusMenuEntries(key) {
        let items=manager.statusItems ? manager.statusItems.items : []
        let revision=manager.statusItems.menuRevision
        let item=items.find(function(i){return i.key===key})
        if(!item)return []
        let entries=item.itemIsMenu ? [] : [{text:"Open "+item.title,action:"status-activate",id:key,name:"status-open"}]
        let flatten=function(list,depth){
            for(let entry of list){
                if(entry.visible===false)continue
                if(entry.separator){entries.push({separator:true,action:"separator"});continue}
                let label=plainLabel(entry.label)
                let children=entry.children||[]
                entries.push({text:"   ".repeat(depth)+label,action:children.length?"none":"status-invoke",id:key+"#"+entry.id,
                              enabled:entry.enabled!==false && children.length===0,checked:Boolean(entry.checkable && entry.checked),
                              name:"status-item-"+label})
                flatten(children,depth+1)
            }
        }
        let menu=manager.statusItems.menu(key)
        if(entries.length && menu.length && !menu[0].separator)entries.push({separator:true,action:"separator"})
        flatten(menu,0)
        return entries.length?entries:[{text:"No Menu",action:"none",enabled:false,name:"status-empty"}]
    }
    // Mac OS 9 strip modules are pop-up menus whose first item opens the owning control panel
    // (V922 E12); the quick settings follow the AirPort and Sound modules' forms.
    function stripMenuEntries(module) {
        if(module.startsWith("status:"))return statusMenuEntries(module.slice(7))
        let values=quickControls.values||({})
        let open=[{text:"Open Quick Controls…",action:"quick-controls"},{separator:true,action:"separator"}]
        let nearest=function(levels,value){return levels.reduce(function(a,b){return Math.abs(b-value)<Math.abs(a-value)?b:a})}
        let unavailable=function(label){return {text:quickControls.service.busy?"Checking…":label+" Unavailable",action:"none",name:"status",enabled:false}}
        if(module==="applications") {
            let recent=manager.recentApplications.map(function(a){return {text:a.name,action:"launch",id:a.id,glyph:"application"}})
            return [{text:"Open Applications…",action:"applications"},{separator:true,action:"separator"},
                    {text:"Finder",action:"finder",glyph:"disk",checked:manager.activeApplication==="Finder"}]
                .concat(recent.length?[{separator:true,action:"separator"}].concat(recent):[])
        }
        if(module==="appearance")return [{text:"Open Appearance…",action:"appearance"},{separator:true,action:"separator"},
            {text:"Desktop Picture…",action:"desktop-picture"},{text:"Hide Control Strip",action:"control-strip"}]
        if(module==="sound") {
            open=[{text:"Open Sound…",action:"sound"},{separator:true,action:"separator"}]
            let volume=values.volume||({})
            if(!volume.available)return open.concat([unavailable("Sound")])
            let level=nearest([10,25,50,75,100],volume.value)
            return open.concat([{text:"Mute",action:"set-mute",id:volume.muted?0:1,name:"mute",checked:volume.muted},{separator:true,action:"separator"}],
                [10,25,50,75,100].map(function(l){return {text:"Volume "+l+"%",action:"set-volume",id:l,name:"volume-"+l,checked:!volume.muted && l===level}}))
        }
        if(module==="monitors") {
            let brightness=values.brightness||({})
            if(!brightness.available)return open.concat([unavailable("Brightness")])
            let level=nearest([10,25,50,75,100],brightness.value)
            return open.concat([10,25,50,75,100].map(function(l){return {text:"Brightness "+l+"%",action:"set-brightness",id:l,name:"brightness-"+l,checked:l===level}}))
        }
        if(module==="bluetooth")return bluetoothMenuEntries()
        if(module==="battery")return batteryMenuEntries()
        if(module==="handheld")return handheldMenuEntries()
        let radio=values.wifi||({})
        open=[{text:"Open Network…",action:"network"},{separator:true,action:"separator"}]
        if(!radio.available)return open.concat([unavailable("Wi-Fi")])
        let entries=open.concat([{text:"Wi-Fi: "+(radio.enabled?"On":"Off"),action:"none",name:"status",enabled:false},
                                 {text:(radio.enabled?"Turn Wi-Fi Off":"Turn Wi-Fi On"),action:"set-wifi",id:radio.enabled?0:1,name:"wifi"}])
        return radio.enabled?entries.concat(wifiMenuEntries()):entries
    }
    function wifiNetworks() {return wifiAlert.service && wifiAlert.service.available?wifiAlert.service.networks:[]}
    // The networks iwd sees, strongest first (docs/wifi.md): choosing one joins it, asking for a
    // password if it needs one. Signal bars and a lock are drawn at the right of each row.
    function wifiMenuEntries() {
        let service=wifiAlert.service
        if(!service || !service.available)return []
        let networks=wifiNetworks()
        let entries=[{separator:true,action:"separator"}]
        if(!networks.length)entries.push({text:service.scanning?"Looking for Networks…":"No Networks Found",action:"none",name:"wifi-none",enabled:false})
        for(let network of networks.slice(0,12))
            entries.push({text:network.name+(network.busy==="joining"?" — Joining…":""),action:network.connected?"none":"wifi-join",id:network.path,
                          name:"wifi-network-"+network.name,checked:network.connected,bars:network.bars,secure:network.secure,
                          enabled:network.busy===""})
        let connected=networks.find(function(n){return n.connected})
        entries.push({separator:true,action:"separator"})
        if(connected) {
            entries.push({text:"Disconnect from “"+connected.name+"”",action:"wifi-disconnect",name:"wifi-disconnect"})
            if(connected.known)entries.push({text:"Forget “"+connected.name+"”…",action:"wifi-forget",id:connected.path,name:"wifi-forget"})
        }
        entries.push({text:service.scanning?"Scanning…":"Scan for Networks",action:"wifi-scan",name:"wifi-scan",enabled:!service.scanning})
        return entries
    }
    // The Battery module, as a PowerBook's Battery Monitor: the charge and an estimate, nothing to set.
    // The Handheld module: Handheld Daemon's own choices for this device (HandheldDaemon), each a
    // checked line under its heading, as the Mac OS 9 strip modules listed their settings.
    function handheldMenuEntries() {
        let service=manager.handheld, state=service.state||({})
        let entries=[{text:"Open Handheld Daemon…",action:"launch",id:"hhd-ui.desktop"}]
            .concat(service.controllerSwitchable?[{text:"Handheld…",action:"handheld",name:"handheld-panel"}]:[])
            .concat([{separator:true,action:"separator"}])
        if(!service.available)return entries.concat([
            {text:service.busy?"Checking…":"Handheld Daemon Unavailable",action:"none",name:"status",enabled:false}])
        let ready=!service.busy
        let choice=function(item,value,text,name){return {text:text,action:"handheld-set",id:{key:item.key,value:value},
                                                           name:name,checked:value===item.value,enabled:ready}}
        // "Controller: Xbox", its choices beside it, as in the menu bar's submenus.
        let setting=function(title,item,name){
            if(!item)return []
            let current=item.options.find(function(o){return o.value===item.value})
            return [{text:title+": "+(current?current.title:item.value),action:"submenu",name:name,enabled:ready,
                     children:item.options.map(function(o){return choice(item,o.value,o.title,"handheld-"+o.value)})}]
        }
        // The built-in controller On or Off first, as the Bluetooth module's power: Off lets an external
        // controller be player 1.
        if(service.controllerSwitchable)entries=entries.concat([
            {text:"Built-in Controller: "+(service.controllerOn?"On":"Off"),action:"none",name:"handheld-builtin-status",enabled:false},
            {text:service.controllerOn?"Turn Built-in Controller Off":"Turn Built-in Controller On",action:"handheld-builtin",
             id:service.controllerOn?0:1,name:"handheld-builtin",enabled:ready},
            {separator:true,action:"separator"}])
        entries=entries.concat(setting("Controller",state.controller,"handheld-controller"))
        if(state.tdp){
            let t=state.tdp, levels=[t.min,8,12,15,20,25,t.max,t.value].filter(function(w,i,all){return w>=t.min && w<=t.max && all.indexOf(w)===i})
            levels.sort(function(a,b){return a-b})
            let power=levels.map(function(w){return choice(t,w,w+" W","handheld-tdp-"+w)})
            if(state.boost)power=power.concat([{separator:true,action:"separator"},
                {text:"TDP Boost",action:"handheld-set",id:{key:state.boost.key,value:!state.boost.value},
                 name:"handheld-boost",checked:state.boost.value,enabled:ready}])
            entries.push({text:"Power: "+t.value+" W"+(state.boost&&state.boost.value?" (Boost)":""),action:"submenu",
                          name:"handheld-power",enabled:ready,children:power})
        }
        return entries.concat(setting("Fan",state.fan,"handheld-fan"),setting("Charge Limit",state.chargeLimit,"handheld-charge"))
    }
    function batteryMenuEntries() {
        let battery=manager.battery
        if(!battery || !battery.present)return [{text:"No Battery",action:"none",name:"status",enabled:false}]
        let time=function(minutes){let h=Math.floor(minutes/60),m=minutes%60
            return (h>0?h+" hr ":"")+m+" min"}
        let detail=battery.state==="Full"?"Fully Charged"
            :battery.charging?(battery.minutesRemaining>=0?"Charging — "+time(battery.minutesRemaining)+" until full":"Charging")
            :battery.pluggedIn?"Plugged In, Not Charging"
            :battery.minutesRemaining>=0?"About "+time(battery.minutesRemaining)+" remaining":"On Battery"
        return [{text:"Battery: "+battery.percent+"%",action:"none",name:"battery-level",enabled:false},
                {text:detail,action:"none",name:"battery-detail",enabled:false}]
    }
    // The Bluetooth module (docs/bluetooth.md): power, then the paired devices; choosing one
    // connects or disconnects it, as in the Bluetooth menus of later systems.
    function bluetoothMenuEntries() {
        let bluetooth=bluetoothPanel
        let open=[{text:"Open Bluetooth…",action:"bluetooth"},{separator:true,action:"separator"}]
        if(!bluetooth.ready)return open.concat([{text:"Bluetooth Unavailable",action:"none",name:"status",enabled:false}])
        let entries=open.concat([{text:"Bluetooth: "+(bluetooth.powered?"On":"Off"),action:"none",name:"status",enabled:false},
                                 {text:bluetooth.powered?"Turn Bluetooth Off":"Turn Bluetooth On",action:"bluetooth-power",id:bluetooth.powered?0:1,name:"bluetooth-power"}])
        if(!bluetooth.powered)return entries
        entries.push({separator:true,action:"separator"})
        if(!bluetooth.devices.length)entries.push({text:"No Paired Devices",action:"none",name:"bluetooth-none",enabled:false})
        for(let device of bluetooth.devices) {
            let state=device.busy==="connecting"?" — Connecting…":device.busy==="disconnecting"?" — Disconnecting…"
                     :device.connected && device.battery>=0?" — "+device.battery+"%":""
            entries.push({text:device.name+state,action:"bluetooth-toggle",id:device.path,name:"bluetooth-device-"+device.name,
                          checked:device.connected,enabled:device.busy===""})
        }
        return entries.concat([{separator:true,action:"separator"},{text:"Add Device…",action:"bluetooth-add",name:"bluetooth-add"}])
    }
    function openShellSubmenu(index, entries, sceneY) {
        submenuEntries=entries;submenuY=sceneY;openSubmenu=index
    }
    // Command-Shift-3 puts the whole desktop on the clipboard; Command-Shift-4 (or Command-Shift-P)
    // shows a crosshair, and the area dragged across goes on the clipboard (Mac OS's picture-to-clipboard keys).
    // The desktop is captured first, so the crosshair is never in the picture.
    function copyScreenshot(area) {
        if(manager.shellModal || manager.nativeDragging || screenshotArea.visible)return
        closeMenu()
        if(!manager.screenshots.capture()){statusMessage=manager.screenshots.error;return}
        if(area){screenshotArea.begin();return}
        if(!manager.screenshots.copy())statusMessage=manager.screenshots.error
        manager.screenshots.discard()
    }
    function takeScreenshot() {
        if(manager.shellModal || manager.nativeDragging)return
        closeMenu()
        if(manager.screenshots.capture())screenshotDialog.edit()
        else statusMessage=manager.screenshots.error
    }
    onWidthChanged:updateDesktopSize()
    onHeightChanged:updateDesktopSize()
    Component.onCompleted:{updateDesktopSize();if(manager.desktopActive)forceActiveFocus()}
    function menuX(index) {
        let entry=barItems.itemAt(index)
        return entry ? entry.mapToItem(desktop,0,0).x : 8
    }
    // The Appearance sound track (InterfaceSounds); silent unless the user imported one.
    function interfaceSound(tag) { if(manager.interfaceSounds)manager.interfaceSounds.play(tag) }
    function closeMenu(endTracking = true, sound = "mnuc") {
        let previous=exportPopup
        if(sound!=="" && (openMenu>=0 || previous))interfaceSound(sound)
        exportPopup=null;openMenu=-1;exportedRoot=-1;openSubmenu=-1;stripModule=""
        if(previous){previous.close();previous.destroy()}
        if(endTracking)manager.endMenu()
    }
    function populate(menu, entries, generation, revision) {
        for (let entry of entries) {
            if (!entry.visible) continue
            if (entry.separator) {menu.addItem(separatorFactory.createObject(menu.contentItem));continue}
            if (entry.children.length > 0) {
                let submenu=menuFactory.createObject(menu,{title:entry.label,rootMenu:false,enabled:entry.enabled})
                populate(submenu,entry.children,generation,revision);menu.addMenu(submenu)
            } else {
                let item=itemFactory.createObject(menu.contentItem,{objectName:"menu-command-"+entry.id,text:entry.label,enabled:entry.enabled,checkable:entry.checkable,checked:entry.checked})
                let identity=entry.id
                item.triggered.connect(function(){broker.invoke(identity,generation,revision);desktop.closeMenu(true,"mnus")})
                menu.addItem(item)
            }
        }
    }
    function clearEntries(menu) {
        // Qt Quick Menu has takeItem/takeMenu; QWidget's clear() is not its API.
        while(menu.count > 0) {
            let index=menu.count-1
            let submenu=menu.menuAt(index)
            if(submenu) {menu.takeMenu(index).destroy()}
            else {let item=menu.takeItem(index);if(item)item.destroy()}
        }
    }
    function showMenu(index) {
        if(manager.shellModal || (index < barEntries.length && barEntries[index].enabled===false))return
        if (openMenu === index) closeMenu()
        else {
            closeMenu(false,"");manager.beginMenu();openMenu=index;interfaceSound("mnuo")
            if(index < barEntries.length && barEntries[index].kind === "export") {
                let entry=barEntries[index];exportedRoot=entry.id
                exportPopup=menuFactory.createObject(desktop,{rootMenu:true})
                populate(exportPopup,entry.children,broker.generation,broker.revision)
                exportPopup.popup(menuX(index),menuBar.y+26);broker.prepare(entry.id)
            }
        }
    }
    Component {id:menuFactory;Menu {id:exportedMenu;property bool rootMenu:false; width:240;padding:2; font.pixelSize:12;enabled:!desktop.broker.busy && !desktop.broker.interactionBlocked
        background:Rectangle{color:"#ddd";border.color:"#222"}
        onClosed:if(rootMenu && desktop.exportPopup===exportedMenu)desktop.closeMenu()
    }}
    Component {id:itemFactory;MenuItem {id:nativeItem;height:24
        contentItem:Text{text:nativeItem.text;textFormat:Text.PlainText;elide:Text.ElideRight;leftPadding:20;rightPadding:15;verticalAlignment:Text.AlignVCenter;font.pixelSize:12;color:!nativeItem.enabled?"#888":nativeItem.highlighted?"white":"black"}
        background:Rectangle{color:nativeItem.highlighted?desktop.menuAccent:"#ddd"}
    }}
    Component {id:separatorFactory;MenuSeparator{padding:3;contentItem:Rectangle{implicitWidth:200;implicitHeight:1;color:"#888"}}}
    Connections {target:broker;function onChanged(){
        if(desktop.exportPopup && desktop.exportedRoot>=0){
            let entry=broker.menus.find(function(e){return e.id===desktop.exportedRoot})
            if(!entry){desktop.closeMenu();return}
            desktop.clearEntries(desktop.exportPopup);desktop.populate(desktop.exportPopup,entry.children,broker.generation,broker.revision)
        }
    }}
    // In 9.2.2 a key closes an open (sticky) menu, choosing nothing; there is no menu keyboard
    // navigation (checked in psn_tch's 9.2.2 VM). The desktop's own menus and Control Strip menus
    // do the same (the manager swallows that key). Exported application menus keep Qt's
    // keyboard handling.
    Binding {target:manager;property:"shellMenuOpen";value:(desktop.openMenu>=0 && desktop.exportedRoot<0) || desktop.stripModule!==""}
    Connections {target:manager;function onShellMenuKeyPressed(){desktop.closeMenu()}}
    Shortcut {sequence:"Meta+H";enabled:!manager.shellModal;onActivated:manager.hideApplication()}
    // Command-? opens the Help Center, as on a Mac (? is Shift-/ on most layouts).
    // A shifted chord has several spellings (Command-Shift-3 is also Command-#). A real keyboard's
    // layout offers Qt all of them, so more than one listed sequence matches and Qt reports the
    // match as ambiguous instead of activating it: both signals do the same (psn_tch, 2026-11-02:
    // the screenshot keys did nothing; the tests' synthetic keys carry no layout and matched once).
    Shortcut {sequences:["Meta+?","Meta+Shift+?","Meta+Shift+/"];autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging
              onActivated:{desktop.closeMenu();desktop.showHelp("welcome")}
              onActivatedAmbiguously:activated()}
    // Frees a pointer that a game locked or confined. Only bound while one is captured, so the
    // chord otherwise reaches the application. See docs/relative-pointer-plan.md.
    Shortcut {sequence:"Ctrl+Alt+Escape";autoRepeat:false;enabled:manager.pointerCaptured;onActivated:manager.releasePointerCapture()}
    // Super is Command (psn_tch, D06); Command-D stays Duplicate, so Applications keeps psn_tch's Alt+D.
    // The other Alt forms are their earlier explicit requests, kept as aliases.
    Shortcut {sequence:"Alt+D";autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging;onActivated:{desktop.closeMenu();applicationBrowser.browse("")}}
    Shortcut {sequences:["Meta+Q","Alt+Q"];autoRepeat:false;enabled:!manager.shellModal && !manager.menuOpen && !manager.nativeDragging;onActivated:manager.requestQuitActiveApplication()}
    Shortcut {sequences:["Meta+Shift+S","Alt+Shift+S"];autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging;onActivated:desktop.takeScreenshot()}
    // Shift-3 and Shift-4 arrive as # and $ on most layouts.
    Shortcut {sequences:["Meta+Shift+3","Meta+Shift+#","Meta+#"];autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging
              onActivated:desktop.copyScreenshot(false);onActivatedAmbiguously:activated()}
    Shortcut {sequences:["Meta+Shift+4","Meta+Shift+$","Meta+$"];autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging
              onActivated:desktop.copyScreenshot(true);onActivatedAmbiguously:activated()}
    // Command-Shift-P does the same as Command-Shift-4 (psn_tch, 2026-11-04). Command-P stays Print.
    Shortcut {sequence:"Meta+Shift+P";autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging
              onActivated:desktop.copyScreenshot(true);onActivatedAmbiguously:activated()}
    Shortcut {sequence:"Escape";enabled:manager.desktopActive && !manager.shellModal && (desktopFiles.dragging || desktopFiles.selecting);onActivated:desktopFiles.selecting ? desktopFiles.cancelSelection() : desktopFiles.cancelDrag()}
    // Special ▸ Empty Trash… (Command-Shift-Delete), asking first as the menu item does.
    Shortcut {sequence:"Meta+Shift+Backspace";autoRepeat:false;enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy
              onActivated:desktop.confirmEmptyTrash();onActivatedAmbiguously:activated()}
    Shortcut {sequences:["Meta+I","Ctrl+I"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && desktopFiles.selectionCount===1;onActivated:desktop.showInfo()}
    Shortcut {sequences:["Meta+O","Ctrl+O"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy;onActivated:desktopFiles.openSelected()}
    // Typing on the active desktop selects an icon by name (DesktopItems::typeSelect); letters
    // typed within a second of each other build one name, as in the 9.2.2 Finder.
    property string typedName: ""
    Timer {id:typedNameTimer;interval:1000;onTriggered:desktop.typedName=""}
    Keys.onPressed:function(event){
        if(!manager.desktopActive || manager.menuOpen || manager.shellModal || desktopFiles.busy ||
           (event.modifiers & (Qt.ControlModifier|Qt.AltModifier|Qt.MetaModifier)) ||
           event.text.length!==1 || event.text<" " || event.text==="\u007f")return
        desktop.typedName+=event.text
        typedNameTimer.restart()
        desktopFiles.typeSelect(desktop.typedName)
        event.accepted=true
    }
    Shortcut {sequences:["Meta+C","Ctrl+C"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && desktopFiles.selectionCount>0;onActivated:desktop.copySelectedNames()}
    Shortcut {sequences:["Meta+A","Ctrl+A"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal;onActivated:desktopFiles.selectAll()}
    Shortcut {sequences:["Meta+Shift+N","Ctrl+Shift+N"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy;onActivated:desktop.promptName("create")}
    Shortcut {sequence:"F2";enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy;onActivated:desktop.promptName("rename")}
    Shortcut {sequences:["Meta+E","Ctrl+E"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && desktopFiles.selectedDrive!=="";onActivated:manager.removableMedia.eject(desktopFiles.selectedDrive)}
    Shortcut {sequences:["Meta+D","Ctrl+D"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy && desktopFiles.canModifySelection;onActivated:desktopFiles.duplicateSelected()}
    Shortcut {sequences:["Meta+Backspace","Ctrl+Backspace"];enabled:manager.desktopActive && !manager.menuOpen && !manager.shellModal && !desktopFiles.busy && desktopFiles.canModifySelection;onActivated:desktopFiles.trashSelected()}
    Connections {
        target: manager
        function onErrorOccurred(message) { desktop.statusMessage = message }
        function onOpenWithRequested(document) {applicationBrowser.browse(document)}
        function onControlPanelRequested(action) {desktop.dispatchShellCommand(action,0)}
        function onFocusDesktop() {
            let w=desktop.frontShellWindow
            if(w && w.visible && w.front)w.forceActiveFocus();else desktop.forceActiveFocus()
        }
        function onContextChanged() { if(!manager.menuOpen && (desktop.openMenu>=0 || desktop.exportPopup || desktop.stripModule!==""))desktop.closeMenu(false) }
    }
    // While a menu bar or Control Strip menu is open, a click anywhere else (a window included)
    // only closes it, as in Mac OS 9. Above windows; below the strip, the menu bar and menus.
    MouseArea {
        objectName:"menu-dismiss";anchors.fill:parent;z:7999;acceptedButtons:Qt.AllButtons
        visible:(desktop.openMenu>=0 && desktop.exportedRoot<0) || desktop.stripModule!==""
        onPressed:desktop.closeMenu()
    }
    MouseArea { objectName:"desktopBackground";anchors.fill: parent; enabled:!manager.shellModal;acceptedButtons:Qt.LeftButton|Qt.RightButton
        onPressed:function(mouse){
            if(desktop.isWindowPoint(mouse.x,mouse.y)){mouse.accepted=false;return}
            desktop.closeMenu()
            if(!manager.activateDesktop()){mouse.accepted=false;return}
            if(mouse.button===Qt.RightButton){desktopFiles.clearSelection();desktop.showDesktopContext(mouse.x,mouse.y,false)}
            else if(!desktopFiles.beginSelection(Math.round(mouse.x),Math.round(mouse.y),Boolean(mouse.modifiers & (Qt.ShiftModifier|Qt.ControlModifier))))mouse.accepted=false
        }
        onPositionChanged:function(mouse){if(pressed && desktopFiles.selecting)desktopFiles.updateSelection(Math.round(mouse.x),Math.round(mouse.y))}
        onReleased:function(mouse){if(mouse.button===Qt.LeftButton)desktopFiles.endSelection()}
        onCanceled:desktopFiles.cancelSelection()
    }
    Rectangle {
        objectName:"desktop-selection-rectangle";z:0.5
        visible:desktopFiles.selecting && (width>1 || height>1)
        x:desktopFiles.selectionRect.x;y:desktopFiles.selectionRect.y
        width:desktopFiles.selectionRect.width;height:desktopFiles.selectionRect.height
        color:"#18333366";border.color:"#222";border.width:1
    }
    Repeater {
        model: desktop.desktopFiles
        delegate: Item {
            id: desktopEntry
            required property string entryKey
            required property string entryName
            required property string entryKind
            required property int itemX
            required property int itemY
            required property bool isSelected
            required property string labelColor
            objectName:"desktop-entry-"+entryName
            // General Controls ▸ Show Desktop when in background: off, the icons go while another
            // application is in front, and come back with the Finder.
            visible:desktop.appearance.general.showDesktop || manager.desktopActive || manager.activeApplication==="Finder"
            z:desktopFiles.dragging && desktopEntry.isSelected ? 10000 : 0
            x:itemX;y:itemY;width:100;height:76
            DesktopIcon {anchors.horizontalCenter:parent.horizontalCenter;y:3;kind:desktopEntry.entryKind;selected:desktopEntry.isSelected && manager.desktopActive;labelColor:desktopEntry.labelColor;personalFolder:manager.personalAssets;full:desktopEntry.entryKind==="trash" && desktopFiles.trashFull}
            Rectangle {x:29;y:0;width:42;height:42;color:"transparent";border.color:"#111";border.width:2;visible:desktopFiles.dropTarget===desktopEntry.entryKey}
            Rectangle {
                objectName:"desktop-label-"+desktopEntry.entryName
                x:entryLabel.x+Math.floor((entryLabel.width-entryLabel.contentWidth)/2)-2;y:43
                width:Math.ceil(entryLabel.contentWidth)+4;height:Math.min(30,Math.ceil(entryLabel.contentHeight)+1)
                color:desktopEntry.isSelected?(manager.desktopActive?"black":"#8995a2"):"white"
            }
            Text {id:entryLabel;x:2;y:43;width:96;height:Math.min(30,implicitHeight+2);text:desktopEntry.entryName;textFormat:Text.PlainText;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.Wrap;elide:Text.ElideRight;maximumLineCount:2
                color:desktopEntry.isSelected?"white":"black";font.italic:desktopEntry.entryKind==="link";font.pixelSize:12}
            MouseArea {
                anchors.fill:parent
                acceptedButtons:Qt.LeftButton|Qt.RightButton
                enabled:!manager.shellModal && !desktopFiles.busy
                property point previousPointer
                property point pressPointer
                property bool moved:false
                onPressed:function(mouse){
                    desktop.closeMenu()
                    if(!manager.activateDesktop()){mouse.accepted=false;return}
                    if(mouse.button===Qt.RightButton){
                        if(!desktopEntry.isSelected)desktopFiles.select(desktopEntry.entryKey)
                        let point=mapToItem(desktop,mouse.x,mouse.y)
                        desktop.showDesktopContext(point.x,point.y,true)
                        return
                    }
                    desktopFiles.select(desktopEntry.entryKey,Boolean(mouse.modifiers & Qt.ShiftModifier))
                    desktopFiles.beginDrag()
                    previousPointer=mapToItem(desktop,mouse.x,mouse.y);pressPointer=previousPointer;moved=false
                }
                onPositionChanged:function(mouse){
                    if(!pressed || !desktopFiles.dragging)return
                    let p=mapToItem(desktop,mouse.x,mouse.y)
                    if(!moved && Math.abs(p.x-pressPointer.x)+Math.abs(p.y-pressPointer.y)<6)return
                    moved=true;desktopFiles.moveSelected(Math.round(p.x-previousPointer.x),Math.round(p.y-previousPointer.y));desktopFiles.updateDropTarget(Math.round(p.x),Math.round(p.y));manager.updateDesktopDragTarget(Math.round(p.x),Math.round(p.y));previousPointer=p
                }
                onReleased:function(mouse){if(mouse.button!==Qt.LeftButton)return;if(moved){let p=mapToItem(desktop,mouse.x,mouse.y);manager.finishDesktopDrag(Math.round(p.x),Math.round(p.y),Boolean(mouse.modifiers & (Qt.AltModifier|Qt.ControlModifier)))}else desktopFiles.cancelDrag()}
                onCanceled:desktopFiles.cancelDrag()
                onDoubleClicked:if(manager.activateDesktop()){desktopFiles.select(desktopEntry.entryKey);desktopFiles.openSelected()}
            }
        }
    }
    Item {
        z:9999;visible:manager.nativeDragging
        x:manager.nativeDragPosition.x-12;y:manager.nativeDragPosition.y-12
        WaylandQuickItem { surface:manager.nativeDragIcon;inputEventsEnabled:false;focusOnClick:false
            scale:manager.hostScale/(output?output.scaleFactor:1);transformOrigin:Item.TopLeft }
        Rectangle {x:8;y:8;width:13;height:14;visible:manager.nativeDragCopy;color:"white";border.color:"#111"
            Text {anchors.centerIn:parent;text:"+";font.pixelSize:12;color:"black"}
        }
    }
    Repeater {
        id:windowFrames
        model: manager
        delegate: WindowRoll {
            id: frame
            required property var record
            objectName: "frame-"+record.id
            // A client's full screen covers the desktop without chrome; while it
            // is active it also covers the menu bar and Control Strip.
            readonly property bool chrome: record.serverDecorated && !record.fullScreen
            x: record.fullScreen ? record.fullScreenRect.x : record.x; y: record.fullScreen ? record.fullScreenRect.y : record.y
            z: record.fullScreen && record.active ? 10002 : record.layer
            width: record.width + (chrome ? 6 : 0)
            contentHeight:record.height;serverDecorated:chrome;collapsed:record.collapsed
            animationDuration:desktop.appearance.shadeDuration
            onAnimatingChanged:manager.setShadeInMotion(record.id,animating)
            visible: record.shown
            focus: record.active && !manager.menuOpen && !record.collapsed && !animating && !manager.shellModal
            enabled: !manager.shellModal
            onXChanged: if (!record.fullScreen && Math.round(x) !== record.x) manager.moveWindow(record.id, Math.round(x), Math.round(y))
            onYChanged: if (!record.fullScreen && Math.round(y) !== record.y) manager.moveWindow(record.id, Math.round(x), Math.round(y))
            Rectangle {visible:frame.chrome; x: 3; y: 3; width: parent.width; height: parent.height; color: "#50000000" }
            Rectangle {visible:frame.chrome; anchors.fill: parent; color: "#ccc"; border.width: 1; border.color: "#111" }
            Rectangle {visible:frame.chrome; x: 1; y: 1; width: parent.width - 2; height: parent.height - 2; color: "transparent"; border.color: "#fff" }
            Rectangle {
                id: titlebar
                visible: frame.chrome
                x: 2; y: 2; width: parent.width-4; height: 19
                color: record.active ? "#ccc" : "#d6d6d6"
                Repeater {
                    model: record.modal || !record.active ? 0 : 5
                    Rectangle { required property int index; x: 4; y: 3+index*3; width: titlebar.width-8; height: 1; color: record.active ? "#777" : "#c5c5c5" }
                }
                MouseArea {
                    objectName:"title-bar-"+record.id
                    anchors.fill: parent;property point origin;property point pointerOrigin
                    // Two clicks: WindowShade, at once. A third: the shade rolls back and the window
                    // fills the screen (or returns to where it was).
                    property double doubleClickAt:0;property bool shadedByDoubleClick:false
                    onPressed: function(mouse) {
                        manager.activate(record.id);origin=Qt.point(record.x,record.y);pointerOrigin=mapToItem(desktop,mouse.x,mouse.y)
                        if(doubleClickAt && Date.now()-doubleClickAt<=Qt.styleHints.mouseDoubleClickInterval){
                            doubleClickAt=0
                            if(shadedByDoubleClick)manager.shade(record.id)
                            manager.fillScreen(record.id)
                        }
                    }
                    onPositionChanged: function(mouse) {if(pressed){let p=mapToItem(desktop,mouse.x,mouse.y);manager.moveWindow(record.id,origin.x+p.x-pointerOrigin.x,origin.y+p.y-pointerOrigin.y)}}
                    onDoubleClicked: {
                        doubleClickAt=Date.now()
                        shadedByDoubleClick=record.canShade && desktop.appearance.doubleClickCollapse
                        if(shadedByDoubleClick)manager.shade(record.id)
                    }
                }
                Rectangle {
                    anchors.centerIn: parent; width: Math.min(caption.implicitWidth+18,parent.width-(record.modal?12:120)); height: 18; color: titlebar.color
                    Text { id: caption; anchors.centerIn: parent; width: parent.width-8; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; text: record.title;textFormat:Text.PlainText; color: record.active ? "#111" : "#777"; font.pixelSize: 12; font.bold: false }
                }
                Row {
                    objectName:"close-box-"+record.id
                    visible:record.hasCloseBox && record.active;x: 4; anchors.verticalCenter: parent.verticalCenter
                    TitleBox { objectName:"close-box-button-"+record.id; kind:"close"; onActivated:manager.closeWindow(record.id) }
                }
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 7
                    TitleBox {objectName:"zoom-box-"+record.id;visible:record.canZoom && record.active;kind:"zoom";onActivated:manager.zoom(record.id)}
                    TitleBox {objectName:"shade-box-"+record.id;visible:record.canShade && record.active;kind:"shade";onActivated:manager.shade(record.id)}
                }
            }
            ClientSurface {
                id: client
                objectName: "client-"+record.id
                hostScale: manager.hostScale
                screens: manager.displayOutputs
                deferDragRelease:manager.nativeDragOnDesktop
                // A self-decorated window's own shadow lies outside its position (Chromium could
                // not reach the menu bar: psn_tch, 2026-10-22).
                x: frame.chrome ? 3 : -record.contentOffset.x; y: frame.chrome ? 23 : -record.contentOffset.y
                shellSurface: record.shell
                // Qt's shell integration mutates moveItem on fullscreen ack,
                // sometimes before its view has the requested output. Keep
                // those writes off the frame's policy-owned QML bindings.
                moveItem: clientMoveProxy
                visible: frame.reveal > 0
                inputEventsEnabled: visible && !record.collapsed && !frame.animating && record.active && !manager.menuOpen && !manager.shellModal
                focus: frame.focus
                focusOnClick: false
                autoCreatePopupItems: true
                Connections {target:manager;function onFocusWindow(id){if(id===record.id && client.visible && !frame.animating)client.takeFocus()}}
                // A negative key is sent bare (Clear is Delete); others as Command chords.
                Connections {target:desktop;function onEditCommand(id,key){if(id===record.id){if(key<0)client.sendKey(-key);else client.sendCommand(key)}}}
            }
            Item {
                id: clientMoveProxy
                parent: desktop
                visible: false
                x: record.x; y: record.y
                onXChanged: {
                    if(!record.fullScreen && Math.round(x)!==record.x)
                        manager.moveWindow(record.id,Math.round(x),record.y)
                    x=Qt.binding(function(){return record.x})
                }
                onYChanged: {
                    if(!record.fullScreen && Math.round(y)!==record.y)
                        manager.moveWindow(record.id,record.x,Math.round(y))
                    y=Qt.binding(function(){return record.y})
                }
            }
            MouseArea {
                x:frame.chrome?3:0;y:frame.chrome?23:0;width:record.width;height:record.height
                visible: !record.collapsed && !frame.animating && !record.active
                acceptedButtons:Qt.LeftButton|Qt.RightButton
                onPressed:function(mouse){
                    if(mouse.button===Qt.RightButton)manager.contextClickWindow(record.id,mapToItem(desktop,mouse.x,mouse.y))
                    else manager.activate(record.id)
                }
            }
            Rectangle {
                objectName:"resize-box-"+record.id
                visible: frame.chrome && !record.collapsed && !frame.animating && record.canResize; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 15; height: 15; color: "#ccc"
                Canvas {
                    anchors.fill:parent
                    onPaint:{
                        let c=getContext("2d");c.clearRect(0,0,width,height);c.lineWidth=1
                        for(let i=0;i<3;++i){
                            let o=4+i*3
                            c.strokeStyle="#666";c.beginPath();c.moveTo(o+.5,13.5);c.lineTo(13.5,o+.5);c.stroke()
                            c.strokeStyle="white";c.beginPath();c.moveTo(o+1.5,13.5);c.lineTo(13.5,o+1.5);c.stroke()
                        }
                    }
                }
                MouseArea {anchors.fill:parent;property point start;property int startW;property int startH
                    onPressed: function(mouse) {start=mapToItem(desktop,mouse.x,mouse.y);startW=record.width;startH=record.height;manager.activate(record.id)}
                    onPositionChanged:function(mouse){if(pressed){var p=mapToItem(desktop,mouse.x,mouse.y);manager.resizeWindow(record.id,startW+p.x-start.x,startH+p.y-start.y)}}
                }
            }
            Rectangle {
                anchors.fill:parent;z:200;enabled:false;color:"transparent"
                border.color:"#202050";border.width:2
                visible:manager.desktopDropWindow===record.id
            }
        }
    }
    Rectangle {
        id: menuBar; objectName: "desktop-menu-bar"; z: 10000
        x:manager.primaryDisplayRect.x;y:manager.primaryDisplayRect.y;width:manager.primaryDisplayRect.width
        // Menus and the menu bar are #dddddd, as in 9.2.2 (psn_tch chose to match it, 2026-10-22).
        height: 26; color: "#ddd"; border.color: "#222";enabled:!manager.shellModal
        Rectangle {x:1;y:1;width:parent.width-2;height:1;color:"white"}
        MouseArea {anchors.fill:parent;onPressed:desktop.closeMenu()}
        Rectangle {objectName:"menu-bar-flash";anchors.fill:parent;z:300;color:"black";visible:menuFlash.running}
        Row {
            x: 10; height: parent.height; spacing: 0
            Repeater {
                id:barItems
                model: desktop.barEntries
                delegate: Rectangle {
                    required property var modelData; required property int index
                    width: (modelData.kind==="apple"?16:label.implicitWidth)+22;height:26;color:desktop.openMenu===index?desktop.menuAccent:"transparent"
                    objectName: "bar-entry-"+index
                    Text {id:label;anchors.centerIn:parent;visible:modelData.kind!=="apple" || emblem.status!==Image.Ready;text:modelData.label;textFormat:Text.PlainText;color:modelData.enabled===false?"#888":desktop.openMenu===index?"white":"black";font.pixelSize:12}
                    Image {id:emblem;anchors.centerIn:parent;width:16;height:16;visible:modelData.kind==="apple";source:modelData.kind==="apple"?desktop.appearance.menuEmblem:"";smooth:false;fillMode:Image.PreserveAspectFit}
                    MouseArea{anchors.fill:parent;enabled:modelData.enabled!==false;hoverEnabled:true;onClicked:desktop.showMenu(index);onEntered:if(desktop.openMenu>=0 && desktop.openMenu!==index)desktop.showMenu(index)}
                }
            }
        }
        Text {
            id:menuClock;objectName:"menu-bar-clock"
            anchors.right:keyboardMenu.visible?keyboardMenu.left:appDivider.left;anchors.rightMargin:7;anchors.verticalCenter:parent.verticalCenter
            visible:desktop.appearance.clock.visible!==false
            text:desktop.clockText(desktop.clockTime,desktop.appearance.clock,desktop.clockShowsDate);textFormat:Text.StyledText;font.pixelSize:12
            MouseArea {anchors.fill:parent;anchors.margins:-4;onClicked:{desktop.clockShowsDate=!desktop.clockShowsDate;if(desktop.clockShowsDate)clockDate.restart()}}
        }
        Rectangle {
            id:keyboardMenu;objectName:"keyboard-menu";visible:desktop.keyboardMenuShown
            anchors.right:appDivider.left;anchors.rightMargin:2;width:keyboardTitle.implicitWidth+14;height:26
            color:desktop.openMenu===desktop.keyboardMenuIndex?desktop.menuAccent:"transparent"
            Rectangle {
                anchors.centerIn:parent;width:keyboardTitle.implicitWidth+6;height:15
                color:"transparent";border.color:desktop.openMenu===desktop.keyboardMenuIndex?"white":"black"
                Text {id:keyboardTitle;objectName:"keyboard-menu-title";anchors.centerIn:parent;text:desktop.keyboardShortName(manager.input.keyboard.layout)
                      textFormat:Text.PlainText;font.pixelSize:10;font.bold:true;color:parent.border.color}
            }
            MouseArea {anchors.fill:parent;hoverEnabled:true;onPressed:desktop.showMenu(desktop.keyboardMenuIndex)
                onEntered:if(desktop.openMenu>=0 && desktop.openMenu!==desktop.keyboardMenuIndex)desktop.showMenu(desktop.keyboardMenuIndex)}
        }
        // The application menu's etched strip, as in 9.2.2 (psn_tch's About This Computer capture):
        // a solid white line, four grip dots, then a solid grey line through the top highlight.
        Item {
            id:appDivider;objectName:"application-menu-divider";anchors.right:appMenu.left;y:1;width:6;height:24
            Rectangle {objectName:"application-menu-divider-light";x:0;y:1;width:1;height:23;color:"white"}
            Repeater {model:4;Item {required property int index;x:2;y:7+index*3;width:2;height:2
                Rectangle {x:0;y:0;width:1;height:1;color:"#8c8c8c"}
                Rectangle {x:1;y:0;width:1;height:1;color:"#adadad"}
                Rectangle {x:0;y:1;width:1;height:1;color:"#bdbdbd"}
                Rectangle {x:1;y:1;width:1;height:1;color:"white"}}}
            Rectangle {objectName:"application-menu-divider-shadow";x:5;y:0;width:1;height:24;color:"#9c9c9c"}
        }
        Rectangle {id:appMenu;objectName:"application-menu";anchors.right:parent.right;anchors.rightMargin:6;width:appTitle.implicitWidth+16;height:26;color:desktop.openMenu===desktop.barEntries.length?desktop.menuAccent:"transparent"
            // While an application asks for attention, the title alternates about once a second
            // with that application's icon alone, as 9.2.2 does (checked in psn_tch's 9.2.2 VM).
            readonly property var waiting:manager.applications.find(function(a){return a.attention && !a.active})
            property bool alternate:false
            Timer {interval:1000;repeat:true;running:Boolean(appMenu.waiting);onTriggered:appMenu.alternate=!appMenu.alternate;onRunningChanged:if(!running)appMenu.alternate=false}
            Row {
                id:appTitle;anchors.centerIn:parent;spacing:5
                readonly property bool showWaiting:Boolean(appMenu.waiting) && appMenu.alternate
                MenuGlyph {objectName:"application-menu-glyph";kind:(appTitle.showWaiting?appMenu.waiting.name:manager.activeApplication)==="Finder"?"disk":"application";anchors.verticalCenter:parent.verticalCenter}
                Text{id:appLabel;visible:!appTitle.showWaiting;text:manager.activeApplication;textFormat:Text.PlainText;color:desktop.openMenu===desktop.barEntries.length?"white":"black";font.bold:false;font.pixelSize:12;anchors.verticalCenter:parent.verticalCenter}
            }
            // Pressing opens the menu; dragging down past the open menu tears it off as the
            // Application Switcher, dropped where the button is released (9.2.2).
            MouseArea{
                objectName:"application-menu-area";anchors.fill:parent;preventStealing:true
                onPressed:{desktop.tearingSwitcher=false;desktop.showMenu(desktop.barEntries.length)}
                onPositionChanged:function(mouse){
                    if(!pressed || desktop.openMenu!==desktop.barEntries.length)return
                    let at=mapToItem(desktop,mouse.x,mouse.y)
                    desktop.tearPoint=at
                    desktop.tearingSwitcher=at.y>popup.y+popup.height+6
                }
                onReleased:{
                    if(!desktop.tearingSwitcher)return
                    desktop.tearingSwitcher=false
                    desktop.closeMenu()
                    let area=manager.primaryDisplayRect
                    desktop.appearance.setSwitcher(true,Math.max(0,Math.round(desktop.tearPoint.x-area.x-applicationSwitcher.width/2)),
                                                   Math.max(0,Math.round(desktop.tearPoint.y-area.y)))
                }
                onCanceled:desktop.tearingSwitcher=false
            }
        }
    }
    // A chosen menu item blinks before its command runs, as many times as General Controls says
    // (9.2.2: Off, 1, 2 or 3). The desktop keeps the count, since a menu can rebuild its rows
    // while it is open (the Control Strip's refresh as they open).
    property string blinkRow:""
    property int blinkFlashes:0
    property var blinkCommand:null
    function chooseShellEntry(name, action, id) {
        if(blinkFlashes>0)return
        const blinks=desktop.appearance.general.menuBlinks
        if(!(blinks>0)){dispatchShellCommand(action,id);return}
        blinkRow=name;blinkCommand={action:action,id:id};blinkFlashes=2*blinks;menuBlink.start()
    }
    Timer {id:menuBlink;interval:45;repeat:true
        onTriggered:{
            desktop.blinkFlashes-=1
            if(desktop.blinkFlashes>0)return
            stop()
            const command=desktop.blinkCommand
            desktop.blinkRow="";desktop.blinkCommand=null
            desktop.dispatchShellCommand(command.action,command.id)
        }}
    Component {
        id: shellMenuRow
        Rectangle {
            id: row
            required property var modelData
            required property int index
            property bool submenuRow: false
            width:parent?parent.width:200;height:modelData.separator?9:25
            readonly property bool blinking:desktop.blinkFlashes>0 && desktop.blinkRow===objectName
            readonly property bool lit:!modelData.separator && (blinking ? desktop.blinkFlashes%2===1 : (entryMouse.containsMouse ||
                (!submenuRow && Boolean(modelData.children) && desktop.openSubmenu===index)))
            color:lit?desktop.menuAccent:"transparent"
            objectName:"desktop-command-"+(modelData.name?modelData.name:modelData.action)
            Rectangle{visible:Boolean(modelData.separator);x:2;y:4;width:parent.width-4;height:1;color:"#888"}
            Text{visible:Boolean(modelData.checked);anchors.verticalCenter:parent.verticalCenter;x:4;text:"✓";font.pixelSize:12;color:row.lit?"white":"black"}
            // ◆: this application asks for attention (Mac OS 9's Notification Manager mark).
            Text{visible:Boolean(modelData.marked);objectName:"attention-mark-"+(modelData.text?modelData.text:"");anchors.verticalCenter:parent.verticalCenter;x:4;text:"◆";font.pixelSize:10;color:row.lit?"white":"black"}
            // A label's colour swatch (File ▸ Label).
            Rectangle {visible:Boolean(modelData.swatch);x:17;width:12;height:12;anchors.verticalCenter:parent.verticalCenter
                color:modelData.swatch?modelData.swatch:"transparent";border.color:"#222"}
            MenuGlyph {objectName:"menu-glyph-"+(modelData.text?modelData.text:"");visible:Boolean(modelData.glyph);x:17;anchors.verticalCenter:parent.verticalCenter;kind:modelData.glyph?modelData.glyph:"application"}
            Text{visible:!modelData.separator;anchors.verticalCenter:parent.verticalCenter;x:modelData.glyph || modelData.swatch?39:15;width:parent.width-x-20;elide:Text.ElideRight
                text:modelData.text?modelData.text:"";textFormat:Text.PlainText;color:modelData.enabled===false?"#888":row.lit?"white":"black";font.pixelSize:12}
            Text{visible:Boolean(modelData.children);anchors.right:parent.right;anchors.rightMargin:6;anchors.verticalCenter:parent.verticalCenter;text:"▶";font.pixelSize:9;color:row.lit?"white":"black"}
            // A Wi-Fi network: its lock (secured) and four signal bars, at the right.
            Row {visible:Boolean(modelData.bars);objectName:"menu-signal-"+(modelData.text?modelData.text:"");anchors.right:parent.right;anchors.rightMargin:10
                anchors.verticalCenter:parent.verticalCenter;spacing:6
                Item {width:9;height:11;anchors.bottom:parent.bottom;visible:Boolean(modelData.secure)
                    Rectangle {x:1.5;y:0;width:6;height:7;radius:3;color:"transparent";border.width:1.5;border.color:row.lit?"white":"black"}
                    Rectangle {x:0;y:4.5;width:9;height:6.5;color:row.lit?"white":"black"}}
                Item {width:15;height:12;anchors.bottom:parent.bottom
                    readonly property int bars:row.modelData.bars||0
                    readonly property color on:row.lit?"white":"black"
                    readonly property color off:row.lit?"#9999cc":"#aaa"
                    Rectangle {x:0;y:9;width:3;height:3;color:parent.bars>=1?parent.on:parent.off}
                    Rectangle {x:4;y:6;width:3;height:6;color:parent.bars>=2?parent.on:parent.off}
                    Rectangle {x:8;y:3;width:3;height:9;color:parent.bars>=3?parent.on:parent.off}
                    Rectangle {x:12;y:0;width:3;height:12;color:parent.bars>=4?parent.on:parent.off}}}
            Text{visible:Boolean(modelData.key);objectName:"menu-key-"+(modelData.text?modelData.text:"");anchors.right:parent.right;anchors.rightMargin:10;anchors.verticalCenter:parent.verticalCenter
                text:modelData.key?modelData.key:"";textFormat:Text.PlainText;font.pixelSize:12;color:modelData.enabled===false?"#888":row.lit?"white":"black"}
            MouseArea{id:entryMouse;anchors.fill:parent;enabled:!modelData.separator && modelData.enabled!==false;hoverEnabled:true
                onEntered:if(!row.submenuRow && !(desktop.stripModule!=="" && Date.now()-desktop.stripMenuOpenedAt<300)){if(modelData.children)desktop.openShellSubmenu(index,modelData.children,row.mapToItem(desktop,0,0).y);else desktop.openSubmenu=-1}
                onPositionChanged:if(!row.submenuRow && modelData.children && desktop.openSubmenu!==index && desktop.stripModule!=="" &&
                                     Date.now()-desktop.stripMenuOpenedAt>=300)desktop.openShellSubmenu(index,modelData.children,row.mapToItem(desktop,0,0).y)
                onClicked:if(modelData.children)desktop.openShellSubmenu(index,modelData.children,row.mapToItem(desktop,0,0).y);else desktop.chooseShellEntry(row.objectName,modelData.action,modelData.id)}
        }
    }
    Rectangle {visible:popup.visible;z:10000.5;x:popup.x+3;y:popup.y+3;width:popup.width;height:popup.height;color:"#50000000"}
    Rectangle {
        id: popup; objectName: "desktop-menu-popup"; z: 10001; visible:desktop.openMenu>=0 && desktop.exportedRoot<0
        x:Math.max(menuBar.x,Math.min(desktop.openMenu===desktop.barEntries.length?menuBar.x+menuBar.width-width-2
              :desktop.openMenu===desktop.keyboardMenuIndex?keyboardMenu.mapToItem(desktop,0,0).x:desktop.menuX(desktop.openMenu),menuBar.x+menuBar.width-width))
        y:menuBar.y+25
        width:240;height:items.height+6;color:"#ddd";border.color:"#222"
        Column {
            id:items;x:3;y:3;width:parent.width-6
            Repeater {model:desktop.openMenu>=0 && desktop.exportedRoot<0 ? desktop.shellMenuEntries(desktop.openMenu) : [];delegate:shellMenuRow}
        }
    }
    Rectangle {visible:submenu.visible;z:10001.5;x:submenu.x+3;y:submenu.y+3;width:submenu.width;height:submenu.height;color:"#50000000"}
    Rectangle {
        id: submenu; objectName:"desktop-submenu"; z: 10002; visible:owner.visible && desktop.openSubmenu>=0
        // Beside the menu it belongs to: the menu bar's, or a Control Strip module's.
        readonly property Item owner: desktop.stripModule!=="" ? stripMenu : popup
        x:owner.x+owner.width+width-4<=menuBar.x+menuBar.width?owner.x+owner.width-4:Math.max(menuBar.x,owner.x-width+4)
        y:Math.max(menuBar.y+26,Math.min(desktop.submenuY-3,menuBar.y+manager.primaryDisplayRect.height-height-2))
        width:220;height:subitems.height+6;color:"#ddd";border.color:"#222"
        Column {
            id:subitems;x:3;y:3;width:parent.width-6
            Repeater {model:desktop.openSubmenu>=0 ? desktop.submenuEntries : [];delegate:shellMenuRow
                onItemAdded:function(index,item){item.submenuRow=true}}
        }
    }
    Rectangle {visible:stripMenu.visible;z:10000.5;x:stripMenu.x+3;y:stripMenu.y+3;width:stripMenu.width;height:stripMenu.height;color:"#50000000"}
    Rectangle {
        id: stripMenu; objectName:"control-strip-menu"; z: 10001; visible:desktop.stripModule!==""
        // The rows, rebuilt only when they change: the entries read every module's live state
        // (Quick Controls, hhd, Wi-Fi), and a rebuild that changed nothing lost a click on its way.
        readonly property var live:desktop.stripModule!=="" ? desktop.stripMenuEntries(desktop.stripModule) : []
        property var entries:[]
        onLiveChanged:if(JSON.stringify(live)!==JSON.stringify(entries))entries=live
        Component.onCompleted:entries=live
        width:220;height:stripItems.height+6;color:"#ddd";border.color:"#222"
        x:Math.max(menuBar.x,Math.min(desktop.stripAnchor.x,menuBar.x+menuBar.width-width))
        // Above the module (V922 E12), or below it when the strip sits under the menu bar.
        y:desktop.stripAnchor.y-height>=menuBar.y+26?desktop.stripAnchor.y-height
          :desktop.stripAnchor.y+desktop.stripAnchor.height+height<=manager.primaryDisplayRect.y+manager.primaryDisplayRect.height
            ?desktop.stripAnchor.y+desktop.stripAnchor.height
          :Math.max(menuBar.y+26,manager.primaryDisplayRect.y+manager.primaryDisplayRect.height-height)
        Column {
            id:stripItems;x:3;y:3;width:parent.width-6
            Repeater {model:stripMenu.entries;delegate:shellMenuRow}
        }
    }
    Rectangle {
        z:5000;x:12;y:desktop.height-height-12;width:operationLabel.implicitWidth+20;height:30
        visible:desktopFiles.busy;color:"#ddd";border.color:"#333"
        Text {id:operationLabel;anchors.centerIn:parent;text:desktopFiles.operationStatus;textFormat:Text.PlainText;font.pixelSize:12}
    }
    ApplicationBrowser {id:applicationBrowser;sessionManager:manager;catalog:manager.applicationCatalog}
    ScreenshotDialog {id:screenshotDialog;sessionManager:manager;tool:manager.screenshots}
    // Command-Shift-4's crosshair over the whole desktop. Escape, another button or a click without
    // a drag copies nothing.
    Item {
        id:screenshotArea;objectName:"screenshot-area";anchors.fill:parent;z:100000;visible:false
        property point start:Qt.point(0,0)
        property point end:Qt.point(0,0)
        property bool dragging:false
        property var token:0
        // What had the keyboard (an application's surface, a note, the desktop) gets it back.
        property Item returnFocus:null
        function clamp(x,y){return Qt.point(Math.max(0,Math.min(width,x)),Math.max(0,Math.min(height,y)))}
        function begin(){dragging=false;returnFocus=desktop.Window.activeFocusItem;token=manager.beginShellModal();visible=true;forceActiveFocus()}
        function finish(copy){
            visible=false;dragging=false
            if(token){manager.endShellModal(token);token=0}
            // Round both corners, then subtract: rounding the size separately could reach a pixel
            // past the picture at a fractional scale, and the copy would silently not happen (Rocky).
            const ratio=manager.screenshots.imageSize.width/Math.max(1,width)
            const x=Math.round(Math.min(start.x,end.x)*ratio), y=Math.round(Math.min(start.y,end.y)*ratio)
            const w=Math.round(Math.max(start.x,end.x)*ratio)-x, h=Math.round(Math.max(start.y,end.y)*ratio)-y
            if(copy && w>0 && h>0 && manager.screenshots.select(x,y,w,h) && !manager.screenshots.copy())
                statusMessage=manager.screenshots.error
            manager.screenshots.discard()
            if(returnFocus && returnFocus.visible)returnFocus.forceActiveFocus()
            else desktop.forceActiveFocus()
            returnFocus=null
        }
        Keys.onPressed:function(event){event.accepted=true;if(event.key===Qt.Key_Escape)finish(false)}
        MouseArea {
            anchors.fill:parent;cursorShape:Qt.CrossCursor;acceptedButtons:Qt.AllButtons
            onPressed:function(mouse){
                if(mouse.button!==Qt.LeftButton){screenshotArea.finish(false);return}
                screenshotArea.start=screenshotArea.clamp(mouse.x,mouse.y);screenshotArea.end=screenshotArea.start
                screenshotArea.dragging=true
            }
            onPositionChanged:function(mouse){if(screenshotArea.dragging)screenshotArea.end=screenshotArea.clamp(mouse.x,mouse.y)}
            onReleased:function(mouse){
                if(!screenshotArea.dragging)return
                screenshotArea.end=screenshotArea.clamp(mouse.x,mouse.y)
                screenshotArea.finish(true)
            }
        }
        Rectangle {
            visible:screenshotArea.dragging
            x:Math.min(screenshotArea.start.x,screenshotArea.end.x);y:Math.min(screenshotArea.start.y,screenshotArea.end.y)
            width:Math.abs(screenshotArea.end.x-screenshotArea.start.x);height:Math.abs(screenshotArea.end.y-screenshotArea.start.y)
            color:"transparent";border.color:"black";border.width:1
            Rectangle {anchors.fill:parent;anchors.margins:1;color:"transparent";border.color:"white"}
        }
    }
    // Every control panel in one modeless window (Apple menu ▸ Control Panels). The Control Strip
    // stays for quick changes; its "Open …" items open this window at that panel.
    // In front it is above every application window; behind, it sits below them, above the icons.
    // It belongs to the desktop (the Finder): bringing it forward makes the desktop the active
    // application, so the manager hands it the keyboard (onFocusDesktop) and not an application.
    // Sticky Notes (StickyNotes, StickyNote.qml): a new note cascades from the primary display's
    // upper middle and comes forward with the keyboard, ready for typing.
    function newStickyNote() {
        let area=manager.primaryDisplayRect, step=(manager.stickies.count%8)*24
        let id=manager.stickies.create(area.x+Math.round(area.width/2)-110+step,area.y+60+step,"yellow")
        if(id)Qt.callLater(function(){let n=stickyNote(id);if(n){n.bringForward();n.forceActiveFocus()}})
    }
    function stickyNote(id) {
        for(let i=0;i<stickyRepeater.count;++i){let n=stickyRepeater.itemAt(i);if(n && n.noteId===id)return n}
        return null
    }
    Repeater {
        id:stickyRepeater
        model:manager.stickies
        delegate:StickyNote {
            id:stickyItem
            notes:manager.stickies
            visible:!manager.stickies.hidden
            // In front: above every application window and the Control Panels behind it. Kept in
            // front: above applications even behind. Otherwise below them, above the icons.
            z:front?3995+noteOrder*0.001:noteFloating?3000+noteOrder*0.001:0.6+noteOrder*0.001
            onRaised:desktop.bringShellWindowForward(stickyItem)
            onNewNoteRequested:desktop.newStickyNote()
            onDeleteRequested:function(id,empty){
                // Removing the row destroys this note: give the keyboard back first, remove after.
                if(empty){desktop.forceActiveFocus();Qt.callLater(function(){manager.stickies.remove(id)});return}
                deleteStickyDialog.noteId=id;deleteStickyDialog.open()
            }
        }
    }
    // Deleting a note with text asks first; Cancel is the default (Return keeps the note).
    ShellDialog {
        id:deleteStickyDialog;objectName:"sticky-delete-dialog";sessionManager:manager;width:400;title:"Stickies"
        alertSound:true
        cancelIsDefault:true
        property int noteId:0
        standardButtons:Dialog.Ok | Dialog.Cancel
        contentItem:Text {text:"Delete this note? Its text will be lost.";textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
        onAccepted:{manager.stickies.remove(noteId);noteId=0}
        onRejected:{let n=desktop.stickyNote(noteId);noteId=0;if(n)n.forceActiveFocus()}
    }
    // Opened from an application, closing it goes back to that application, as when a Mac OS 9
    // control panel quit; unless something else was brought forward in between.
    // The desktop-owned window that had the keyboard last (ShellWindowFocus): Control Panels or a note.
    property var frontShellWindow:null
    property bool tearingSwitcher:false
    property point tearPoint
    // The desk accessory in front, when it takes the Edit menu's commands (editCommands, edit()).
    function editAccessory() {
        let w=frontShellWindow
        return w && w.visible && w.front && w.editCommands!==undefined ? w : null
    }
    property var controlPanelsReturnTo:0
    // A desktop-owned window waiting for an alert or the Applications browser to finish.
    property var shellWindowPending:null
    function bringShellWindowForward(window) {
        // An alert or the Applications browser still has the input: come forward after it.
        if(manager.shellModal){shellWindowPending=window;return}
        shellWindowPending=null
        if(window===controlPanels && !manager.desktopActive){let a=manager.applications.find(function(x){return x.active});controlPanelsReturnTo=a?a.id:0}
        manager.activateDesktop()
        if(window.visible)window.forceActiveFocus()
    }
    Connections {target:manager;function onContextChanged(){
        let w=desktop.shellWindowPending
        if(w && !manager.shellModal)Qt.callLater(function(){desktop.bringShellWindowForward(w)})
    }}
    property var aboutReturnTo:0
    property var aboutReturnWindow:null
    AboutComputerWindow {
        id:aboutComputer
        sessionManager:manager
        z:front?3991:0.76
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.aboutReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==aboutComputer && previous.visible)desktop.aboutReturnWindow=previous
            desktop.bringShellWindowForward(aboutComputer)
        }
        onFrontChanged:if(!front && visible){desktop.aboutReturnTo=0;desktop.aboutReturnWindow=null}
        onClosed:{
            let id=desktop.aboutReturnTo, previous=desktop.aboutReturnWindow
            desktop.aboutReturnTo=0;desktop.aboutReturnWindow=null
            if(desktop.shellWindowPending===aboutComputer)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    property var calculatorReturnTo:0
    property var calculatorReturnWindow:null
    CalculatorWindow {
        id:calculator
        z:front?3992:0.77
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.calculatorReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==calculator && previous.visible)desktop.calculatorReturnWindow=previous
            desktop.bringShellWindowForward(calculator)
        }
        onFrontChanged:if(!front && visible){desktop.calculatorReturnTo=0;desktop.calculatorReturnWindow=null}
        onCopyRequested:function(text){if(manager.clipboard)manager.clipboard.copyText(text)}
        onClosed:{
            let id=desktop.calculatorReturnTo, previous=desktop.calculatorReturnWindow
            desktop.calculatorReturnTo=0;desktop.calculatorReturnWindow=null
            if(desktop.shellWindowPending===calculator)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    // Control Panel (System 6), kept open beside the applications as long as the user likes.
    property var classicPanelReturnTo:0
    property var classicPanelReturnWindow:null
    ClassicControlPanelWindow {
        id:classicControlPanel
        appearance:desktop.appearance;input:manager.input;controls:manager.systemControls
        z:front?3995:0.795
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onOpenPanel:function(action){desktop.dispatchShellCommand(action,0)}
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.classicPanelReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==classicControlPanel && previous.visible)desktop.classicPanelReturnWindow=previous
            desktop.bringShellWindowForward(classicControlPanel)
        }
        onFrontChanged:if(!front && visible){desktop.classicPanelReturnTo=0;desktop.classicPanelReturnWindow=null}
        onClosed:{
            let id=desktop.classicPanelReturnTo, previous=desktop.classicPanelReturnWindow
            desktop.classicPanelReturnTo=0;desktop.classicPanelReturnWindow=null
            if(desktop.shellWindowPending===classicControlPanel)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    property var notePadReturnTo:0
    property var notePadReturnWindow:null
    NotePadWindow {
        id:notePadWindow
        store:manager.notePad
        z:front?3993:0.78
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.notePadReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==notePadWindow && previous.visible)desktop.notePadReturnWindow=previous
            desktop.bringShellWindowForward(notePadWindow)
        }
        onFrontChanged:if(!front && visible){desktop.notePadReturnTo=0;desktop.notePadReturnWindow=null}
        onClosed:{
            let id=desktop.notePadReturnTo, previous=desktop.notePadReturnWindow
            desktop.notePadReturnTo=0;desktop.notePadReturnWindow=null
            if(desktop.shellWindowPending===notePadWindow)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    property var keyCapsReturnTo:0
    property var keyCapsReturnWindow:null
    KeyCapsWindow {
        id:keyCapsWindow
        caps:manager.keyCaps
        z:front?3993.5:0.785
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.keyCapsReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==keyCapsWindow && previous.visible)desktop.keyCapsReturnWindow=previous
            desktop.bringShellWindowForward(keyCapsWindow)
        }
        onFrontChanged:if(!front && visible){desktop.keyCapsReturnTo=0;desktop.keyCapsReturnWindow=null}
        onClosed:{
            let id=desktop.keyCapsReturnTo, previous=desktop.keyCapsReturnWindow
            desktop.keyCapsReturnTo=0;desktop.keyCapsReturnWindow=null
            if(desktop.shellWindowPending===keyCapsWindow)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    property var scrapbookReturnTo:0
    property var scrapbookReturnWindow:null
    ScrapbookWindow {
        id:scrapbookWindow
        store:manager.scrapbook
        clipboard:manager.clipboard
        z:front?3994:0.79
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        onRaised:{
            if(!manager.desktopActive){let a=manager.applications.find(function(a){return a.active});desktop.scrapbookReturnTo=a?a.id:0}
            let previous=desktop.frontShellWindow
            if(previous && previous!==scrapbookWindow && previous.visible)desktop.scrapbookReturnWindow=previous
            desktop.bringShellWindowForward(scrapbookWindow)
        }
        onFrontChanged:if(!front && visible){desktop.scrapbookReturnTo=0;desktop.scrapbookReturnWindow=null}
        onClosed:{
            let id=desktop.scrapbookReturnTo, previous=desktop.scrapbookReturnWindow
            desktop.scrapbookReturnTo=0;desktop.scrapbookReturnWindow=null
            if(desktop.shellWindowPending===scrapbookWindow)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
            else if(previous && previous.visible)desktop.bringShellWindowForward(previous)
            else desktop.forceActiveFocus()
        }
    }
    ControlPanelsWindow {
        id:controlPanels;z:front?3990:0.75
        onRaised:desktop.bringShellWindowForward(controlPanels)
        onFrontChanged:if(!front && visible)desktop.controlPanelsReturnTo=0
        onClosed:{
            let id=desktop.controlPanelsReturnTo
            desktop.controlPanelsReturnTo=0;if(desktop.shellWindowPending===controlPanels)desktop.shellWindowPending=null
            if(id && manager.desktopActive && manager.applications.some(function(a){return a.id===id && !a.hidden}))manager.activateApplication(id)
        }
        x:manager.primaryDisplayRect.x+Math.max(8,Math.round((manager.primaryDisplayRect.width-width)/2))
        y:manager.primaryDisplayRect.y+Math.max(30,Math.round((manager.primaryDisplayRect.height-height)/4))
        // In the list's order, alphabetical as in Mac OS 9's Control Panels folder.
        AppearanceDialog {id:appearanceDialog;sessionManager:manager;settings:desktop.appearance
            onPlacePictureRequested:desktopPictureDialog.edit()}
        // Tests substitute bluetoothPanel.service; the alert, strip module and Quick Controls follow it.
        BluetoothPanel {id:bluetoothPanel;service:manager.bluetooth
            onForgetRequested:function(path,name){forgetBluetoothDialog.path=path;forgetBluetoothDialog.name=name;forgetBluetoothDialog.open()}}
        DateTimePanel {id:dateTimePanel;settings:desktop.appearance}
        DesktopPictureDialog {id:desktopPictureDialog;settings:desktop.appearance}
        EnergySaverPanel {id:energyPanel;settings:manager.energy;sessionManager:manager}
        GeneralControlsPanel {id:generalPanel;settings:desktop.appearance}
        // Listed only on a computer whose Handheld Daemon can hide its built-in controller.
        HandheldPanel {id:handheldPanel;service:manager.handheld}
        KeyboardPanel {id:keyboardPanel;settings:manager.input}
        MonitorsPanel {id:monitorsPanel;sessionManager:manager}
        MousePanel {id:mousePanel;settings:manager.input}
        // Tests substitute wifiAlert.service and quickControls.service; this panel follows both.
        NetworkPanel {id:networkPanel;service:wifiAlert.service;controls:quickControls.service
            onForgetRequested:function(path,name){forgetWifiDialog.path=path;forgetWifiDialog.name=name;forgetWifiDialog.open()}}
        QuickControls {id:quickControls;service:manager.systemControls;bluetooth:bluetoothPanel.service}
        SoundPanel {id:soundPanel;service:manager.audio;alertSounds:manager.alertSounds;startupSound:manager.startupSound}
    }
    // A silent alert (alert volume 0, or no sound player) flashes the menu bar, as Mac OS 9 did.
    Timer {id:menuFlash;interval:140}
    Connections {target:manager.alertSounds;function onFlash(){menuFlash.restart()}}
    BluetoothAlert {id:bluetoothAlert;sessionManager:manager;service:bluetoothPanel.service}
    // Tests substitute wifiAlert.service; the strip module and the dialogs below follow it.
    WifiAlert {id:wifiAlert;sessionManager:manager;service:manager.wifi}
    ShellDialog {
        id:forgetWifiDialog;objectName:"wifi-forget-dialog";sessionManager:manager;width:440;title:"Forget Network"
        alertSound:true
        property string path:"";property string name:""
        cancelIsDefault:true
        standardButtons:Dialog.Ok | Dialog.Cancel
        contentItem:Text {text:"Forget “"+forgetWifiDialog.name+"”? This computer will stop joining it, and its password will be removed.";textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
        onAccepted:wifiAlert.service.forgetNetwork(path)
    }
    // Why joining a network failed, as an alert.
    ShellDialog {
        id:wifiMessageDialog;objectName:"wifi-message-dialog";sessionManager:manager;width:440;title:"Wi-Fi"
        alertSound:true
        standardButtons:Dialog.Ok
        contentItem:Text {objectName:"wifi-message-text";text:wifiAlert.service?wifiAlert.service.message:"";textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
        Connections {target:wifiAlert.service;function onMessageChanged(){if(wifiAlert.service.message)wifiMessageDialog.open()}}
        onClosed:if(wifiAlert.service)wifiAlert.service.clearMessage()
    }
    // After a display is turned on or off: keep it, or have it undone (also after 15 seconds,
    // by the session supervisor, so a dark or unreadable screen comes back by itself).
    ShellDialog {
        id:displayKeepDialog;objectName:"monitors-keep-dialog";sessionManager:manager;width:420;title:"Monitors"
        alertSound:true
        cancelIsDefault:true
        standardButtons:Dialog.Ok | Dialog.Cancel
        property int seconds:15
        contentItem:Text {
            objectName:"monitors-keep-text"
            text:"Keep this display setting? The previous one comes back in "+displayKeepDialog.seconds+(displayKeepDialog.seconds===1?" second.":" seconds.")
            textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12
        }
        Component.onCompleted:{standardButton(Dialog.Ok).text="Keep";standardButton(Dialog.Cancel).text="Revert"}
        Timer {interval:1000;repeat:true;running:displayKeepDialog.visible;onTriggered:displayKeepDialog.seconds=Math.max(0,displayKeepDialog.seconds-1)}
        onAboutToShow:seconds=15
        onAccepted:manager.keepDisplayChange()
        onRejected:manager.revertDisplayChange()
    }
    Connections {
        target:manager
        function onDisplayLayoutChanged() {
            if(manager.displayChangePending && !displayKeepDialog.visible)displayKeepDialog.open()
            else if(!manager.displayChangePending && displayKeepDialog.visible)displayKeepDialog.close()
        }
    }
    // Spring-loaded folders: a drag held a second over a desktop folder, disk or Trash opens it, so
    // the drop can go into its window (Mac OS 9).
    Timer {
        id:springTimer;interval:1000
        property string target:desktopFiles?desktopFiles.dropTarget:""
        onTargetChanged:if(target!=="")restart();else stop()
        onTriggered:if(desktopFiles.dropTarget===target)desktopFiles.springOpen(target)
    }
    ApplicationSwitcher {
        id:applicationSwitcher;z:7990;sessionManager:manager;settings:desktop.appearance
        applications:desktop.applicationsByName()
        onChosen:function(id){desktop.dispatchShellCommand("app",id)}
    }
    // The palette's outline, dotted, following the pointer while it is torn off the menu.
    Canvas {
        id:tearOutline;objectName:"application-switcher-outline";z:10002
        visible:desktop.tearingSwitcher
        x:desktop.tearPoint.x-width/2;y:desktop.tearPoint.y;width:applicationSwitcher.width;height:applicationSwitcher.height
        onVisibleChanged:if(visible)requestPaint()
        onPaint:{let c=getContext("2d");c.clearRect(0,0,width,height);c.fillStyle="#333"
            for(let x=0;x<width;x+=2){c.fillRect(x,0,1,1);c.fillRect(x,height-1,1,1)}
            for(let y=0;y<height;y+=2){c.fillRect(0,y,1,1);c.fillRect(width-1,y,1,1)}}
    }
    ControlStrip {id:desktopControlStrip;z:8000;sessionManager:manager;settings:desktop.appearance;openModule:desktop.stripModule
        extraModules:manager.statusItems ? manager.statusItems.items.map(function(i){return {command:"status:"+i.key,name:i.title,icon:i.icon,statusId:i.id}}) : []
        onModuleRequested:function(module,anchor){desktop.openStripModule(module,anchor)}}
    // A tray item that disappears takes its open menu with it.
    Connections {
        target:manager.statusItems
        function onItemsChanged() {
            if(desktop.stripModule.startsWith("status:") &&
               !manager.statusItems.items.some(function(i){return "status:"+i.key===desktop.stripModule}))desktop.closeMenu()
        }
    }
    // A brief level display for the hardware keys, in the Platinum style.
    Rectangle {
        id: levelIndicator
        objectName:"level-indicator"
        property string control:"volume"
        property int value:0
        property bool muted:false
        visible:false;z:9500;width:236;height:64;color:"#ddd";border.color:"#222"
        x:manager.primaryDisplayRect.x+(manager.primaryDisplayRect.width-width)/2
        y:manager.primaryDisplayRect.y+manager.primaryDisplayRect.height-height-96
        Rectangle{x:1;y:1;width:parent.width-2;height:1;color:"white"}
        Rectangle{x:1;y:1;width:1;height:parent.height-2;color:"white"}
        Row {
            x:14;anchors.verticalCenter:parent.verticalCenter;spacing:12
            StripGlyph {width:32;height:32;kind:levelIndicator.control==="brightness"?"monitors":"sound";anchors.verticalCenter:parent.verticalCenter}
            Column {
                spacing:5
                Text {objectName:"level-indicator-label";font.pixelSize:12
                      text:levelIndicator.control==="brightness"?"Brightness":levelIndicator.muted?"Volume (muted)":"Volume"}
                Row {
                    spacing:2
                    Repeater {
                        model:16
                        Rectangle {
                            required property int index
                            width:9;height:14;border.color:"#222"
                            color:!levelIndicator.muted && index<Math.round(levelIndicator.value*16/100)?desktop.menuAccent:"white"
                        }
                    }
                }
            }
        }
        Timer {id:levelTimer;interval:1500;onTriggered:levelIndicator.visible=false}
    }
    NotificationAlerts {z:9000;server:manager.notifications;x:manager.primaryDisplayRect.x+manager.primaryDisplayRect.width-width-8;y:manager.primaryDisplayRect.y+34}
    ShellDialog {
        id:nameDialog;objectName:"desktop-name-dialog";sessionManager:manager;width:400
        property string operation:"create"
        property string targetKey:""
        standardButtons:Dialog.Ok | Dialog.Cancel
        contentItem:Column {
            spacing:12
            Text {text:"Name:";textFormat:Text.PlainText;font.pixelSize:12}
            TextField {
                id:nameField;objectName:"desktop-name-field";width:parent.width;selectByMouse:true;font.pixelSize:12
                color:"black";selectionColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightColor : "#ccccff"; selectedTextColor: typeof manager !== "undefined" && manager ? manager.appearance.highlightTextColor : "black"
                background:FieldBackground{}
                onAccepted:nameDialog.accept()
                Keys.onPressed:function(event){if(event.key===Qt.Key_A && (event.modifiers & Qt.MetaModifier)){selectAll();event.accepted=true}}
            }
        }
        onOpened:{nameField.forceActiveFocus();nameField.selectAll()}
        onAccepted:if(operation==="rename")desktopFiles.renameItem(targetKey,nameField.text);else desktopFiles.createFolder(nameField.text)
    }
    ShellDialog {
        id:emptyTrashDialog;objectName:"empty-trash-dialog";sessionManager:manager;width:460;title:"Empty Trash"
        alertSound:true
        property string message:""
        cancelIsDefault:true
        standardButtons:Dialog.Ok | Dialog.Cancel
        contentItem:Text {text:emptyTrashDialog.message;textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
        onAccepted:{desktop.interfaceSound("ftrs");desktopFiles.emptyTrash()}
    }
    ShellDialog {
        id:forgetBluetoothDialog;objectName:"bluetooth-forget-dialog";sessionManager:manager;width:440;title:"Forget Device"
        alertSound:true
        property string path:"";property string name:""
        cancelIsDefault:true
        standardButtons:Dialog.Ok | Dialog.Cancel
        contentItem:Text {text:"Forget “"+forgetBluetoothDialog.name+"”? To use it again, you will need to pair it again.";textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
        onAccepted:bluetoothPanel.service.forgetDevice(path)
    }
    ShellDialog {
        id:messageDialog;objectName:"desktop-message-dialog";sessionManager:manager;width:460;title:"Platinum Reality"
        alertSound:true
        standardButtons:Dialog.Ok
        contentItem:Text {text:desktop.statusMessage;textFormat:Text.PlainText;wrapMode:Text.WordWrap;font.pixelSize:12}
    }
    Connections {target:messageDialog;function onClosed(){desktop.statusMessage=""}}
}
