"""Actual Apple-menu -> Monitors interaction in a reviewed owned VM geometry.

Opens the panel only; never changes a display setting or claims persistence.
"""
import csv,importlib.util,io,json
from pathlib import Path
import subprocess,time

spec=importlib.util.spec_from_file_location('gui_match',Path(__file__).with_name('gui-drive.py'))
gui=importlib.util.module_from_spec(spec);spec.loader.exec_module(gui)


def input_canvas(observed,size):
    topology=observed['topology'];outputs=topology['outputs']
    if not outputs or any(o.get('scale')!=1 or o.get('transform')!='normal' for o in outputs):
        raise ValueError('Monitors UI fixture requires unrotated unit-scale outputs')
    primary=[o for o in outputs if o.get('name')==topology.get('primary')]
    if len(primary)!=1 or (primary[0]['x'],primary[0]['y'])!=(0,0):
        raise ValueError('Monitors UI fixture requires its primary screen at the canvas origin')
    if (primary[0]['width'],primary[0]['height'])!=size or size not in ((1280,800),(1024,768)):
        raise ValueError('Unreviewed primary-screen screenshot geometry')
    if any(min(o['x'],o['y'])<0 for o in outputs):raise ValueError('Negative output coordinates are unreviewed')
    return (max(o['x']+o['width'] for o in outputs),max(o['y']+o['height'] for o in outputs))


def capture(guest,stem):
    from PIL import Image
    path=guest.path/(stem+'.ppm');guest.qmp('screendump',{'filename':str(path)})
    with Image.open(path) as image:
        size=image.size
        if size not in ((1280,800),(1024,768)):raise ValueError('Unreviewed Monitors screenshot geometry')
        enlarged=guest.path/(stem+'-ocr.png');image.resize((size[0]*2,size[1]*2),Image.Resampling.LANCZOS).save(enlarged)
    def read_region(image_path,tag,psm,offset=(0,0)):
        text=subprocess.check_output(['tesseract',str(image_path),'stdout','--psm',str(psm),'tsv'],
                                     stderr=subprocess.DEVNULL,text=True)
        (guest.path/(stem+tag+'.tsv')).write_text(text)
        found=[]
        for row in csv.DictReader(io.StringIO(text),delimiter='\t',quoting=csv.QUOTE_NONE):
            if row['level']!='5' or not row['text'].strip():continue
            found.append({'text':row['text'],'x':round(int(row['left'])/2)+offset[0],
                          'y':round(int(row['top'])/2)+offset[1],
                          'w':max(1,round(int(row['width'])/2)),'h':max(1,round(int(row['height'])/2)),
                          'line':(tag,)+tuple(row[k] for k in ('page_num','block_num','par_num','line_num'))})
        return found
    words=[w for w in read_region(enlarged,'',11) if w['y']>=26]
    # The full-width menu-bar border confuses sparse full-frame segmentation.
    # Read its actual text interior separately; never invent missing labels.
    menu=guest.path/(stem+'-menubar.png')
    with Image.open(path) as image:
        image.crop((42,2,430,22)).resize((776,40),Image.Resampling.LANCZOS).save(menu)
    words+=read_region(menu,'-menubar',7,(42,2))
    return size,words


def tap(guest,x,y,canvas):
    width,height=canvas
    if not (0<=x<width<=16384 and 0<=y<height<=16384):raise ValueError('Pointer outside owned display canvas')
    guest.qmp('input-send-event',{'events':[
        {'type':'abs','data':{'axis':'x','value':round(x*32767/(width-1))}},
        {'type':'abs','data':{'axis':'y','value':round(y*32767/(height-1))}},
        {'type':'btn','data':{'button':'left','down':True}}]})
    guest.qmp('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':False}}]})
    time.sleep(.2)


def wait(guest,stem,phrase):
    deadline=time.monotonic()+8
    while True:
        size,words=capture(guest,stem);found=gui.matches(words,phrase)
        if len(found)==1:return size,words,found[0]
        if time.monotonic()>=deadline:raise RuntimeError('Actual Monitors UI did not uniquely show '+phrase)
        time.sleep(.25)


def open_panel(guest,observed):
    size,words=capture(guest,'monitors-00-desktop')
    if not all(gui.matches(words,s) for s in ('File','Edit','View')):
        raise RuntimeError('Desktop menu bar not recognized; no pointer input sent')
    canvas=input_canvas(observed,size)
    tap(guest,13,11,canvas)
    _,_,point=wait(guest,'monitors-01-apple','Control Panels');tap(guest,point['x'],point['y'],canvas)
    _,_,point=wait(guest,'monitors-02-controls','Monitors');tap(guest,point['x'],point['y'],canvas)
    _,words,_=wait(guest,'monitors-03-panel','Mirror displays')
    if not gui.matches(words,'Show the desktop on'):
        raise RuntimeError('Opened panel did not show actual managed display controls')
    return {'opened':True,'route':'Actual Apple menu -> Control Panels -> Monitors through QMP pointer events',
            'screenshot':'monitors-03-panel.ppm','input_canvas':list(canvas),
            'scope':'Panel opening/visible controls only; settings, mirroring and persistence NOT TESTED',
            'visual_review':'REQUIRED'}


