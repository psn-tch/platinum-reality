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
    property var broker: manager.menuBroker
    property var exportPopup: null
    property int exportedRoot: -1
    property var desktopFiles: manager.desktopItems
    property var appearance: manager.appearance
    // Mac OS 9 highlights menu titles and items with the Appearance accent: blue-violet by
    // default, or the graphite variation (sampled from 9.2 captures; historical review F01/F10).
    readonly property color menuAccent: desktop.appearance.accentColor
    Image {
        objectName:"desktop-wallpaper";anchors.fill:parent
        source:desktop.appearance.wallpaper
        fillMode:desktop.appearance.wallpaperLayout==="fit" ? Image.PreserveAspectFit
                 : desktop.appearance.wallpaperLayout==="center" ? Image.Pad
                 : desktop.appearance.wallpaperLayout==="tile" ? Image.Tile : Image.PreserveAspectCrop
        asynchronous:true
        sourceSize:desktop.appearance.wallpaperLayout==="center" || desktop.appearance.wallpaperLayout==="tile"
                   ? Qt.size(0,0) : Qt.size(Math.min(4096,desktop.width*manager.hostScale),Math.min(4096,desktop.height*manager.hostScale))
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
        else if(action==="app")manager.activateApplication(applicationId)
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
        else if(action==="quit")manager.quitSession()
        else if(action==="eject")manager.removableMedia.eject(desktopFiles.selectedDrive)
        else if(action==="sleep")manager.sleepComputer()
        else if(action==="restart")manager.restartComputer()
        else if(action==="shut-down")manager.shutDownComputer()
        else if(action==="about")statusMessage=manager.aboutThisComputer()
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
    function applicationsByName() {
        return manager.applications.slice().sort(function(a,b){
            return String(a.name).localeCompare(String(b.name),undefined,{sensitivity:"base"}) || a.id-b.id})
    }
    function switchApplication(step) {
        let apps=applicationsByName()
        if(apps.length<2)return
        let current=apps.findIndex(function(a){return a.active})
        let next=apps[((current<0?0:current)+step+apps.length)%apps.length]
        closeMenu()
        manager.activateApplication(next.id)
    }
    Shortcut {sequence:"Meta+Tab";enabled:!manager.shellModal && !manager.nativeDragging;onActivated:desktop.switchApplication(1)}
    Shortcut {sequences:["Meta+Shift+Tab","Meta+Shift+Backtab","Meta+Backtab"];enabled:!manager.shellModal && !manager.nativeDragging;onActivated:desktop.switchApplication(-1)}
    // Shell menus in the 9.2.2 Finder layout (docs/historical-fidelity-review.md F07/F08).
    function shellMenuEntries(index) {
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
                {text:"Control Panels",action:"control-panels",glyph:"application",children:[
                    {text:"Appearance…",action:"appearance"},
                    {text:"Bluetooth…",action:"bluetooth"},
                    {text:"Control Strip",action:"control-strip",checked:desktop.appearance.controlStripVisible},
                    {text:"Desktop Picture…",action:"desktop-picture"},
                    {text:"Monitors…",action:"monitors"},
                    {text:"Quick Controls…",action:"quick-controls"},
                    {text:"Sound…",action:"sound"}]},
                {text:"Puzzle",action:"puzzle",glyph:"application"},
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
            {text:"Duplicate",action:"duplicate",enabled:desktopFiles.canModifySelection && ready},
            {text:"Rename…",action:"rename",enabled:Boolean(desktopFiles.renameTarget.key) && ready},
            {separator:true,action:"separator"},
            {text:"Open Desktop Folder",action:"desktop-folder"}]
        // Edit as in the 9.2.2 Finder (checked in psn_tch's 9.2.2 VM): no separator before Show Clipboard.
        // Copy with icons selected copies their names as text, one per line, as the Finder does.
        // For an application without menus of its own, each command is that application's shortcut,
        // sent as Command and the key shown (command_keys.hpp); Clear sends Delete.
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
            {text:"Empty Trash…",action:"empty-trash",enabled:ready},
            {text:"Eject",key:"⌘E",action:"eject",enabled:desktopFiles.selectedDrive!=="" && manager.removableMedia.canEject(desktopFiles.selectedDrive)},
            {separator:true,action:"separator"}].concat(manager.powerAvailable ? [
            {text:"Sleep",action:"sleep"},
            {text:"Restart",action:"restart"},
            {text:"Shut Down",action:"shut-down"},
            {separator:true,action:"separator"}] : []).concat([
            {text:"Log Out",action:"quit"}])
        return [{text:"About Platinum Reality…",action:"about"}]
    }
    function openStripModule(module, anchor) {
        if(manager.shellModal)return
        if(stripModule===module){closeMenu();return}
        closeMenu(false);manager.beginMenu()
        let origin=anchor.mapToItem(desktop,0,0)
        stripAnchor=Qt.rect(origin.x,origin.y,anchor.width,anchor.height)
        stripModule=module
        if(["sound","monitors","wifi"].includes(module))quickControls.service.refresh()
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
        let radio=values.wifi||({})
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
    // Frees a pointer that a game locked or confined. Only bound while one is captured, so the
    // chord otherwise reaches the application. See docs/relative-pointer-plan.md.
    Shortcut {sequence:"Ctrl+Alt+Escape";autoRepeat:false;enabled:manager.pointerCaptured;onActivated:manager.releasePointerCapture()}
    // Super is Command (psn_tch, D06); Command-D stays Duplicate, so Applications keeps psn_tch's Alt+D.
    // The other Alt forms are their earlier explicit requests, kept as aliases.
    Shortcut {sequence:"Alt+D";autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging;onActivated:{desktop.closeMenu();applicationBrowser.browse("")}}
    Shortcut {sequences:["Meta+Q","Alt+Q"];autoRepeat:false;enabled:!manager.shellModal && !manager.menuOpen && !manager.nativeDragging;onActivated:manager.requestQuitActiveApplication()}
    Shortcut {sequences:["Meta+Shift+S","Alt+Shift+S"];autoRepeat:false;enabled:!manager.shellModal && !manager.nativeDragging;onActivated:desktop.takeScreenshot()}
    Shortcut {sequence:"Escape";enabled:manager.desktopActive && !manager.shellModal && (desktopFiles.dragging || desktopFiles.selecting);onActivated:desktopFiles.selecting ? desktopFiles.cancelSelection() : desktopFiles.cancelDrag()}
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
        function onFocusDesktop() { desktop.forceActiveFocus() }
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
            objectName:"desktop-entry-"+entryName
            z:desktopFiles.dragging && desktopEntry.isSelected ? 10000 : 0
            x:itemX;y:itemY;width:100;height:76
            DesktopIcon {anchors.horizontalCenter:parent.horizontalCenter;y:3;kind:desktopEntry.entryKind;selected:desktopEntry.isSelected && manager.desktopActive;personalFolder:manager.personalAssets;full:desktopEntry.entryKind==="trash" && desktopFiles.trashFull}
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
        Text {anchors.right:appDivider.left;anchors.rightMargin:7;anchors.verticalCenter:parent.verticalCenter;text:Qt.formatTime(desktop.clockTime,"h:mm AP");font.pixelSize:12}
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
            MouseArea{anchors.fill:parent;onClicked:desktop.showMenu(desktop.barEntries.length)}
        }
    }
    Component {
        id: shellMenuRow
        Rectangle {
            id: row
            required property var modelData
            required property int index
            property bool submenuRow: false
            width:parent?parent.width:200;height:modelData.separator?9:25
            readonly property bool lit:!modelData.separator && (entryMouse.containsMouse ||
                (!submenuRow && Boolean(modelData.children) && desktop.openSubmenu===index))
            color:lit?desktop.menuAccent:"transparent"
            objectName:"desktop-command-"+(modelData.name?modelData.name:modelData.action)
            Rectangle{visible:Boolean(modelData.separator);x:2;y:4;width:parent.width-4;height:1;color:"#888"}
            Text{visible:Boolean(modelData.checked);anchors.verticalCenter:parent.verticalCenter;x:4;text:"✓";font.pixelSize:12;color:row.lit?"white":"black"}
            // ◆: this application asks for attention (Mac OS 9's Notification Manager mark).
            Text{visible:Boolean(modelData.marked);objectName:"attention-mark-"+(modelData.text?modelData.text:"");anchors.verticalCenter:parent.verticalCenter;x:4;text:"◆";font.pixelSize:10;color:row.lit?"white":"black"}
            MenuGlyph {objectName:"menu-glyph-"+(modelData.text?modelData.text:"");visible:Boolean(modelData.glyph);x:17;anchors.verticalCenter:parent.verticalCenter;kind:modelData.glyph?modelData.glyph:"application"}
            Text{visible:!modelData.separator;anchors.verticalCenter:parent.verticalCenter;x:modelData.glyph?39:15;width:parent.width-x-20;elide:Text.ElideRight
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
                onEntered:if(!row.submenuRow){if(modelData.children)desktop.openShellSubmenu(index,modelData.children,row.mapToItem(desktop,0,0).y);else desktop.openSubmenu=-1}
                onClicked:if(modelData.children)desktop.openShellSubmenu(index,modelData.children,row.mapToItem(desktop,0,0).y);else desktop.dispatchShellCommand(modelData.action,modelData.id)}
        }
    }
    Rectangle {visible:popup.visible;z:10000.5;x:popup.x+3;y:popup.y+3;width:popup.width;height:popup.height;color:"#50000000"}
    Rectangle {
        id: popup; z: 10001; visible:desktop.openMenu>=0 && desktop.exportedRoot<0
        x:Math.max(menuBar.x,Math.min(desktop.openMenu===desktop.barEntries.length?menuBar.x+menuBar.width-width-2:desktop.menuX(desktop.openMenu),menuBar.x+menuBar.width-width))
        y:menuBar.y+25
        width:240;height:items.height+6;color:"#ddd";border.color:"#222"
        Column {
            id:items;x:3;y:3;width:parent.width-6
            Repeater {model:desktop.openMenu>=0 && desktop.exportedRoot<0 ? desktop.shellMenuEntries(desktop.openMenu) : [];delegate:shellMenuRow}
        }
    }
    Rectangle {visible:submenu.visible;z:10001.5;x:submenu.x+3;y:submenu.y+3;width:submenu.width;height:submenu.height;color:"#50000000"}
    Rectangle {
        id: submenu; objectName:"desktop-submenu"; z: 10002; visible:popup.visible && desktop.openSubmenu>=0
        x:popup.x+popup.width+width-4<=menuBar.x+menuBar.width?popup.x+popup.width-4:Math.max(menuBar.x,popup.x-width+4)
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
        width:220;height:stripItems.height+6;color:"#ddd";border.color:"#222"
        x:Math.max(menuBar.x,Math.min(desktop.stripAnchor.x,menuBar.x+menuBar.width-width))
        // Above the module (V922 E12), or below it when the strip sits under the menu bar.
        y:desktop.stripAnchor.y-height>=menuBar.y+26?desktop.stripAnchor.y-height:desktop.stripAnchor.y+desktop.stripAnchor.height
        Column {
            id:stripItems;x:3;y:3;width:parent.width-6
            Repeater {model:desktop.stripModule!=="" ? desktop.stripMenuEntries(desktop.stripModule) : [];delegate:shellMenuRow}
        }
    }
    Rectangle {
        z:5000;x:12;y:desktop.height-height-12;width:operationLabel.implicitWidth+20;height:30
        visible:desktopFiles.busy;color:"#ddd";border.color:"#333"
        Text {id:operationLabel;anchors.centerIn:parent;text:desktopFiles.operationStatus;textFormat:Text.PlainText;font.pixelSize:12}
    }
    ApplicationBrowser {id:applicationBrowser;sessionManager:manager;catalog:manager.applicationCatalog}
    AppearanceDialog {id:appearanceDialog;sessionManager:manager;settings:desktop.appearance
        // After the panel's modal input is released, so the chooser can take it.
        onPlacePictureRequested:Qt.callLater(function(){desktopPictureDialog.edit()})}
    DesktopPictureDialog {id:desktopPictureDialog;sessionManager:manager;settings:desktop.appearance}
    ScreenshotDialog {id:screenshotDialog;sessionManager:manager;tool:manager.screenshots}
    QuickControls {id:quickControls;sessionManager:manager;service:manager.systemControls;bluetooth:bluetoothPanel.service}
    SoundPanel {id:soundPanel;sessionManager:manager;service:manager.audio;alertSounds:manager.alertSounds}
    // A silent alert (alert volume 0, or no sound player) flashes the menu bar, as Mac OS 9 did.
    Timer {id:menuFlash;interval:140}
    Connections {target:manager.alertSounds;function onFlash(){menuFlash.restart()}}
    // Tests substitute bluetoothPanel.service; the alert, strip module and Quick Controls follow it.
    BluetoothPanel {id:bluetoothPanel;sessionManager:manager;service:manager.bluetooth
        onForgetRequested:function(path,name){forgetBluetoothDialog.path=path;forgetBluetoothDialog.name=name;forgetBluetoothDialog.open()}}
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
    MonitorsPanel {id:monitorsPanel;sessionManager:manager}
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
                color:"black";selectionColor:"#202050";selectedTextColor:"white"
                background:Rectangle{color:"white";border.color:"#555"}
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
