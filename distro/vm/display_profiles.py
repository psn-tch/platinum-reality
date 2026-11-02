"""Explicit non-GL QEMU display fixtures; no host renderer or device access.

The requested capacity is not proof of connected/active guest outputs.
"""
from pathlib import Path
import json

GPUS = ('virtio-vga', 'std', 'qxl')


def profile(gpu, outputs, directory):
    if gpu not in GPUS or type(outputs) is not int or outputs not in (1, 2):
        raise ValueError('Display fixture requires a known non-GL GPU and one or two outputs')
    path = Path(directory)
    if not path.is_absolute() or any(c in str(path) for c in ',\n\r'):
        raise ValueError('Display fixture requires an absolute QEMU-safe owned directory')
    # Preserve the established one-output command, including its implicit ID.
    if gpu == 'virtio-vga' and outputs == 1:
        arguments = ['-device', 'virtio-vga', '-display', 'none']
        captures = [{}]
        topology = 'one virtio-vga device; default head'
    elif gpu == 'std':
        arguments = ['-device', 'VGA,id=pr-display0']
        if outputs == 2:
            arguments += ['-device', 'secondary-vga,id=pr-display1']
        arguments += ['-display', 'none']
        captures = [{'device': 'pr-display'+str(n), 'head': 0} for n in range(outputs)]
        topology = 'one VGA device' if outputs == 1 else 'VGA plus secondary-vga; two devices, not one multi-head GPU'
    elif gpu == 'virtio-vga':
        # max_outputs only reserves capacity. With display=none no UI client
        # connects the other head, so explicitly seed each virtual monitor.
        device = {'driver':'virtio-vga','id':'pr-display0','max_outputs':outputs,
                  'outputs':[{'name':'PR VM '+str(n+1),'xres':1280,'yres':800} for n in range(outputs)]}
        arguments = ['-device',json.dumps(device,separators=(',',':')),'-display','none']
        captures = [{'device':'pr-display0','head':n} for n in range(outputs)]
        topology = 'one virtio-vga device; explicit 1280x800 connected monitor per head'
    else:
        device = 'qxl-vga'
        arguments = ['-device', device+',id=pr-display0,max_outputs='+str(outputs), '-display', 'none']
        if gpu == 'qxl':
            # Private directory, Unix socket only. No TCP SPICE endpoint,
            # virtio agent, clipboard bridge, file transfer or host display.
            arguments += ['-spice', 'unix=on,addr='+str(path/'spice.sock')+',disable-ticketing=on']
        captures = [{'device': 'pr-display0', 'head': n} for n in range(outputs)]
        topology = 'one '+device+' device; advertised maximum '+str(outputs)+' heads'
    return {'gpu': gpu, 'requested_output_capacity': outputs, 'gl': False,
            'arguments': arguments, 'captures': captures, 'topology': topology,
            'scope': 'Requested virtual hardware only; guest detection, active outputs and Monitors UI require independent evidence'}
