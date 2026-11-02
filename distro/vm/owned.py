#!/usr/bin/env python3
"""Private QEMU helpers: copied disks, localhost SSH, authenticated QMP, no shares.

Only callers holding the daily-use lease may start a guest. Privileged commands
are sent over SSH to that guest; this module never invokes host sudo.
"""
import fcntl
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shlex
import socket
import stat
import struct
import subprocess
import time

# The ISO builder also loads this file by absolute spec, without sys.path edits.
_display_spec = importlib.util.spec_from_file_location(
    'owned_display_profiles', Path(__file__).with_name('display_profiles.py'))
_displays = importlib.util.module_from_spec(_display_spec)
_display_spec.loader.exec_module(_displays)
display_profile = _displays.profile

ROOT = Path(__file__).resolve().parents[2]
# Integration clones can explicitly share one lease without embedding a user's
# home path in source. All collaborators must select the same existing path.
LEASE = Path(os.environ.get('PLATINUM_HEAVY_WORK_LEASE', ROOT/'build/daily-use-gate.lock')).expanduser().resolve()


def run(argv, **kwargs):
    return subprocess.run([str(x) for x in argv], check=True, **kwargs)


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class Lease:
    def __init__(self, wait=0):
        if not isinstance(wait, (int, float)) or not 0 <= wait <= 1800:
            raise ValueError('Lease wait must be between 0 and 1800 seconds')
        self.wait = wait

    def __enter__(self):
        LEASE.parent.mkdir(parents=True, exist_ok=True)
        self.file = LEASE.open('a')
        deadline = time.monotonic()+self.wait
        try:
            while True:
                try:
                    fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    return self
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise SystemExit('Heavy-work lease is busy; no VM started.')
                    time.sleep(min(.5, max(0, deadline-time.monotonic())))
        except BaseException:
            # Includes cancellation during the retry sleep, not just flock.
            self.file.close()
            raise

    def __exit__(self, *exc):
        self.file.close()


