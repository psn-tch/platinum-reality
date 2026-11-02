"""Owned live-VM interruption transport and evidence, never a physical installer.

This module does not run from the command line. The explicit controller flag
owns the VM, source pins, blank-disk baseline and subsequent reinstall/login.
"""
import json
from pathlib import Path
import shutil
import stat
import subprocess
import time


class AsyncInstall:
    """One SSH Popen; secret input goes only to its anonymous stdin pipe."""
    def __init__(self,guest,command,stem,secret_input):
        self.output=guest.path/(stem+'.jsonl')
        self.out=self.output.open('wb');self.err=(guest.path/(stem+'.stderr')).open('wb')
        self.process=None
        try:
            self.process=subprocess.Popen(guest.ssh_argv(command),stdin=subprocess.PIPE,
                                          stdout=self.out,stderr=self.err)
            try:
                self.process.stdin.write(secret_input)
                self.process.stdin.flush()
            finally:
                self.process.stdin.close()
        except BaseException:
            self.close();raise

    def close(self):
        # This is only the Popen created above, never a PID read from a file.
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill();self.process.wait(timeout=5)
        self.out.close();self.err.close()

    def __enter__(self):return self
    def __exit__(self,*exc):self.close()


def progress(path):
    if path.is_symlink() or path.stat().st_size>1024**2:
        raise ValueError('Expected bounded installer progress file')
    # An incomplete final write is not a complete progress event.
    events=[json.loads(line) for line in path.read_bytes().split(b'\n')[:-1] if line.strip()]
    if any(not isinstance(event,dict) for event in events):raise ValueError('Invalid installer progress')
    return events


def wait_for_writes(command,probe,timeout=180):
    deadline=time.monotonic()+timeout;attempt=0
    while True:
        events=progress(command.output)
        steps=[e['step'] for e in events if 'step' in e]
        if command.process.poll() is not None or any('result' in e for e in events) or (steps and steps[-1] in ('configure','bootloader','finish')):
            raise RuntimeError('Missed package-write interruption window; no power cut performed')
        if steps and steps[-1]=='packages':
            attempt+=1;observed=probe(attempt)
            after=progress(command.output);last=[e['step'] for e in after if 'step' in e]
            if (observed.get('package_writes_observed') is True and observed.get('mounted_installer_targets')==1
                    and observed.get('pacman_pids') and observed.get('written_package_samples')
                    and command.process.poll() is None and last and last[-1]=='packages'
                    and not any('result' in e for e in after)):
                return {'observed':observed,'probe_attempt':attempt,'progress_events':after,
                        'observed_monotonic':time.monotonic(),
                        'scope':'Fresh blank target; active package writer plus real nonempty package metadata, shortly before owned VM termination'}
        if time.monotonic()>=deadline:raise TimeoutError('No verified active package-write stage; no power cut performed')
        time.sleep(.1)


def target_identity(guest):
    target=guest.install_target
    if (not isinstance(target,Path) or target.is_symlink() or not target.is_relative_to(guest.path)
            or target.resolve(strict=True)!=target or not stat.S_ISREG(target.stat().st_mode)):
        raise ValueError('Only the current owned regular installation target is accepted')
    info=target.stat()
    return {'path':str(target),'device':info.st_dev,'inode':info.st_ino}


def interrupt(guest,executable,secret_input,remote,receipt,digest):
    """Observe writes, kill only this VM, preserve its disk; does not restart it."""
    if not receipt.get('read_only_guards') or receipt.get('installed') is not False:
        raise ValueError('Fresh invocation-owned target and completed read-only baseline required')
    identity=target_identity(guest)
    record={'ok':False,'target_identity':identity,'scope':'Abrupt VM process termination, followed by explicit same-disk destructive reinstall; not physical power-loss or repair'}
    receipt['interrupted_retry']=record
    command=executable+'install --config /run/pr-test/config.json --confirm-erase /dev/vda --progress-fd 1'
    with AsyncInstall(guest,command,'interrupted-install',secret_input) as running:
        stage=wait_for_writes(running,lambda attempt:json.loads(remote(
            'sudo -n python3 -I /run/pr-test/package-write-probe.py','interrupt-write-probe-'+str(attempt))))
        record['package_write_stage']=stage
        (guest.path/'interrupt-stage.json').write_text(json.dumps(stage,indent=2)+'\n')
        guest.qmp('screendump',{'filename':str(guest.path/'interrupt-write-stage.ppm')})
        # Capturing evidence can outlast the remaining package transaction.
        # Never cut after it advanced merely because the previous probe passed.
        current=progress(running.output)
        steps=[event['step'] for event in current if 'step' in event]
        if running.process.poll() is not None or not steps or steps[-1]!='packages' or any('result' in event for event in current):
            raise RuntimeError('Package stage advanced during evidence capture; no power cut performed')
        record['progress_immediately_before_cut']=current
        record['injection']=guest.power_cut_install_fixture()
        record['observed_to_stopped_seconds']=round(time.monotonic()-stage['observed_monotonic'],3)
        try:record['interrupted_ssh_exit']=running.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            raise RuntimeError('Interrupted VM transport did not finish promptly; retry not started')
    if target_identity(guest)!=identity:raise RuntimeError('Interrupted destination identity changed')
    target=guest.install_target
    if shutil.disk_usage(guest.path).free<max(20*1024**3,target.stat().st_size+1024**3):
        raise RuntimeError('Preserving host free space: insufficient space for interrupted evidence copy')
    copy=guest.path/'target-interrupted-evidence.qcow2'
    if copy.exists() or copy.is_symlink():raise RuntimeError('Interrupted evidence copy already exists')
    subprocess.run(['cp','--reflink=auto','--sparse=always','--',str(target),str(copy)],check=True,timeout=180)
    original_hash=digest(target);copy_hash=digest(copy)
    if original_hash!=copy_hash:raise RuntimeError('Interrupted evidence copy differs')
    record['evidence_copy']={'file':copy.name,'container_sha256':copy_hash,
                             'scope':'qcow2 container bytes; NOT a full virtual-sector hash'}
    # QEMU is stopped. Preserve first-phase logs before its next start can truncate them.
    # Live media generates a new SSH host key. Archive its old per-phase trust
    # file as well, so even an ephemeral forwarding-port reuse cannot reject
    # the new owned boot as a changed old server. Never modify the host user's trust.
    archive=guest.path/'interrupted-first-attempt';archive.mkdir(mode=0o700)
    for name in ('serial.log','qemu.log','command.json','qemu.pid','known_hosts','live-session-logs.tar','live-session-logs'):
        source=guest.path/name
        if source.exists():source.rename(archive/name)
    return identity
