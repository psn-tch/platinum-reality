"""Explicit update37 installer GUI adapter; no legacy-label guessing.

The source pin and the one-output geometry are mandatory. Widget rendering
checks are preliminary; an actual image run and frame review remain required.
"""
import importlib.util
import json
from pathlib import Path
import time

spec=importlib.util.spec_from_file_location('legacy_gui',Path(__file__).with_name('gui-drive.py'))
gui=importlib.util.module_from_spec(spec);spec.loader.exec_module(gui)
NATIVE='6f37afdede5cf161d1015452efdf26492e0b6531'
FULL=(70,60,1050,650)
# Measured on the actual update37 image (install-20261107T045614Z, gui-01-welcome): the default
# button's label spans x 569-620, y 462-471 inside a ring at x 553-637, y 455-480. Read the
# label interior only: the earlier (561,454,68,16) clipped its last row and took in the ring.
FOOTER=(563,459,64,16)
# The Your Account group's label column on the actual image (install-20261107T052603Z): whole-window
# OCR read the group titles but none of these right-aligned labels; this block reads all four.
LABELS=(140,198,92,86)


def choose(guest,label,available,value,visible,key):
    """The legacy keyboard path, then, if its row read misses a short value (actual image
    install-20261107T052907Z: "uk" shown but not read), the pop-up's own text alone."""
    try:
        gui.choose(guest,label,available,value,visible,key)
    except RuntimeError as error:
        if 'did not visibly select' not in str(error):raise
        _,points=gui.wait(guest,'gui-choice-'+label.replace(' ','-'),label)
        if len(points)!=1:raise
        gui.wait(guest,'gui-selected-value-'+label.replace(' ','-'),visible,timeout=5,
                 region=(262,round(points[0]['y'])-8,230,16),psm='7')


def account_label(guest,stem,label):
    words,found=gui.wait(guest,stem,label,region=LABELS,psm='6')
    if len(found)!=1:raise RuntimeError('Actual account label ambiguous; no credential typed')
    return words,found[0]


def pair(words,reject,accept):
    """Require a unique lowest aligned button pair, below its question text."""
    no=gui.matches(words,reject);yes=gui.matches(words,accept)
    candidates=[(a,b) for a in no for b in yes if a['x']<b['x'] and abs(a['y']-b['y'])<=3]
    if not candidates:raise RuntimeError('Question has no aligned actual button labels; no consent click sent')
    bottom=max(max(a['y'],b['y']) for a,b in candidates)
    candidates=[(a,b) for a,b in candidates if abs(max(a['y'],b['y'])-bottom)<=3]
    if len(candidates)!=1:raise RuntimeError('Ambiguous actual question button row; no consent click sent')
    return candidates[0]


def button(guest,stem,label):
    if label not in ('Continue','Select','Start'):raise ValueError('Unreviewed update37 footer action')
    _,points=gui.wait(guest,stem,label,region=FOOTER,psm='7')
    if len(points)!=1:raise RuntimeError('Actual update37 footer label ambiguous; no click sent')
    gui.click(guest,points[0]['x'],points[0]['y'])


def account_value(guest,stem,label,value):
    top=round(label['y'])-9
    if not 175<=top<=330:raise ValueError('Unreviewed account row; no credential typed')
    gui.wait(guest,stem,value,region=(230,top,390,18),psm='7',timeout=10)


def agree(guest):
    words,_=gui.wait(guest,'gui-06-license-dialog','Disagree',region=FULL)
    text=gui.normalized(' '.join(w['text'] for w in words))
    if not gui.matches(words,'Software License Agreement') or 'tocontinuewithinstallingthesoftwareyoumustagree' not in text:
        raise RuntimeError('Actual update37 license question unrecognized; no Agree click sent')
    try:
        no,yes=pair(words,'Disagree','Agree')
    except RuntimeError:
        # The default button's ring hides "Agree" from whole-frame OCR (actual update37 image,
        # install-20261107T052341Z: Disagree read, Agree not). Read that button's label interior
        # alone: the button right of the lowest Disagree, on its row (measured: label centres
        # 90 px apart, label 16 px tall).
        rows=gui.matches(words,'Disagree')
        if not rows:raise
        no=max(rows,key=lambda w:w['y'])
        _,found=gui.wait(guest,'gui-06-agree-button','Agree',timeout=10,
                         region=(round(no['x'])+60,round(no['y'])-8,60,16),psm='7')
        if len(found)!=1 or abs(found[0]['y']-no['y'])>3 or found[0]['x']<=no['x']:
            raise RuntimeError('Actual update37 Agree button not found right of Disagree; no consent click sent')
        yes=found[0]
    if yes['y']<190:raise RuntimeError('Unreviewed license button placement; no Agree click sent')
    gui.click(guest,yes['x'],yes['y'])