def enabled_duplicate_label(frame,labels):
    """Disambiguate two identical monitor labels by their actual enabled ink.

    With exactly one active display, its checkbox is disabled (grey #888) to
    keep the last display on. The off monitor's enabled label is #111. Accept
    only two aligned, adjacent rows and that exact visible distinction. An
    unknown, clipped, or differently styled frame refuses before any click.
    """
    from PIL import Image
    if len(labels)!=2:return None
    first,second=sorted(labels,key=lambda p:p['y'])
    if abs(first['left']-second['left'])>2 or not 18<=second['y']-first['y']<=26:return None
    inks=[]
    with Image.open(frame) as source:
        image=source.convert('RGB')
        for point in (first,second):
            box=(point['left'],point['top'],2*point['x']-point['left'],2*point['y']-point['top'])
            if not (0<=box[0]<box[2]<=image.width and 0<=box[1]<box[3]<=image.height):return None
            # Subpixel antialiasing can give the darkest channels different
            # values; the minimum channel still reaches the actual ink level.
            ink=min(low for low,high in image.crop(box).getextrema())
            inks.append(ink)
    if sorted(inks)!=[17,136]:return None
    return (first,second)[inks.index(17)]


def enable_second(guest,observed,recollect):
    """Turn on one actual connected/off display using its checkbox and Keep.

    Owned live-VM fixture only. Unlike open_panel, this deliberately changes
    that VM's display preference. No backend/control-socket mutation is used.
    """
    active=[o for o in observed['backend_outputs'] if o.get('active') is True]
    off=observed['topology'].get('available',[])
    connected=[o for o in observed['backend_outputs'] if not o.get('non_desktop',False)]
    if (len(active)!=1 or len(off)!=1 or len(connected)!=2 or
            off[0].get('name') not in [o.get('name') for o in connected if o.get('active') is False] or
            not isinstance(off[0].get('label'),str) or not off[0]['label'].strip()):
        raise ValueError('Need exactly one actual connected/off monitor and one active monitor')
    size,words=capture(guest,'monitors-04-before-enable')
    canvas=input_canvas(observed,size)
    anchor=gui.matches(words,'Show the desktop on')
    labels=gui.matches(words,off[0]['label'])
    labels=[p for p in labels if len(anchor)==1 and anchor[0]['y']<p['y']<anchor[0]['y']+90]
    if len(labels)==2:
        enabled=enabled_duplicate_label(guest.path/'monitors-04-before-enable.ppm',labels)
        labels=[enabled] if enabled else []
    if len(labels)!=1:raise RuntimeError('Actual off-monitor checkbox label was not unique below its heading')
    tap(guest,labels[0]['x'],labels[0]['y'],canvas)
    names={o['name'] for o in connected}
    def settled(record,pending):
        topology=record['topology'];generation=record.get('display_generation')
        return (type(generation) is int and generation>0 and
                generation==record.get('display_applied_generation')==topology.get('generation') and
                topology.get('mode')=='extend' and topology.get('pending') is pending and
                {o['name'] for o in record['backend_outputs'] if o.get('active') is True}==names and
                {o['name'] for o in topology.get('outputs',[])}==names)
    deadline=time.monotonic()+8
    while True:
        changed=recollect()
        if settled(changed,True):break
        if time.monotonic()>=deadline:raise RuntimeError('Actual second display did not become active with an acknowledged pending layout')
        time.sleep(.1)
    # The layout changed: recalculate absolute input against its new full canvas.
    # Keep appears both in prose and on the button; only the lower button counts.
    size,words=capture(guest,'monitors-05-keep-question')
    question=gui.matches(words,'Keep this display setting')
    keep=[p for p in gui.matches(words,'Keep') if len(question)==1 and p['y']>question[0]['y']+20]
    if len(keep)!=1:raise RuntimeError('Actual Keep button not unique below the display-change question')
    tap(guest,keep[0]['x'],keep[0]['y'],input_canvas(changed,size))
    deadline=time.monotonic()+5
    while True:
        final=recollect()
        if settled(final,False):break
        if time.monotonic()>=deadline:raise RuntimeError('Keep did not leave the actual two-output acknowledged layout settled')
        time.sleep(.1)
    capture(guest,'monitors-06-second-kept')
    return final,{'enabled_connector':off[0]['name'],'label':off[0]['label'],
                  'route':'Actual Monitors checkbox -> actual Keep button through QMP pointer input',
                  'pending_generation':changed['display_generation'],'kept_generation':final['display_generation'],
                  'scope':'Live VM activation/Keep and actual backend/native agreement; mirroring, relogin persistence and installed login NOT TESTED',
                  'visual_review':'REQUIRED'}