class Guest:
    def __init__(self, directory, username='tester'):
        self.path = Path(directory).resolve()
        self.process = None
        self.username = username

    def initialize_builder(self, base):
        if self.path.exists():
            if not (self.path / 'origin.json').is_file():
                raise RuntimeError('Existing VM directory has no origin receipt')
            return
        self.path.mkdir(mode=0o700, parents=True)
        os.chmod(self.path, 0o700)
        base = Path(base).resolve(strict=True)
        if not base.is_file() or base.suffix != '.qcow2':
            raise ValueError('Builder base must be a regular .qcow2 file, never a device')
        info = json.loads(subprocess.check_output(['qemu-img', 'info', '--output=json', str(base)]))
        if (info.get('format') != 'qcow2' or info.get('backing-filename') or
                info.get('format-specific', {}).get('data', {}).get('data-file')):
            raise ValueError('Base must be self-contained qcow2, with no external data or backing files')
        copied = self.path / 'base.qcow2'
        run(['cp', '--reflink=auto', '--sparse=always', base, copied])
        os.chmod(copied, 0o444)
        run(['qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', copied,
             self.path / 'builder.qcow2', '64G'])
        run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', self.path / 'key'])
        public = (self.path / 'key.pub').read_text().strip()
        cloud = {'hostname': 'pr-iso-builder', 'users': [{
            'name': 'tester', 'groups': ['wheel'], 'shell': '/bin/bash',
            'sudo': ['ALL=(ALL) NOPASSWD:ALL'], 'lock_passwd': True,
            'ssh_authorized_keys': [public]}], 'disable_root': True, 'ssh_pwauth': False,
            'growpart': {'mode': 'auto', 'devices': ['/']}, 'resize_rootfs': True,
            'runcmd': [['systemctl', 'enable', '--now', 'sshd']]}
        (self.path / 'user-data').write_text('#cloud-config\n' + json.dumps(cloud))
        (self.path / 'meta-data').write_text('instance-id: pr-builder-' + secrets.token_hex(8) + '\n')
        run(['genisoimage', '-quiet', '-output', self.path / 'seed.iso', '-volid', 'cidata',
             '-joliet', '-rock', self.path / 'user-data', self.path / 'meta-data'])
        (self.path / 'origin.json').write_text(json.dumps({
            'base_source': str(Path(base).resolve()), 'base_copy_sha256': sha256(copied),
            'scope': 'Copied cloud disk; unique SSH key; no host shares; guest-only sudo.'}, indent=2)+'\n')

    def start(self, *, iso=None, uefi=False, network=True, target=None, fixture=None, installed=False, restricted=False, test_units=None, serial=True, gpu='virtio-vga', outputs=1):
        if type(serial) is not bool:
            raise ValueError('Serial console selection must be explicit boolean')
        self.display_profile = display_profile(gpu, outputs, self.path)
        self.is_live_iso = bool(iso)
        self.install_target = None
        self.ssh_ready = False
        if self.path.joinpath('qmp.sock').exists():
            raise RuntimeError('QMP socket exists; inspect the previous owned guest first')
        # An ephemeral localhost port; only the unique guest key authenticates SSH.
        with socket.socket() as port:
            port.bind(('127.0.0.1', 0))
            self.port = port.getsockname()[1]
        firmware = []
        drives = []
        if uefi:
            code = Path('/usr/share/edk2/x64/OVMF_CODE.4m.fd')
            variables = self.path / 'OVMF_VARS.fd'
            if not variables.exists():
                run(['cp', '/usr/share/edk2/x64/OVMF_VARS.4m.fd', variables])
            firmware = ['-drive', f'if=pflash,format=raw,readonly=on,file={code}',
                        '-drive', f'if=pflash,format=raw,file={variables}']
        if iso:
            iso = Path(iso).resolve(strict=True)
            if not iso.is_file() or iso.suffix != '.iso':
                raise ValueError('Boot media must be a regular .iso file, never a device')
            drives = ['-drive', f'file={Path(iso).resolve(strict=True)},media=cdrom,readonly=on',
                      '-boot', 'd']
        elif not installed:
            # Explicit guest-only fstrim can release deleted build work from
            # this disposable overlay; the copied base and seed stay intact.
            drives = ['-drive', f'file={self.path}/builder.qcow2,if=virtio,format=qcow2,discard=unmap',
                      '-drive', f'file={self.path}/seed.iso,media=cdrom,readonly=on']
        if target:
            target = Path(target).resolve(strict=True)
            if not target.is_file() or target.suffix != '.qcow2' or not target.is_relative_to(self.path):
                raise ValueError('Writable installation target must be an owned regular qcow2 in this VM directory')
            info = json.loads(subprocess.check_output(['qemu-img', 'info', '--output=json', str(target)]))
            if info.get('format') != 'qcow2' or info.get('backing-filename') or info.get('format-specific', {}).get('data', {}).get('data-file'):
                raise ValueError('Install target must have no external backing or data files')
            drives += ['-drive', f'file={target},if=virtio,format=qcow2,discard=unmap']
            self.install_target = target
        if installed and (iso or fixture or not target):
            raise ValueError('Installed-disk proof must detach all ISO media and select an owned target')
        if fixture:
            fixture = Path(fixture).resolve(strict=True)
            if not fixture.is_file() or fixture.suffix != '.iso' or not fixture.is_relative_to(self.path):
                raise ValueError('Fixture must be an owned read-only ISO')
            drives += ['-drive', f'file={fixture},media=cdrom,readonly=on']
        credentials = []
        for name, contents in (test_units or {}).items():
            # Nonsecret test-unit definitions only. Never send passwords, private
            # keys or LUKS material through SMBIOS/argv or the command receipt.
            if not name.startswith(('systemd.extra-unit.', 'systemd.unit-dropin.')) or not isinstance(contents, str):
                raise ValueError('Only nonsecret systemd test-unit credentials are supported')
            encoded = base64.b64encode(contents.encode()).decode()
            credentials += ['-smbios', f'type=11,value=io.systemd.credential.binary:{name}={encoded}']
        command = ['qemu-system-x86_64', '-enable-kvm', '-machine', 'q35', '-cpu', 'host',
                   '-smp', '4', '-m', '8G', *firmware, *drives, *credentials,
                   '-nic', (f'user,{"restrict=on," if restricted else ""}model=virtio-net-pci,hostfwd=tcp:127.0.0.1:{self.port}-:22' if network else 'none'),
                   *self.display_profile['arguments'],
                   '-audiodev', 'none,id=disabled',
                   '-device', 'virtio-keyboard-pci', '-device', 'virtio-tablet-pci',
                   '-qmp', f'unix:{self.path}/qmp.sock,server=on,wait=off',
                   '-serial', (f'file:{self.path}/serial.log' if serial else 'none'), '-monitor', 'none', '-nodefaults']
        self.started = time.monotonic()
        self.process = subprocess.Popen(command, stdout=(self.path/'qemu.log').open('ab'), stderr=subprocess.STDOUT)
        self.pid = self.process.pid
        (self.path / 'command.json').write_text(json.dumps(command, indent=2)+'\n')
        (self.path / 'qemu.pid').write_text(str(self.pid)+'\n')
        until = time.monotonic() + 10
        while not (self.path/'qmp.sock').exists():
            if self.process.poll() is not None or time.monotonic() > until:
                raise RuntimeError('QEMU did not start; inspect qemu.log')
            time.sleep(.1)

    def qmp(self, command, arguments=None):
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(10)
            peer.connect(str(self.path/'qmp.sock'))
            pid, uid, _ = struct.unpack('3i', peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if pid != self.pid or uid != os.getuid():
                raise RuntimeError('QMP peer does not match our owned QEMU')
            with peer.makefile('rwb', buffering=0) as stream:
                json.loads(stream.readline())
                for request in [{'execute': 'qmp_capabilities'},
                                {'execute': command, 'arguments': arguments or {}}]:
                    stream.write(json.dumps(request).encode()+b'\n')
                    while True:
                        response = json.loads(stream.readline())
                        if 'error' in response:
                            raise RuntimeError(response['error'])
                        if 'return' in response:
                            break
                return response['return']

    def capture_displays(self, stem):
        if not stem or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in stem):
            raise ValueError('Display capture requires a plain artifact stem')
        captures = []
        for index, selection in enumerate(self.display_profile['captures']):
            image = self.path/(stem+'-'+str(index)+'.ppm')
            item = {'file': image.name, **selection, 'captured': False}
            try:
                self.qmp('screendump', {'filename': str(image), **selection})
                item['captured'] = image.is_file() and image.stat().st_size > 0
            except (RuntimeError, OSError) as error:
                item['error'] = str(error)
            captures.append(item)
        return captures  # A captured head can be black/inactive: visual review remains required.

    def power_cut_install_fixture(self):
        """Deliberately crash only our running live installer VM, never the host.

        Caller must first retain independent evidence of actual package writes.
        This method does not assert that stage and never deletes a target disk.
        """
        if (not self.process or self.process.poll() is not None or
                self.process.pid != self.pid or not self.is_live_iso or
                not self.install_target or not self.install_target.is_relative_to(self.path)):
            raise RuntimeError('Power-cut injection requires our active live VM and its owned installation target')
        endpoint = self.path/'qmp.sock'
        before = endpoint.lstat()
        if not stat.S_ISSOCK(before.st_mode) or before.st_uid != os.getuid():
            raise RuntimeError('Power-cut injection requires our private QMP socket')
        # qmp authenticates the kernel peer PID/UID against the Popen owner.
        if self.qmp('query-status').get('status') != 'running':
            raise RuntimeError('Power-cut injection requires an actually running owned VM')
        self.process.kill()
        status = self.process.wait(timeout=10)
        # SIGKILL leaves a filesystem socket. Never unlink a replacement or a
        # foreign file just because it has the old path. Retain on ambiguity.
        try:
            after = endpoint.lstat()
        except FileNotFoundError:
            after = None
        if after is not None:
            if (not stat.S_ISSOCK(after.st_mode) or after.st_uid != before.st_uid or
                    (after.st_dev,after.st_ino) != (before.st_dev,before.st_ino)):
                raise RuntimeError('QMP endpoint changed after power cut; retained for inspection')
            endpoint.unlink()
        if status != -9:
            raise RuntimeError('Owned VM did not exit by SIGKILL; no simulated power-loss pass')
        return {'injection':'SIGKILL of invocation-owned QEMU Popen', 'qemu_exit':status,
                'target_retained':True, 'package_write_stage':'Caller evidence required',
                'scope':'Abrupt VM process termination; not physical storage power-loss qualification'}

    def ssh_argv(self, command):
        # Shared with the invocation-owned asynchronous installer transport.
        # Commands contain no credentials; those use only its stdin pipe.
        return [str(x) for x in ['ssh', '-F', '/dev/null', '-i', self.path/'key', '-p', self.port,
                    '-o', 'IdentitiesOnly=yes', '-o', 'IdentityAgent=none', '-o', 'ForwardAgent=no',
                    '-o', 'ControlMaster=no', '-o', 'ControlPath=none', '-o', 'BatchMode=yes',
                    '-o', 'ConnectTimeout=4', '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=3',
                    '-o', 'StrictHostKeyChecking=accept-new',
                    '-o', f'UserKnownHostsFile={self.path}/known_hosts', self.username+'@127.0.0.1', command]]

    def ssh(self, command, **kwargs):
        return run(self.ssh_argv(command), **kwargs)

    def wait_ssh(self):
        until = time.monotonic()+180
        while time.monotonic() < until:
            try:
                self.ssh('true', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
                self.ssh_ready = True
                return
            except (subprocess.SubprocessError, OSError):
                if self.process.poll() is not None:
                    break
                time.sleep(2)
        raise TimeoutError('Owned guest SSH unavailable; inspect serial.log')

    def put(self, source, destination):
        if not str(destination).startswith('/home/tester/'):
            raise ValueError('Guest upload must be under /home/tester')
        with Path(source).open('rb') as stream:
            self.ssh('cat > ' + shlex.quote(str(destination)), stdin=stream)

    def stop(self):
        if self.process and self.process.poll() is None:
            if not self.is_live_iso and self.ssh_ready:
                try:
                    self.ssh('sudo systemctl poweroff', stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, timeout=12)
                except (OSError, subprocess.SubprocessError):
                    # The guest can close SSH before systemd has finished
                    # syncing/unmounting. A transport error is not permission
                    # to turn that ordinary shutdown into an immediate cut.
                    pass
                try:
                    self.process.wait(timeout=20)
                    return
                except subprocess.TimeoutExpired:
                    pass
            try:
                self.qmp('quit')
                self.process.wait(timeout=15)
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                # Popen still owns the process; never signal an arbitrary recorded PID.
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=10)
