import QtQuick

// Original geometric drawings; no Apple artwork or font glyph icons. A user's own icon in
// <personalFolder>/icons (for example imported from their Mac OS 9 by
// scripts/import-macos9-assets.py) is drawn instead when present, darkened when selected.
Canvas {
    id: icon
    required property string kind
    property bool selected: false
    property string personalFolder: ""
    property bool full: false
    readonly property string personalName: ({folder: "folder", application: "application", launcher: "application", disk: "hard-disk",
                                              trash: full ? "trash-full" : "trash", file: "document"})[kind] || ""
    readonly property string personal: personalFolder !== "" && personalName !== ""
                                       ? "file://" + encodeURI(personalFolder + "/icons/" + personalName + ".png") : ""
    width: 34; height: 36
    onPersonalChanged: if (personal !== "") loadImage(personal)
    Component.onCompleted: if (personal !== "") loadImage(personal)
    onImageLoaded: requestPaint()
    onKindChanged: requestPaint()
    // Delegates created inside a hidden menu must still paint once shown.
    onAvailableChanged: if (available) requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onSelectedChanged: requestPaint()
    onPaint: {
        let c=getContext("2d")
        c.clearRect(0,0,width,height)
        if(personal!=="" && isImageLoaded(personal)) {
            c.drawImage(personal,1,2,32,32)
            if(selected){c.globalCompositeOperation="source-atop";c.fillStyle="rgba(0,0,48,0.45)";c.fillRect(0,0,width,height);c.globalCompositeOperation="source-over"}
            return
        }
        c.lineWidth=1;c.strokeStyle=selected?"#10102a":"#222"
        let fill=selected?"#9292bd":"#dedede"
        if(kind==="folder") {
            c.fillStyle=selected?"#9394b5":"#c1cad6"
            c.fillRect(2,9,30,23);c.strokeRect(2.5,9.5,29,22)
            c.fillRect(2,5,13,7);c.strokeRect(2.5,5.5,12,6)
            c.fillStyle=selected?"#a6a7c8":"#d3d9e2";c.fillRect(3,13,28,18)
            c.strokeRect(2.5,12.5,29,19)
        } else if(kind==="appearance") {
            c.fillStyle=fill;c.fillRect(2,4,30,28);c.strokeRect(2.5,4.5,29,27)
            c.fillStyle="#777"
            for(let x=9;x<30;x+=8){c.fillRect(x,9,1,18);let y=x===9?12:x===17?20:15;c.fillStyle="#ddd";c.fillRect(x-3,y,7,5);c.strokeRect(x-2.5,y+.5,6,4);c.fillStyle="#777"}
        } else if(kind==="application" || kind==="launcher") {
            c.fillStyle=fill;c.fillRect(2,5,30,27);c.strokeRect(2.5,5.5,29,26)
            c.fillStyle=selected?"#77779b":"#bbb";c.fillRect(3,6,28,5)
            c.fillStyle=selected?"#b0b0ca":"white";c.fillRect(5,14,24,15)
            c.strokeRect(5.5,14.5,23,14)
            c.beginPath();c.moveTo(8,25);c.lineTo(14,19);c.lineTo(18,24);c.lineTo(22,20);c.lineTo(26,25);c.stroke()
        } else if(kind==="disk") {
            c.fillStyle=fill;c.fillRect(2,5,30,26);c.strokeRect(2.5,5.5,29,25)
            c.fillStyle=selected?"#676784":"#aaa";c.fillRect(3,24,28,6)
            c.strokeRect(5.5,11.5,22,8);c.fillStyle="#444";c.fillRect(6,14,21,2)
            c.fillStyle="#515";c.fillRect(25,26,3,2)
        } else if(kind==="trash") {
            c.fillStyle=fill;c.fillRect(7,9,22,24);c.strokeRect(7.5,9.5,21,23)
            c.fillRect(4,6,28,4);c.strokeRect(4.5,6.5,27,3)
            c.strokeRect(12.5,3.5,11,3)
            for(let x=11;x<28;x+=5){c.beginPath();c.moveTo(x+.5,13);c.lineTo(x+.5,30);c.stroke()}
        } else {
            c.fillStyle=selected?"#a2a2be":"#fff";c.fillRect(6,3,23,30);c.strokeRect(6.5,3.5,22,29)
            c.strokeRect(22.5,3.5,6,7)
            c.fillStyle=selected?"#595979":"#aaa"
            for(let y=14;y<28;y+=4)c.fillRect(10,y,15,1)
            if(kind==="link") {
                c.fillStyle="#fff";c.fillRect(1,23,13,12);c.strokeRect(1.5,23.5,12,11)
                c.fillStyle="#222";c.fillRect(4,29,6,2);c.fillRect(8,26,2,6)
            }
        }
    }
}