def erase_buttons(guest,words):
    """The ringed default Cancel hides both buttons from whole-frame OCR (actual image
    install-20261107T053255Z). Read the button row alone, just below the question's last line and
    right-aligned under it (the dialog grows with listed partitions, so never a fixed spot)."""
    # The title, and the question's own opening ("Erase “Disk (/dev/vda)…"), both read as Erase Disk.
    titles=gui.matches(words,'Erase Disk')
    if not titles:raise RuntimeError('Erase question title not read; no erase click sent')
    title=min(titles,key=lambda t:t['y'])
    lines=[w for w in words if gui.normalized(w['text']) and w['y']>title['y']+8 and w['y']<430 and w['x']>=230]
    if not lines:raise RuntimeError('Erase question text not read; no erase click sent')
    last=max(w['y']+w['h'] for w in lines);right=max(w['x']+w['w'] for w in lines)
    return gui.capture(guest,'gui-11-erase-buttons',region=(right-210,last+20,240,30),psm='11')


def occupied_path_pixels(guest,receipt):
    """OCR reads this font's "/dev/vda1" as "/dev/vdal" (or "/devjvdal"), even zoomed. Sammy viewed
    the actual frame install-20261107T064451Z (native 3053688, engine 68bf00cf…): the question lists
    /dev/vda1 1.1 GB vfat "VM_DATA", Cancel ringed. Accept the path only as those exact pixels, at
    that geometry, for that exact native installer and engine; never substitute characters."""
    # a632c6e changes only Desktop.qml and a test after 3053688; src/installer is byte-identical.
    # 6f37afd adds USB-only/refusal wording on the Destination page; the Erase Disk question is the same
    # code, and the hash below still requires the identical pixels.
    if (receipt.get('native_source_commit') not in ('3053688000063625d2ad6b5ac489e0e7ad74e0a4','a632c6ed330e8c6b0bfed3cd14c3ef71c872079a','6f37afdede5cf161d1015452efdf26492e0b6531') or
            # ac041cb8…: engine 1066b21 (USB installs on real computers); same question, same pixels
            # (install-20261107T104309Z viewed, crop sha identical).
            receipt.get('engine_sha256') not in ('68bf00cf1ce3a83736ad06f4963a831419dc533a78970222f2d23ca255fbb8fd',
                                                 'ac041cb8de1a1d29cc4983246a1ace9d9f5a368954b6761b9a3e9fb1cd03da1d')):
        return False
    import hashlib
    from PIL import Image
    with Image.open(guest.path/'gui-11-erase-confirmation.ppm') as frame:
        crop=frame.convert('RGB').crop((200,244,264,260))
        return hashlib.sha256(crop.tobytes()).hexdigest()=='c6e77e696dc2e31d57254d35a59d419f3de5c4f40a22800a098a7a9710a1b54b'


def recognize_erase(guest,receipt):
    words,_=gui.wait(guest,'gui-11-erase-confirmation','Everything on this disk',region=FULL)  # matches() spans at most 6 words; the whole sentence is checked below
    text=gui.normalized(' '.join(w['text'] for w in words))
    if not gui.matches(words,'Erase Disk') or not gui.matches(words,'/dev/vda') or 'everythingonthisdiskwillbelost' not in text:
        raise RuntimeError('Actual update37 whole-disk warning unrecognized; no erase click sent')
    if receipt.get('occupied_gui_fixture'):
        # Do not carry the legacy 1/l pixel fallback into new modal geometry.
        literal=gui.occupied_confirmation_visible(words)
        pixels=not literal and occupied_path_pixels(guest,receipt)
        if not (literal or gui.occupied_confirmation_visible(words,path_pixels_verified=pixels)):
            raise RuntimeError('Occupied update37 path/label/type/removal warning not literally recognized; no erase click sent')
        receipt['gui_erase_confirmation']={'visible_partition':'/dev/vda1','label':'VM_DATA','fstype':'vfat',
            'warning':'every listed partition will be removed','path_recognition':'literal OCR' if literal else 'reviewed exact pixels (64x16 at 200,244)',
            'screenshot':'gui-11-erase-confirmation.ppm','visual_review':'REQUIRED'}
    try:
        no,yes=pair(words,'Cancel','Erase and Install')
    except RuntimeError:
        no,yes=pair(erase_buttons(guest,words),'Cancel','Erase and Install')
    if no['y']<190:raise RuntimeError('Unreviewed erase button placement; no erase click sent')
    return yes


def confirm_erase(guest,receipt,remote,key):
    point=recognize_erase(guest,receipt)
    if receipt.get('erase_return_only_requested') and not receipt.get('verify_erase_return_requested'):
        raise ValueError('Return-only requires verified cancellation')
    if receipt.get('verify_erase_return_requested'):
        before=(guest.path/'hash-before.jsonl').read_text().split()[0]
        if len(before)!=64 or any(c not in '0123456789abcdef' for c in before):raise ValueError('Missing full-disk baseline')
        receipt['erase_return_cancel']={'passed':False,'only_dialog_action':'Return'}
        key(guest,'ret')
        after=remote('sudo -n sha256sum /dev/vda','hash-after-erase-return').split()[0]
        if after!=before:raise RuntimeError('Return changed the occupied target; no explicit erase click sent')
        words,_=gui.wait(guest,'gui-11-return-summary','Click Start to install',region=FULL)
        if gui.matches(words,'Erase Disk'):raise RuntimeError('Return did not dismiss actual erase question')
        receipt['erase_return_cancel'].update(passed=True,disk_changed=False,full_target_hash=before,screenshot='gui-11-return-summary.ppm')
        if receipt.get('erase_return_only_requested'):
            receipt['gui_cancel_only']='PASS: actual Return cancelled; full occupied target unchanged; no explicit erase requested'
            return False
        button(guest,'gui-11-reopen-erase','Start');point=recognize_erase(guest,receipt)
    gui.click(guest,point['x'],point['y']);return True


