#!/usr/bin/env python3
"""Exact pixel comparison in private headless Chromium against an external reference.

The reference image is private Apple artwork and must never be committed. This
check does not install the CSS or read/change any existing browser profile.
Run under the shared heavy-work lease. Evidence is retained, including failures.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent
REFERENCE_RGB_SHA256 = '14d9e5b41928e977bd1f2f6009b141e724dad0b8bbe46b841ae15e395435eb1c'
# Reference geometry is a 154px track with a 114px thumb. At a 184px viewport,
# content height 248 gives a 114.258px proportional thumb: Blink rounds it to
# 114 at 1x but 114.5 at 2x. Height 249 gives 113.799px, rounded to 114 at both
# scales. Keep the reference pixels/zero-mismatch requirement unchanged: this
# fixture compares the same thumb geometry, not two different size roundings.
CONTENT_LENGTH = 249


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--chromium', default='/usr/bin/chromium')
    args = parser.parse_args()
    with Image.open(args.reference) as source:
        reference=source.convert('RGB').crop((402,67,418,251))
    if hashlib.sha256(reference.tobytes()).hexdigest()!=REFERENCE_RGB_SHA256:
        raise ValueError('Not the reviewed 16x184 Mac OS 9.2.2 reference pixels')
    args.evidence.mkdir(mode=0o700, parents=True, exist_ok=False)
    css=(ROOT/'src/appearance/chromium-scrollbars.css').read_text()
    report={'ok':False, 'reference_rgb_sha256':REFERENCE_RGB_SHA256,
            'css_sha256':hashlib.sha256(css.encode()).hexdigest(), 'runs':[]}
    try:
        for orientation in ('vertical','horizontal'):
            for scale in (1,2):
                run=args.evidence/(orientation+'-'+str(scale)); run.mkdir()
                html=run/'fixture.html'
                style=('width:216px;height:184px;overflow-y:scroll;overflow-x:hidden' if orientation=='vertical'
                       else 'width:184px;height:216px;overflow-x:scroll;overflow-y:hidden')
                content=(f'width:200px;height:{CONTENT_LENGTH}px' if orientation=='vertical'
                         else f'width:{CONTENT_LENGTH}px;height:200px')
                html.write_text('<!doctype html><style>body{margin:0;background:#345}#fixture{'+style+
                    '}#content{'+content+';background:white}</style><style>'+css+
                    '</style><div id="fixture"><div id="content"></div></div>')
                environment={k:v for k,v in os.environ.items() if not k.startswith(('QT_','QSG_','GTK_','GDK_'))
                             and k not in ('DISPLAY','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS','SWAYSOCK')}
                home=run/'home'; home.mkdir()
                environment.update(HOME=str(home.resolve()),XDG_CONFIG_HOME=str((home/'config').resolve()),
                    XDG_DATA_HOME=str((home/'data').resolve()),XDG_CACHE_HOME=str((home/'cache').resolve()))
                command=[args.chromium,'--headless','--disable-gpu','--disable-background-networking',
                    '--disable-crash-reporter','--disable-breakpad',
                    '--no-first-run','--no-default-browser-check','--disable-extensions','--disable-sync',
                    '--host-resolver-rules=MAP * ~NOTFOUND','--window-size=640,480',
                    '--force-device-scale-factor='+str(scale),'--user-data-dir='+str((run/'profile').resolve()),
                    '--screenshot='+str((run/'actual.png').resolve()),html.resolve().as_uri()]
                with (run/'chromium.log').open('x') as log:
                    result=subprocess.run(command,env=environment,stdout=log,stderr=subprocess.STDOUT,timeout=30)
                if result.returncode:
                    report['runs'].append({'orientation':orientation,'scale':scale,'exit':result.returncode,'ok':False})
                    return 1
                box=(200,0,216,184) if orientation=='vertical' else (0,200,184,216)
                with Image.open(run/'actual.png') as source:
                    actual=source.convert('RGB').crop(tuple(value*scale for value in box))
                expected=reference if orientation=='vertical' else reference.transpose(Image.Transpose.TRANSPOSE)
                expected=expected.resize((expected.width*scale,expected.height*scale),Image.Resampling.NEAREST)
                difference=ImageChops.difference(actual,expected)
                mismatches=sum(a!=b for a,b in zip(actual.getdata(),expected.getdata()))
                actual.save(run/'bar.png');difference.save(run/'difference.png')
                report['runs'].append({'orientation':orientation,'scale':scale,'ok':mismatches==0,
                    'mismatched_pixels':mismatches,'pixels':actual.width*actual.height})
        report['ok']=all(run['ok'] for run in report['runs']) and len(report['runs'])==4
        return 0 if report['ok'] else 1
    finally:
        (args.evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report))


if __name__=='__main__':
    raise SystemExit(main())
