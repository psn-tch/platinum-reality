#!/usr/bin/env python3
"""Synthetic display plans and mocked QMP; no QEMU or runtime display changes."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import display_profiles as displays
import owned


class DisplayChecks(unittest.TestCase):
    def test_default_preserves_established_command(self):
        item=displays.profile('virtio-vga',1,'/owned/run')
        self.assertEqual(item['arguments'],['-device','virtio-vga','-display','none'])
        self.assertEqual(item['captures'],[{}])
        self.assertFalse(item['gl'])

    def test_virtio_two_heads_are_addressable_on_one_device(self):
        item=displays.profile('virtio-vga',2,'/owned/run')
        device=json.loads(item['arguments'][1])
        self.assertEqual(device,{'driver':'virtio-vga','id':'pr-display0','max_outputs':2,
                                 'outputs':[{'name':'PR VM 1','xres':1280,'yres':800},
                                            {'name':'PR VM 2','xres':1280,'yres':800}]})
        self.assertEqual(item['captures'],[{'device':'pr-display0','head':0},{'device':'pr-display0','head':1}])
        self.assertEqual(item['requested_output_capacity'],2)
        self.assertIn('independent evidence',item['scope'])

    def test_standard_vga_two_devices_are_not_mislabeled_as_one_multihead_card(self):
        item=displays.profile('std',2,'/owned/run')
        self.assertIn('VGA,id=pr-display0',item['arguments'])
        self.assertIn('secondary-vga,id=pr-display1',item['arguments'])
        self.assertIn('two devices',item['topology'])
        self.assertEqual(item['captures'][1],{'device':'pr-display1','head':0})
        self.assertNotIn('max_outputs',str(item['arguments']))

    def test_qxl_spice_is_private_unix_only_without_an_agent(self):
        for count in (1,2):
            item=displays.profile('qxl',count,'/owned/run')
            self.assertIn('qxl-vga,id=pr-display0,max_outputs='+str(count),item['arguments'])
            self.assertEqual(item['arguments'][-2:],['-spice','unix=on,addr=/owned/run/spice.sock,disable-ticketing=on'])
            self.assertNotIn('port=',str(item['arguments']))
            self.assertNotIn('vdagent',str(item['arguments']))

    def test_unreviewed_gpu_output_and_host_gl_choices_refuse(self):
        for gpu,count in [('virtio-vga-gl',1),('std',True),('std',0),('std',3),('qxl','2')]:
            with self.subTest(gpu=gpu,count=count):
                with self.assertRaises(ValueError):displays.profile(gpu,count,'/owned/run')

    def test_path_option_injection_refuses(self):
        for path in ('relative','/owned/run,port=1234','/owned/\nrun'):
            with self.assertRaises(ValueError):displays.profile('qxl',1,path)

    def test_invalid_fixture_refuses_before_socket_files_or_processes(self):
        guest=owned.Guest('/owned/run')
        with patch.object(owned.socket,'socket') as socket, patch.object(owned.subprocess,'Popen') as process:
            with self.assertRaises(ValueError):guest.start(gpu='virtio-vga-gl')
            socket.assert_not_called();process.assert_not_called()

    def test_capture_retains_partial_failure_without_inventing_active_outputs(self):
        with tempfile.TemporaryDirectory() as folder:
            guest=owned.Guest(folder)
            guest.display_profile=displays.profile('virtio-vga',2,guest.path)
            def qmp(verb,args):
                self.assertEqual(verb,'screendump')
                if args['head']==1:raise RuntimeError('head not available')
                Path(args['filename']).write_bytes(b'P6\n1 1\n255\n\0\0\0')
            guest.qmp=Mock(side_effect=qmp)
            result=guest.capture_displays('desktop-head')
            self.assertTrue(result[0]['captured']);self.assertFalse(result[1]['captured'])
            self.assertIn('head not available',result[1]['error'])
            self.assertNotIn('active',result[0])
            with self.assertRaises(ValueError):guest.capture_displays('../outside')
            self.assertEqual(guest.qmp.call_count,2)


if __name__=='__main__':unittest.main()
