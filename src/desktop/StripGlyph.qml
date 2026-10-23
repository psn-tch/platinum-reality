import QtQuick

// Original Control Strip module drawings on a 16-unit grid; no Apple artwork.
Canvas {
    id: glyph
    property string kind: "applications"
    // The battery module's charge (0-100) and whether power is connected.
    property int level: -1
    property bool charging: false
    width: 20; height: 20
    onKindChanged: requestPaint()
    onLevelChanged: if (kind === "battery") requestPaint()
    onChargingChanged: if (kind === "battery") requestPaint()
    onAvailableChanged: if (available) requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onPaint: {
        let c=getContext("2d")
        c.reset()
        c.clearRect(0,0,width,height)
        c.scale(width/16,height/16)
        c.lineWidth=1;c.strokeStyle="#222";c.fillStyle="#222"
        const line=function(x1,y1,x2,y2){c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.stroke()}
        if(kind==="finder") {
            c.fillStyle="#ddd";c.fillRect(1.5,4.5,13,8);c.strokeRect(1.5,4.5,13,8)
            c.fillStyle="#999";c.fillRect(2,10,12,2);c.fillStyle="#2a2";c.fillRect(11,10,2,1)
        } else if(kind==="applications") {
            c.fillStyle="#eee";c.fillRect(1.5,2.5,13,11);c.strokeRect(1.5,2.5,13,11)
            c.fillStyle="#999";c.fillRect(2,3,12,3);c.fillStyle="#777";c.fillRect(3,8,10,1);c.fillRect(3,10,7,1)
        } else if(kind==="appearance") {
            line(4.5,2,4.5,14);line(8.5,2,8.5,14);line(12.5,2,12.5,14)
            c.fillStyle="#ddd"
            for(const [x,y] of [[2,9],[6,4],[10,7]]){c.fillRect(x+.5,y+.5,4,3);c.strokeRect(x+.5,y+.5,4,3)}
        } else if(kind==="sound") {
            c.fillRect(2,6,3,4)
            c.beginPath();c.moveTo(5,6);c.lineTo(9,2);c.lineTo(9,14);c.lineTo(5,10);c.closePath();c.fill()
            c.beginPath();c.arc(9,8,3.5,-0.9,0.9);c.stroke()
            c.beginPath();c.arc(9,8,6,-0.9,0.9);c.stroke()
        } else if(kind==="monitors") {
            c.beginPath();c.arc(8,8,3,0,Math.PI*2);c.fill()
            for(let i=0;i<8;++i){const a=i*Math.PI/4;line(8+Math.cos(a)*4.5,8+Math.sin(a)*4.5,8+Math.cos(a)*6.5,8+Math.sin(a)*6.5)}
        } else if(kind==="battery") {
            // A battery on its side, filled to the charge; a bolt while plugged in.
            c.fillStyle="#eee";c.fillRect(1.5,4.5,12,7);c.strokeRect(1.5,4.5,12,7)
            c.fillStyle="#222";c.fillRect(13.5,6.5,1.5,3)
            const fill=Math.max(0,Math.min(100,level))/100*10
            c.fillStyle=level>=0 && level<=10 && !charging?"#c00":"#444";c.fillRect(3,6,fill,4)
            if(charging){c.fillStyle="#fc0";c.strokeStyle="#222"
                c.beginPath();c.moveTo(8.5,3);c.lineTo(5.5,8.5);c.lineTo(8,8.5);c.lineTo(6.5,13);c.lineTo(10.5,7);c.lineTo(8,7);c.lineTo(9.5,3);c.closePath();c.fill();c.stroke()}
        } else if(kind==="wifi") {
            c.beginPath();c.arc(8,13,1.2,0,Math.PI*2);c.fill()
            for(const r of [4,7,10]){c.beginPath();c.arc(8,13,r,-Math.PI*0.75,-Math.PI*0.25);c.stroke()}
        } else if(kind==="status") {
            // An application without a usable icon: a small generic window.
            c.fillStyle="#eee";c.fillRect(2.5,3.5,11,9);c.strokeRect(2.5,3.5,11,9)
            c.fillStyle="#777";c.fillRect(3,4,10,2)
        } else if(kind==="bluetooth" || kind==="device") {
            c.beginPath();c.moveTo(4.5,5);c.lineTo(11.5,11);c.lineTo(8,14.5);c.lineTo(8,1.5);c.lineTo(11.5,5);c.lineTo(4.5,11);c.stroke()
        // Bluetooth device kinds for the Bluetooth panel.
        } else if(kind==="headphones") {
            c.beginPath();c.arc(8,9,5.5,Math.PI,0);c.stroke()
            c.fillRect(1.5,9,3,5);c.fillRect(11.5,9,3,5)
        } else if(kind==="speaker") {
            c.fillStyle="#ddd";c.fillRect(3.5,1.5,9,13);c.strokeRect(3.5,1.5,9,13)
            c.beginPath();c.arc(8,10,2.5,0,Math.PI*2);c.stroke()
            c.fillStyle="#222";c.beginPath();c.arc(8,5,1,0,Math.PI*2);c.fill()
        } else if(kind==="mouse") {
            c.fillStyle="#ddd";c.beginPath();c.moveTo(4.5,6);c.arc(8,6,3.5,Math.PI,0);c.lineTo(11.5,10.5)
            c.arc(8,10.5,3.5,0,Math.PI);c.closePath();c.fill();c.stroke()
            line(8,2.5,8,6.5);line(4.5,6.5,11.5,6.5)
        } else if(kind==="keyboard") {
            c.fillStyle="#ddd";c.fillRect(1.5,4.5,13,8);c.strokeRect(1.5,4.5,13,8);c.fillStyle="#222"
            for(let y of [6,8])for(let x=3;x<=12;x+=2)c.fillRect(x,y,1,1)
            c.fillRect(5,10,6,1)
        } else if(kind==="gamepad") {
            c.fillStyle="#ddd";c.fillRect(1.5,5.5,13,6);c.strokeRect(1.5,5.5,13,6)
            c.fillStyle="#222";c.fillRect(2,11,3,2.5);c.fillRect(11,11,3,2.5)
            c.fillRect(3,8,3,1);c.fillRect(4,7,1,3);c.fillRect(11,7,1,1);c.fillRect(12,9,1,1)
        } else if(kind==="phone") {
            c.fillStyle="#ddd";c.fillRect(4.5,1.5,7,13);c.strokeRect(4.5,1.5,7,13)
            c.fillStyle="#999";c.fillRect(5,3,6,8);c.fillStyle="#222";c.fillRect(7,12,2,1)
        } else if(kind==="computer") {
            c.fillStyle="#999";c.fillRect(1.5,2.5,13,9);c.strokeRect(1.5,2.5,13,9)
            c.fillStyle="#222";c.fillRect(6,12,4,1);c.fillRect(4,13,8,1)
        }
    }
}
