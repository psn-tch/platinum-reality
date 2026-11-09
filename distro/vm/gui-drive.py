#!/usr/bin/env python3
"""Drive the actual unprivileged installer in an owned live VM, using QMP/OCR.

Only the VM controller supplies the synthetic password, in memory. Nothing
here bypasses the GUI's confirmation, substitutes its engine or edits the image.
"""
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import time

INSTALLER_REGION = (110, 92, 606, 468)
SHORTCUT_REGION = (1120, 135, 160, 90)
LICENSE_DIALOG_REGION = (160, 266, 506, 117)
AGREE_TEXT_REGION = (584, 347, 56, 17)
FOOTER_PRIMARY_TEXT_REGION = (605, 519, 79, 17)
OCCUPIED_ERASE_REGION = (160, 197, 506, 258)
OCCUPIED_ERASE_TEXT_REGION = (539, 416, 106, 18)
OCCUPIED_PATH_RECT = (230, 285, 63, 19)
OCCUPIED_PATH_RGB_SHA256 = 'd9f5c25aeb735536b9dc1256ad6f84ffd4ff979da68486f1440bd23a586b689e'


def normalized(value):
    return ''.join(re.findall(r'[a-z0-9/]+', value.casefold()))


def matches(words, phrase):
    wanted = normalized(phrase)
    found = []
    for start in range(len(words)):
        if not normalized(words[start]['text']):
            continue  # An icon/punctuation prefix must not duplicate its following real label.
        text = ''
        for end in range(start, min(len(words), start+6)):
            # Never combine words from different OCR lines into a fake button.
            if words[end]['line'] != words[start]['line']:
                break
            text += normalized(words[end]['text'])
            if text == wanted:
                segment = words[start:end+1]
                left = min(w['x'] for w in segment); top = min(w['y'] for w in segment)
                right = max(w['x']+w['w'] for w in segment); bottom = max(w['y']+w['h'] for w in segment)
                found.append({'x': (left+right)//2, 'y': (top+bottom)//2, 'left': left, 'top': top})
                break
            if not wanted.startswith(text):
                break
    return found


def capture(guest, stem, region=None, psm='11'):
    image = guest.path/(stem+'.ppm')
    guest.qmp('screendump', {'filename': str(image)})
    with image.open('rb') as stream:
        if stream.readline().strip() != b'P6' or stream.readline().strip() != b'1280 800':
            raise ValueError('GUI driver only supports the reviewed 1280x800 VM fixture')
    # Source-pinned one-output fixture: the installer is the second window,
    # at its reviewed initial geometry. Crop it so sparse OCR sees the actual
    # footer buttons as well as mentions of Continue in the page body.
    region = region or INSTALLER_REGION
    source = image; scale = 1; offset_x = offset_y = 0
    if region is not None:
        from PIL import Image
        offset_x, offset_y, width, height = region
        if min(offset_x, offset_y) < 0 or width <= 0 or height <= 0 or offset_x+width > 1280 or offset_y+height > 800:
            raise ValueError('OCR region outside the reviewed display')
        scale = 3 if region == SHORTCUT_REGION else 2
        source = guest.path/(stem+'-ocr.png')
        with Image.open(image) as pixels:
            pixels.crop((offset_x, offset_y, offset_x+width, offset_y+height)).resize(
                (width*scale, height*scale), Image.Resampling.LANCZOS).save(source)
        (guest.path/(stem+'-ocr.json')).write_text(json.dumps({'region': region, 'scale': scale})+'\n')
    tsv = subprocess.check_output(['tesseract', str(source), 'stdout', '--psm', '6' if region == SHORTCUT_REGION else psm, 'tsv'],
                                  stderr=subprocess.DEVNULL, text=True)
    (guest.path/(stem+'.tsv')).write_text(tsv)
    words = []
    for row in csv.DictReader(io.StringIO(tsv), delimiter='\t', quoting=csv.QUOTE_NONE):
        if not row['text'].strip() or row['level'] != '5':
            continue
        words.append({'text': row['text'], 'x': offset_x+round(int(row['left'])/scale),
                      'y': offset_y+round(int(row['top'])/scale),
                      'w': max(1, round(int(row['width'])/scale)), 'h': max(1, round(int(row['height'])/scale)),
                      'line': tuple(row[k] for k in ('page_num','block_num','par_num','line_num'))})
    return words


def wait(guest, stem, phrase, timeout=30, region=None, psm='11'):
    deadline = time.monotonic()+timeout
    while True:
        words = capture(guest, stem, region, psm)
        found = matches(words, phrase)
        if found:
            return words, found
        if time.monotonic() >= deadline:
            raise TimeoutError('Installer GUI did not show '+phrase+'; see '+stem+'.ppm')
        time.sleep(.5)


def click(guest, x, y):
    if not (0 <= x < 1280 and 0 <= y < 800):
        raise ValueError('Click outside owned screenshot')
    guest.qmp('input-send-event', {'events': [
        {'type': 'abs', 'data': {'axis': 'x', 'value': round(x*32767/1279)}},
        {'type': 'abs', 'data': {'axis': 'y', 'value': round(y*32767/799)}},
        {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
    guest.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
    time.sleep(.15)


def button(guest, stem, text):
    if text not in ('Continue','Install'):
        raise ValueError('Only reviewed primary footer buttons are supported')
    # A focus ring can make full-window OCR omit the actual button and leave
    # only prose such as "Click Install to begin". Read the reviewed footer's
    # text interior, never choose body words by their relative y position.
    _, found = wait(guest, stem, text,region=FOOTER_PRIMARY_TEXT_REGION,psm='7')
    if len(found)!=1:raise RuntimeError('Ambiguous actual footer button; no click sent')
    point=found[0]
    click(guest, point['x'], point['y'])


def agree_to_license(guest):
    # Reviewed update26/native6505ef8 1280x800 fixture. Full-window sparse OCR
    # misses Agree inside the default-button focus ring, leaving body mentions
    # of the word as false button candidates. Recognize the actual modal first,
    # then read its isolated button-text interior (never click body prose).
    words,_=wait(guest,'gui-06-license-dialog','Software License Agreement',region=LICENSE_DIALOG_REGION)
    if not matches(words,'To continue installing') or not matches(words,'Disagree'):
        raise RuntimeError('Unrecognized license agreement question; no consent click sent')
    _,points=wait(guest,'gui-06-agree-button','Agree',region=AGREE_TEXT_REGION,psm='7')
    if len(points)!=1:raise RuntimeError('Ambiguous actual Agree button; no consent click sent')
    point=points[0];click(guest,point['x'],point['y'])


def wait_account_value(guest,stem,label_point,value):
    # Row spacing changed with update26 focus rings. Anchor the text interior
    # to the actual recognized label row, not the older fixed y=213 crop.
    top=round(label_point['y'])-8
    if not 175<=top<=330:raise ValueError('Unreviewed account value row; no password typed')
    return wait(guest,stem,value,timeout=10,region=(246,top,440,17),psm='7')


def choose(guest, label, available, value, visible, key):
    # The GUI preserves choices --json order and caps each list at 2000.
    # Navigate only a value actually advertised by the immutable engine.
    if value not in available[:2000] or len(available) != len(set(available)):
        raise ValueError('Requested GUI choice is absent/ambiguous: '+value)
    _, points = wait(guest, 'gui-choice-'+label.replace(' ', '-'), label)
    if len(points) != 1:
        raise ValueError('GUI choice label is ambiguous: '+label)
    point = points[0]
    click(guest, point['left']+200, point['y'])
    # Select through the focused, CLOSED combo. Return on a closed combo can
    # activate the form's default Continue button (observed in143225Z), not
    # commit a popup. Escape closes any popup first; Tab leaves the selected
    # value without advancing the installer page.
    key(guest,'esc')
    capture(guest,'gui-focused-'+label.replace(' ','-'))
    key(guest, 'home')
    for _ in range(available.index(value)):
        key(guest, 'down')
    capture(guest,'gui-navigated-'+label.replace(' ','-'))
    key(guest,'tab')
    deadline=time.monotonic()+5
    while True:
        words = capture(guest, 'gui-selected-'+label.replace(' ', '-'))
        # Require the selected item's visible text on the actual label row.
        row = [word for word in words if abs(word['y']+word['h']/2-point['y']) <= 10]
        if matches(row,visible):return
        if time.monotonic()>=deadline:raise RuntimeError('GUI did not visibly select '+value)
        time.sleep(.25)


def occupied_confirmation_visible(words, path_pixels_verified=False):
    # Each fixture identity must actually appear. The warning may wrap lines;
    # join its reading-order words without weakening the required wording.
    text = normalized(' '.join(word['text'] for word in words))
    return ((bool(matches(words,'/dev/vda1')) or path_pixels_verified) and
            all(matches(words, value) for value in ('VM_DATA', 'vfat')) and
            normalized('every listed partition will be removed') in text)


def occupied_path_pixels(guest,receipt):
    # Reviewed144437Z, update28/045516Z and widget-fix/054056Z frames: OCR reads1 as l.
    # Sammy, 2026-11-01: update-29 canonical 3ae1f74 (src/, packaging/, CMakeLists byte-identical
    # to 996311d) frame install-20261101T071825Z viewed: Cancel ringed, vda1/vfat/VM_DATA and the
    # removal warning visible, path crop exactly d9f5c25… at the same geometry.
    # Do NOT substitute characters or guess. Require the exact visible path
    # pixels, native source, engine source and geometry of that reviewed frame.
    if (receipt.get('native_source_commit') not in (
            '6505ef833072e03083bae642565877050bdc294e',
            '9d36a317a9ccb0a6aa901a545e74d332b7f8a687',
            '996311d013129443657d1081142f747bc36a95c8',
            '3ae1f740859445c7d1c01fe1db37dab11f78c81f') or
            receipt.get('engine_sha256') not in (
                '02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2',
                # Sammy, 2026-11-01: 379440b (partition-table reread only); frame of
                # install-20261101T100639Z viewed, identical question, path crop d9f5c25….
                '36da10dfd00aca77f8700211be9c6c4a0de6115e45dcfd6f638375b8fbbd38f3')):
        return False
    from PIL import Image
    with Image.open(guest.path/'gui-11-erase-confirmation.ppm') as image:
        if image.size!=(1280,800):return False
        x,y,width,height=OCCUPIED_PATH_RECT
        pixels=image.convert('RGB').crop((x,y,x+width,y+height)).tobytes()
    return hashlib.sha256(pixels).hexdigest()==OCCUPIED_PATH_RGB_SHA256


def recognize_erase(guest,receipt):
    if receipt.get('occupied_gui_fixture'):
        words,_=wait(guest,'gui-11-erase-confirmation','Erase Disk',region=OCCUPIED_ERASE_REGION)
        pixels=False if matches(words,'/dev/vda1') else occupied_path_pixels(guest,receipt)
        if not occupied_confirmation_visible(words,pixels):
            raise RuntimeError('Actual erase question did not visibly identify the existing partition and removal warning; no erase click sent')
        receipt['gui_erase_confirmation'] = {
            'visible_partition': '/dev/vda1', 'label': 'VM_DATA', 'fstype': 'vfat',
            'warning': 'every listed partition will be removed',
            'screenshot': 'gui-11-erase-confirmation.ppm', 'visual_review': 'REQUIRED',
            'path_recognition':'exact source/geometry-pinned RGB pixels' if pixels else 'literal OCR',
            'path_pixel_sha256':OCCUPIED_PATH_RGB_SHA256 if pixels else None}
        _,found=wait(guest,'gui-11-erase-button','Erase and Install',region=OCCUPIED_ERASE_TEXT_REGION,psm='7')
    else:
        _,found=wait(guest,'gui-11-erase-confirmation','Erase and Install')
    point = max(found, key=lambda p: p['y'])
    return point


def verify_return_cancel(guest,receipt,remote,key):
    before=(guest.path/'hash-before.jsonl').read_text().split()[0]
    if not re.fullmatch(r'[0-9a-f]{64}',before):raise ValueError('Missing full-disk baseline; no Return sent')
    receipt['erase_return_cancel']={'passed':False,'only_dialog_action':'Return',
        'scope':'Invocation-owned synthetic disk only; actual GUI behavior, no consent bypass'}
    key(guest,'ret')
    capture(guest,'gui-11-return-observed')
    after=remote('sudo -n sha256sum /dev/vda','hash-after-erase-return').split()[0]
    if after!=before:
        receipt['erase_return_cancel']['disk_changed']=True
        capture(guest,'gui-11-return-changed-disk')
        raise RuntimeError('Return at the erase question changed the target disk; expected Cancel, no explicit erase click sent')
    deadline=time.monotonic()+5
    while True:
        words=capture(guest,'gui-11-return-summary')
        if matches(words,'Click Install to begin') and not matches(words,'Erase Disk'):break
        if time.monotonic()>=deadline:
            raise RuntimeError('Return did not visibly cancel the erase question; no cancellation pass')
        time.sleep(.25)
    receipt['erase_return_cancel'].update(passed=True,disk_changed=False,full_target_hash=before,
        screenshot='gui-11-return-summary.ppm')


def confirm_erase(guest, receipt, remote=None, key=None):
    if receipt.get('verify_erase_return_requested') and (not callable(remote) or not callable(key)):
        raise ValueError('Actual Return cancellation requires owned transport and key input')
    if receipt.get('erase_return_only_requested') and not receipt.get('verify_erase_return_requested'):
        raise ValueError('Return-only proof requires verified cancellation; no erase click sent')
    point=recognize_erase(guest,receipt)
    if receipt.get('verify_erase_return_requested'):
        verify_return_cancel(guest,receipt,remote,key)
        if receipt.get('erase_return_only_requested'):
            receipt['gui_cancel_only'] = 'PASS: actual Return cancelled; full occupied target unchanged; no explicit erase requested'
            return False
        button(guest,'gui-11-reopen-erase','Install')
        point=recognize_erase(guest,receipt)
    click(guest, point['x'], point['y'])
    return True


def drive(guest, password, receipt, remote, key, type_text):
    # Whole-screen sparse OCR ignores the small white desktop-label patches.
    # This is the reviewed shortcut cell on the one-output 1280x800 image;
    # still require its actual text, never click the empty cell blindly.
    words, points = wait(guest, 'gui-00-desktop', 'Install', region=SHORTCUT_REGION)
    points = [point for point in points if point['y'] > 50]
    if len(points) != 1:
        raise RuntimeError('Expected one visible live installer shortcut; no click sent')
    point = points[0]
    click(guest, point['x'], point['y']); click(guest, point['x'], point['y'])
    button(guest, 'gui-01-welcome', 'Continue')
    words, rows = wait(guest, 'gui-02-destination', '/dev/vda')
    point = rows[0]; click(guest, point['x'], point['y'])
    button(guest, 'gui-03-destination-selected', 'Continue')
    wait(guest, 'gui-04-information', 'Important Information')
    button(guest, 'gui-04-information', 'Continue')
    wait(guest, 'gui-05-license', 'Software License Agreement')
    button(guest, 'gui-05-license', 'Continue')
    agree_to_license(guest)
    words, labels = wait(guest, 'gui-07-account', 'Your name')
    if not matches(words, 'Password') or not matches(words, 'Verify'):
        raise RuntimeError('Unrecognized account form; no secret typed')
    point = labels[0]
    click(guest, point['left']+200, point['y'])
    key(guest, 'ctrl+a'); type_text(guest, 'Test User')
    wait_account_value(guest,'gui-07-full-name',point,'Test User')
    words, short = wait(guest, 'gui-07-short-name-label', 'Short name')
    point = short[0]; click(guest, point['left']+200, point['y'])
    key(guest, 'ctrl+a'); type_text(guest, 'tester')
    wait_account_value(guest,'gui-07-short-name',point,'tester')
    capture(guest, 'gui-07-account-names')
    key(guest, 'tab')
    type_text(guest, password); key(guest, 'tab'); type_text(guest, password); key(guest, 'tab')
    key(guest, 'ctrl+a'); type_text(guest, 'pr-installed')
    choices = json.loads(remote('/usr/bin/platinum-reality-install choices --json', 'gui-choices'))
    choose(guest, 'Language', choices['locales'], 'en_GB.UTF-8', 'English (United Kingdom)', key)
    choose(guest, 'Time zone', choices['timezones'], 'Europe/London', 'Europe/London', key)
    choose(guest, 'Keyboard', choices['keymaps'], 'uk', 'uk', key)
    receipt['gui_selected_choices'] = {'locale': 'en_GB.UTF-8', 'timezone': 'Europe/London', 'keymap': 'uk'}
    button(guest, 'gui-08-account-complete', 'Continue')
    words, _ = wait(guest, 'gui-09-summary', '/dev/vda')
    if not matches(words, 'tester'):
        raise RuntimeError('Installation summary did not name the synthetic test account')
    button(guest, 'gui-10-summary-install', 'Install')
    if not confirm_erase(guest, receipt, remote, key):
        return
    # The only engine is the real immutable image wrapper, reached by Qt's
    # QProcess, stdin secret pipe and stdout JSONL. No test-answer shortcuts.
    deadline = time.monotonic()+1800
    while True:
        words = capture(guest, 'gui-12-result')
        text = ' '.join(w['text'] for w in words).casefold()
        if 'was installed' in text and matches(words, 'Restart'):
            break
        if 'could not be completed' in text or 'was not erased' in text or 'erasing had started' in text:
            raise RuntimeError('Actual GUI installation failed; see gui-12-result.ppm')
        if time.monotonic() > deadline:
            raise TimeoutError('Actual GUI installation did not complete')
        time.sleep(2)
    receipt['gui_install'] = 'PASS: real GUI, confirmation, image engine, stdin credentials and completion screen'
    receipt['installed'] = True
    # Never press Restart here: the controller must detach BOTH CDs first.
