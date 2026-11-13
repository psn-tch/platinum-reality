import QtQuick

// Original Control Strip module drawings on a 16-unit grid; no Apple artwork. Coloured as 9.2.2's
// strip is (Lily Hewitt, 2026-11-12; docs/fidelity/control-strip.md): crisp black outlines,
// platinum bodies lit from the top left, and strong colour as accents: indigo screens, lavender,
// red, yellow, green and blue, and small red and green lights.
Canvas {
    id: glyph
    property string kind: "applications"
    // The battery module's charge (0-100) and whether power is connected.
    property int level: -1
    property bool charging: false
    // The workspaces module's number (1-9).
    property int number: 0
    // The camera module: the switch is Off.
    property bool off: false
    width: 20; height: 20
    onKindChanged: requestPaint()
    onLevelChanged: if (kind === "battery") requestPaint()
    onChargingChanged: if (kind === "battery") requestPaint()
    onNumberChanged: if (kind === "workspaces") requestPaint()
    onOffChanged: if (kind === "camera") requestPaint()
    onAvailableChanged: if (available) requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    // The palette, from the 9.2.2 strip capture's colours.
    readonly property var ink: ({outline: "#000", platinum: "#ddd", light: "#fff", shade: "#888", dark: "#555",
                                 indigo: "#2a1a9a", lavender: "#ccf", lavenderShade: "#99f", red: "#d00",
                                 green: "#0b0", yellow: "#fd0", orange: "#e80", blue: "#2a5ad8", gold: "#d8b84a",
                                 goldShade: "#a08020", screen: "#fff"})
    readonly property var digits: [[], [".#.","##.",".#.",".#.","###"], ["##.","..#",".#.","#..","###"],
                                   ["##.","..#",".#.","..#","##."], ["#.#","#.#","###","..#","..#"],
                                   ["###","#..","##.","..#","##."], [".##","#..","###","#.#","###"],
                                   ["###","..#",".#.",".#.",".#."], ["###","#.#","###","#.#","###"],
                                   ["###","#.#","###","..#","##."]]
    onPaint: {
        let c=getContext("2d")
        c.reset()
        c.clearRect(0,0,width,height)
        c.scale(width/16,height/16)
        const k=ink
        c.lineWidth=1;c.strokeStyle=k.outline;c.fillStyle=k.outline
        const line=function(x1,y1,x2,y2){c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.stroke()}
        const fill=function(colour,x,y,w,h){c.fillStyle=colour;c.fillRect(x,y,w,h)}
        // A raised platinum part covering pixels x..x+w-1, y..y+h-1: outline, highlight, shade.
        const box=function(x,y,w,h,colour){
            fill(colour||k.platinum,x,y,w,h)
            fill(k.light,x+1,y+1,w-3,1);fill(k.light,x+1,y+1,1,h-3)
            fill(k.shade,x+2,y+h-2,w-3,1);fill(k.shade,x+w-2,y+2,1,h-3)
            c.strokeStyle=k.outline;c.strokeRect(x+.5,y+.5,w-1,h-1)
        }
        // A screen set into a platinum case, with the case's light in the corner.
        const screen=function(x,y,w,h,colour,led){
            box(x,y,w,h)
            fill(k.outline,x+2,y+2,w-4,h-5);fill(colour,x+3,y+3,w-6,h-7)
            if(led)fill(led,x+2,y+h-2,2,1)
        }
        const disc=function(x,y,r,colour){c.fillStyle=colour;c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.fill();c.stroke()}
        if(kind==="finder") {
            box(1,4,14,9);fill(k.shade,2,10,12,2);fill(k.green,11,10,2,1)
        } else if(kind==="workspaces") {
            // A screen with the number of the workspace showing on it.
            screen(1,1,14,12,k.indigo,k.green)
            fill(k.outline,6,13,4,1);fill(k.outline,4,14,8,2);fill(k.platinum,5,14,6,1)
            // The digit in our own 3x5 pixel figures, crisp at 16 px.
            const figure=digits[number]||[]
            for(let y=0;y<figure.length;++y)for(let x=0;x<3;++x)if(figure[y][x]==="#")fill(k.light,7+x,4+y,1,1)
        } else if(kind==="camera") {
            // A webcam on its stand; Off closes the lens and strikes it through.
            disc(8,6.5,5.5,k.platinum)
            c.strokeStyle=k.light;c.beginPath();c.arc(8,6.5,4.5,Math.PI*0.9,Math.PI*1.6);c.stroke();c.strokeStyle=k.outline
            disc(8,6.5,2.5,off?k.shade:k.indigo)
            if(!off)fill(k.light,7,5,1,1)
            fill(off?k.red:k.green,12,11,1,1)
            fill(k.outline,7,12,2,2);fill(k.outline,4,14,8,2);fill(k.platinum,5,14,6,1)
            if(off){c.strokeStyle=k.red;c.lineWidth=2;line(2.5,14,13.5,1.5)}
        } else if(kind==="clipboard") {
            // A clipboard: a brown board, its silver clip, and a sheet of writing.
            box(2,2,12,13,"#b07a3c")
            box(4,4,8,10,k.light)
            for(const y of [7,9,11])fill(k.shade,5,y,y===11?4:6,1)
            box(5,1,6,4,"#ccc")
            fill(k.outline,7,2,2,1)
        } else if(kind==="applications") {
            // A window with its striped title bar, and an application inside.
            box(1,2,14,12,k.light)
            fill(k.platinum,2,3,12,3);for(const y of [3,5])fill(k.shade,3,y,10,1)
            fill(k.outline,2,6,12,1)
            c.fillStyle=k.lavenderShade;c.beginPath();c.moveTo(8,7.5);c.lineTo(11.5,10);c.lineTo(8,12.5);c.lineTo(4.5,10);c.closePath();c.fill();c.stroke()
        } else if(kind==="appearance") {
            // Three sliders with lavender thumbs: the Appearance theme colour.
            for(const x of [4,8,12]){fill(k.dark,x,2,1,12);fill(k.light,x+1,2,1,12)}
            for(const [x,y] of [[2,9],[6,4],[10,7]])box(x,y,5,4,k.lavender)
        } else if(kind==="sound") {
            fill(k.outline,1,5,4,6);fill(k.platinum,2,6,2,4)
            c.fillStyle=k.dark;c.beginPath();c.moveTo(5,5.5);c.lineTo(9.5,1.5);c.lineTo(9.5,14.5);c.lineTo(5,10.5);c.closePath();c.fill();c.stroke()
            c.strokeStyle=k.blue;c.lineWidth=1.3
            c.beginPath();c.arc(9,8,3.5,-0.9,0.9);c.stroke()
            c.beginPath();c.arc(9,8,6,-0.9,0.9);c.stroke()
        } else if(kind==="monitors") {
            // Brightness: the sun.
            c.strokeStyle=k.orange;c.lineWidth=1.5
            for(let i=0;i<8;++i){const a=i*Math.PI/4;line(8+Math.cos(a)*4.5,8+Math.sin(a)*4.5,8+Math.cos(a)*7,8+Math.sin(a)*7)}
            c.lineWidth=1;c.strokeStyle=k.outline;disc(8,8,3.5,k.yellow)
            fill(k.light,6,6,2,1)
        } else if(kind==="keychain") {
            // Keychain: a brass padlock.
            c.lineWidth=2;c.strokeStyle=k.outline;c.beginPath();c.arc(8,6.5,3.5,Math.PI,0);c.stroke()
            c.lineWidth=1;c.strokeStyle=k.shade;c.beginPath();c.arc(8,6.5,3.5,Math.PI,0);c.stroke()
            fill(k.outline,4,6,1,2);fill(k.outline,11,6,1,2)
            box(2,7,12,8,k.gold)
            for(const y of [10,12])fill(k.goldShade,3,y,10,1)
            fill(k.outline,7,9,2,4)
        } else if(kind==="printer") {
            // Printer Selector: a desktop printer printing in colour, its light on.
            box(4,1,8,6,k.light)
            fill(k.red,5,3,5,1);fill(k.blue,5,5,4,1)
            box(1,6,14,7)
            fill(k.green,3,10,2,1)
            box(4,10,8,5,k.light);fill(k.yellow,5,12,5,1)
        } else if(kind==="resolution") {
            // Monitor Resolution: a screen of fine and coarse squares.
            screen(1,1,14,12,k.screen,k.red)
            fill(k.indigo,4,4,8,5)
            for(let y=0;y<5;++y)for(let x=0;x<8;++x)if((x+y)%2===0)fill(k.light,4+x,4+y,1,1)
            fill(k.indigo,9,6,3,3)
            fill(k.outline,6,13,4,1);fill(k.outline,4,14,8,2);fill(k.platinum,5,14,6,1)
        } else if(kind==="display") {
            // A monitor on its stand.
            screen(1,1,14,12,k.blue,k.green)
            fill(k.outline,6,13,4,1);fill(k.outline,4,14,8,2);fill(k.platinum,5,14,6,1)
        } else if(kind==="picture") {
            // A framed landscape: sky, a hill and the sun.
            box(1,2,14,12)
            fill(k.outline,3,4,10,8);fill("#9cc0f0",3,4,10,8)
            c.fillStyle="#2a8a2a";c.beginPath();c.moveTo(3,12);c.lineTo(7,7);c.lineTo(10,10);c.lineTo(11.5,8.5);c.lineTo(13,10);c.lineTo(13,12);c.closePath();c.fill()
            c.fillStyle=k.yellow;c.beginPath();c.arc(11,6,1.5,0,Math.PI*2);c.fill()
        } else if(kind==="general") {
            // A desktop window above a slider: General Controls.
            box(1,1,14,10,k.light)
            fill(k.outline,2,3,12,1);fill(k.lavenderShade,2,4,12,6)
            line(2,13.5,14,13.5)
            c.fillStyle=k.lavender;c.beginPath();c.moveTo(8.5,11);c.lineTo(11.5,11);c.lineTo(11.5,13.5);c.lineTo(10,15);c.lineTo(8.5,13.5);c.closePath();c.fill();c.stroke()
        } else if(kind==="clock") {
            disc(8,8,6.5,k.light)
            c.strokeStyle=k.outline
            for(let i=0;i<12;i+=3){const a=i*Math.PI/6;line(8+Math.cos(a)*5,8+Math.sin(a)*5,8+Math.cos(a)*6,8+Math.sin(a)*6)}
            c.lineWidth=1.3;line(8,8,8,4);line(8,8,11,9.5)
            c.lineWidth=1;c.strokeStyle=k.red;line(8,8,5,11)
        } else if(kind==="energy") {
            // A plug: two prongs, the body and its cord.
            fill(k.shade,5,1,2,4);fill(k.shade,9,1,2,4);c.strokeRect(5.5,1.5,1,3);c.strokeRect(9.5,1.5,1,3)
            box(3,5,10,6)
            fill(k.green,7,7,2,1)
            fill(k.outline,6,11,4,1)
            c.lineWidth=1.5;c.beginPath();c.moveTo(8,12);c.bezierCurveTo(8,15,4,13,3,15.5);c.stroke()
        } else if(kind==="sleep") {
            // Energy Settings: a crescent moon and stars on a night sky.
            fill(k.outline,1,1,14,14);fill(k.indigo,2,2,12,12)
            c.fillStyle=k.light;c.beginPath();c.arc(8,8,4.5,0,Math.PI*2);c.fill()
            c.fillStyle=k.indigo;c.beginPath();c.arc(10,6.5,4,0,Math.PI*2);c.fill()
            for(const [x,y] of [[11,10],[4,3],[12,3],[3,12]])fill(k.light,x,y,1,1)
            fill(k.green,12,12,1,1);fill(k.yellow,6,12,1,1)
        } else if(kind==="trackpad") {
            box(1,3,14,10)
            line(1.5,10.5,14.5,10.5);line(8,10.5,8,12.5)
            fill(k.lavender,3,5,10,4);disc(6,6.5,1,k.outline)
        } else if(kind==="battery") {
            // A battery on its side, filled to the charge; a bolt while plugged in.
            box(1,4,13,8,k.light)
            fill(k.outline,14,6,2,4)
            const charge=Math.max(0,Math.min(100,level))/100*10
            fill(level>=0 && level<=10 && !charging?k.red:k.green,2,5,charge,6)
            if(charging){c.fillStyle=k.yellow;c.strokeStyle=k.outline
                c.beginPath();c.moveTo(8.5,3);c.lineTo(5.5,8.5);c.lineTo(8,8.5);c.lineTo(6.5,13);c.lineTo(10.5,7);c.lineTo(8,7);c.lineTo(9.5,3);c.closePath();c.fill();c.stroke()}
        } else if(kind==="wifi") {
            disc(8,13,1.2,k.outline)
            c.lineWidth=1.6
            for(const r of [4,7,10]){c.strokeStyle=k.outline;c.beginPath();c.arc(8,13,r,-Math.PI*0.75,-Math.PI*0.25);c.stroke()}
            c.lineWidth=0.8
            for(const r of [4,7,10]){c.strokeStyle=k.blue;c.beginPath();c.arc(8,13,r,-Math.PI*0.75,-Math.PI*0.25);c.stroke()}
        } else if(kind==="status") {
            // An application without a usable icon: a small generic window.
            box(2,3,12,10,k.light)
            fill(k.platinum,3,4,9,2);fill(k.shade,4,4,7,1)
        } else if(kind==="bluetooth" || kind==="device") {
            // The rune on its blue badge.
            c.fillStyle=k.blue;c.beginPath();c.moveTo(8,0.5);c.bezierCurveTo(14,0.5,14,15.5,8,15.5);c.bezierCurveTo(2,15.5,2,0.5,8,0.5);c.fill();c.stroke()
            c.strokeStyle=k.light;c.lineWidth=1.2
            c.beginPath();c.moveTo(5,5);c.lineTo(10.5,10.5);c.lineTo(8,13);c.lineTo(8,3);c.lineTo(10.5,5.5);c.lineTo(5,11);c.stroke()
        // Bluetooth device kinds for the Bluetooth panel.
        } else if(kind==="headphones") {
            c.lineWidth=1.5;c.beginPath();c.arc(8,9,5.5,Math.PI,0);c.stroke();c.lineWidth=1
            box(1,8,4,7,k.lavender);box(11,8,4,7,k.lavender)
        } else if(kind==="speaker") {
            box(3,1,10,14)
            disc(8,10,2.5,k.dark);fill(k.shade,7,9,1,1)
            disc(8,5,1.2,k.dark)
            fill(k.green,10,2,1,1)
        } else if(kind==="mouse") {
            c.fillStyle=k.platinum;c.beginPath();c.moveTo(4.5,6);c.arc(8,6,3.5,Math.PI,0);c.lineTo(11.5,10.5)
            c.arc(8,10.5,3.5,0,Math.PI);c.closePath();c.fill();c.stroke()
            line(8,2.5,8,6.5);line(4.5,6.5,11.5,6.5)
            fill(k.light,5,7,1,4)
        } else if(kind==="keyboard") {
            box(1,4,14,9)
            for(let y of [6,8])for(let x=3;x<=12;x+=2)fill(k.dark,x,y,1,1)
            fill(k.dark,5,10,6,1);fill(k.green,12,5,1,1)
        } else if(kind==="gamepad" || kind==="handheld") {
            box(1,5,14,7)
            fill(k.outline,2,11,3,3);fill(k.outline,11,11,3,3)
            fill(k.outline,3,8,3,1);fill(k.outline,4,7,1,3)
            fill(k.red,11,7,1,1);fill(k.blue,12,8,1,1);fill(k.green,10,8,1,1);fill(k.yellow,11,9,1,1)
        } else if(kind==="fingerprint") {
            // An original whorl: nested open loops, as a fingertip's ridges.
            c.lineWidth=1.1;c.strokeStyle=k.indigo
            for(const r of [1.5,3.5,5.5]){c.beginPath();c.arc(8,8.5,r,Math.PI*0.85,Math.PI*2.35);c.stroke()}
            c.beginPath();c.arc(8,8.5,7.2,Math.PI*1.05,Math.PI*1.95);c.stroke()
            line(8,8.5,8,12)
        } else if(kind==="phone") {
            box(4,1,8,14)
            fill(k.outline,5,3,6,8);fill(k.indigo,6,4,4,6)
            fill(k.dark,7,12,2,1)
        } else if(kind==="computer") {
            screen(1,1,14,11,k.indigo,k.green)
            fill(k.outline,6,12,4,1);fill(k.outline,4,13,8,2);fill(k.platinum,5,13,6,1)
        }
    }
}