def drive(guest,password,receipt,remote,key,type_text):
    if receipt.get('native_source_commit')!=NATIVE:
        raise ValueError('Update37 GUI adapter requires its exact qualified native source; no input sent')
    receipt['gui_layout']='update37-540x372-explicit'
    _,points=gui.wait(guest,'gui-00-desktop','Install',region=gui.SHORTCUT_REGION)
    if len(points)!=1:raise RuntimeError('Actual installer shortcut ambiguous; no launch sent')
    p=points[0];gui.click(guest,p['x'],p['y']);gui.click(guest,p['x'],p['y'])
    gui.wait(guest,'gui-01-title','Install Platinum Reality Software',region=FULL)
    button(guest,'gui-01-welcome','Continue')
    gui.wait(guest,'gui-02-destination','Destination Disk',region=FULL)
    # The whole "Destination Disk: <name> (<path>) — <size>" line (window at x 110, y 93 on the actual
    # image): from 1a55251 the pop-up names the path before the size and is never cut off there.
    gui.wait(guest,'gui-02-destination-value','/dev/vda',region=(120,196,500,24),psm='7')
    # There is one usable virtual target; the actual pop-up must already show
    # its path. Clicking its text as though it were the old list opens a menu.
    button(guest,'gui-03-destination-selected','Select')
    gui.wait(guest,'gui-04-information','Important Information');button(guest,'gui-04-information','Continue')
    gui.wait(guest,'gui-05-license','Software License Agreement');button(guest,'gui-05-license','Continue');agree(guest)
    words,p=account_label(guest,'gui-07-account','Your name')
    if not gui.matches(words,'Password') or not gui.matches(words,'Verify'):
        raise RuntimeError('Actual account form unrecognized; no credential typed')
    gui.click(guest,p['left']+200,p['y']);key(guest,'ctrl+a');type_text(guest,'Test User')
    account_value(guest,'gui-07-full-name',p,'Test User')
    _,p=account_label(guest,'gui-07-short-name-label','Short name')
    gui.click(guest,p['left']+200,p['y']);key(guest,'ctrl+a');type_text(guest,'tester')
    account_value(guest,'gui-07-short-name',p,'tester')
    key(guest,'tab');type_text(guest,password);key(guest,'tab');type_text(guest,password);key(guest,'tab')
    key(guest,'ctrl+a');type_text(guest,'pr-installed')
    choices=json.loads(remote('/usr/bin/platinum-reality-install choices --json','gui-choices'))
    choose(guest,'Language',choices['locales'],'en_GB.UTF-8','English (United Kingdom)',key)
    choose(guest,'Time zone',choices['timezones'],'Europe/London','Europe/London',key)
    choose(guest,'Keyboard',choices['keymaps'],'uk','uk',key)
    receipt['gui_selected_choices']={'locale':'en_GB.UTF-8','timezone':'Europe/London','keymap':'uk'}
    button(guest,'gui-08-account-complete','Continue')
    words,_=gui.wait(guest,'gui-09-summary','tester',region=FULL)
    if not gui.matches(words,'Click Start to install') or 'diskwillbeencrypted' not in gui.normalized(' '.join(w['text'] for w in words)):
        raise RuntimeError('Actual Start summary/account/encryption not recognized')
    button(guest,'gui-10-summary-install','Start')
    if not confirm_erase(guest,receipt,remote,key):return
    deadline=time.monotonic()+1800
    while True:
        words=gui.capture(guest,'gui-12-result',region=FULL)
        text=gui.normalized(' '.join(w['text'] for w in words))
        # The ringed default Restart is often not read whole-frame (as Agree and Cancel weren't); the
        # alert's own sentences say it is the success alert, and nothing is clicked here.
        # OCR interleaves the alert's wrapped lines (install-20261107T054146Z read "click Restart to
        # installer. use your new software"), so pair its first sentence with the page behind it.
        if 'theinstallationprocesshasfinished' in text and 'platinumrealitywasinstalledon' in text:break
        if any(s in text for s in ('couldnotbecompleted','wasnoterased','erasinghadstarted')):raise RuntimeError('Actual GUI reported installation failure')
        if time.monotonic()>=deadline:raise TimeoutError('Actual update37 finished alert was not observed')
        time.sleep(2)
    receipt['gui_install']='PASS: real update37 GUI, Select/pop-up/Start, explicit erase, immutable engine, completion alert'
    receipt['installed']=True
    # Keep final alert open: controller must detach BOTH CDs before target boot.
