#!/usr/bin/env python3
"""Synthetic metadata and mocked backend IPC; no real socket, VM or display."""
import importlib.util,json,os
from pathlib import Path
import stat,struct,unittest
from types import SimpleNamespace
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('display_reader',Path(__file__).with_name('read-displays.py'))
reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)


class DisplayObservationChecks(unittest.TestCase):
    def record(self):
        geometry=[{'x':0,'y':0,'width':1280,'height':800},
                  {'x':1280,'y':0,'width':1280,'height':800}]
        return {'backend_outputs':[{'name':name,'active':True,'rect':dict(rect),'scale':1.0,'transform':'normal'}
                                   for name,rect in zip(('Virtual-1','Virtual-2'),geometry)],
                'topology':{'mode':'extend','generation':2,'primary':'Virtual-1',
                            'outputs':[{'name':name,**rect,'scale':1.0,'transform':'normal'}
                                       for name,rect in zip(('Virtual-1','Virtual-2'),geometry)]},
                'primary':'Virtual-1','display_generation':2,'display_applied_generation':2}

    def test_matching_names_and_generation_do_not_hide_wrong_geometry(self):
        for changed in ('x','y','width','height','scale','transform'):
            value=self.record();native=value['topology']['outputs'][1]
            if changed=='scale':native[changed]=1.5
            elif changed=='transform':native[changed]='90'
            else:native[changed]+=20
            with self.subTest(changed=changed):
                self.assertFalse(reader.assess(value,2)['all_requested_outputs_active_and_applied'])

    def test_invalid_or_missing_geometry_and_primary_refuse(self):
        for changed in ('missing-rect','boolean-width','zero-width','missing-scale','nan-scale',
                        'unknown-transform','missing-primary','unknown-primary','primary-disagreement',
                        'overlap','oversized-canvas'):
            value=self.record();backend=value['backend_outputs'][1];native=value['topology']['outputs'][1]
            if changed=='missing-rect':del backend['rect']
            elif changed=='boolean-width':backend['rect']['width']=native['width']=True
            elif changed=='zero-width':backend['rect']['width']=native['width']=0
            elif changed=='missing-scale':del native['scale']
            elif changed=='nan-scale':backend['scale']=native['scale']=float('nan')
            elif changed=='unknown-transform':backend['transform']=native['transform']='diagonal'
            elif changed=='missing-primary':del value['topology']['primary']
            elif changed=='unknown-primary':value['topology']['primary']=value['primary']='Other'
            elif changed=='primary-disagreement':value['primary']='Virtual-2'
            elif changed=='overlap':backend['rect']['x']=native['x']=100
            else:backend['rect']['x']=native['x']=16384
            with self.subTest(changed=changed):
                self.assertFalse(reader.assess(value,2)['all_requested_outputs_active_and_applied'])

    def test_matching_fractional_scale_rotated_and_negative_origin_are_valid(self):
        value=self.record()
        for backend,native in zip(value['backend_outputs'],value['topology']['outputs']):
            backend['scale']=native['scale']=1.5
            backend['transform']=native['transform']='90'
            backend['rect']['x']-=1280;native['x']-=1280
        assessment=reader.assess(value,2)
        self.assertTrue(assessment['all_requested_outputs_active_and_applied'])

    def test_output_count_failure_does_not_claim_a_geometry_mismatch(self):
        value=self.record()
        value['backend_outputs'].pop();value['topology']['outputs'].pop()
        assessment=reader.assess(value,2)
        self.assertFalse(assessment['all_requested_outputs_active_and_applied'])
        self.assertTrue(assessment['backend_and_desktop_geometry_match'])
        self.assertTrue(assessment['primary_output_matches'])

    def test_generation_acknowledgments_are_positive_integers_not_coercible_values(self):
        for field in ('display_generation','display_applied_generation','topology'):
            for bad in (True,2.0,0,None):
                value=self.record()
                if field=='topology':value['topology']['generation']=bad
                else:value[field]=bad
                with self.subTest(field=field,bad=bad):
                    self.assertFalse(reader.assess(value,2)['all_requested_outputs_active_and_applied'])

    def test_backend_capacity_alone_does_not_establish_a_native_two_output_desktop(self):
        value=self.record();self.assertTrue(reader.assess(value,2)['all_requested_outputs_active_and_applied'])
        for changed in ('one-native','different-name','unapplied','stale-topology','duplicate','mirror'):
            value=self.record()
            if changed=='one-native':value['topology']['outputs'].pop()
            elif changed=='different-name':value['topology']['outputs'][1]['name']='Other'
            elif changed=='unapplied':value['display_applied_generation']=1
            elif changed=='stale-topology':value['topology']['generation']=1
            elif changed=='duplicate':value['backend_outputs'][1]['name']='Virtual-1'
            else:value['topology']['mode']='mirror'
            with self.subTest(changed=changed):self.assertFalse(reader.assess(value,2)['all_requested_outputs_active_and_applied'])

    def test_inactive_and_non_desktop_connectors_do_not_count(self):
        value=self.record();value['backend_outputs'][1]['active']=False
        self.assertEqual(reader.assess(value,2)['backend_active_count'],1)
        self.assertFalse(reader.assess(value,2)['all_requested_outputs_active_and_applied'])
        value=self.record();value['backend_outputs'][1]['non_desktop']=True
        self.assertEqual(reader.assess(value,2)['backend_active_count'],1)
        with self.assertRaises(ValueError):reader.assess(value,True)

    def test_host_refuses_before_virtualization_or_user_data_queries(self):
        with patch.object(reader.os,'geteuid',return_value=1000), \
                patch.object(reader.socket,'gethostname',return_value='ordinary-host'), \
                patch.object(reader.subprocess,'run') as command:
            with self.assertRaises(SystemExit):reader.require_live()
            command.assert_not_called()

    def query(self,pid=42,kind=3,size=None,body=None):
        body=json.dumps([{'name':'Virtual-1','active':True}]).encode() if body is None else body
        data=bytearray(b'i3-ipc'+struct.pack('<II',len(body) if size is None else size,kind)+body)
        peer=Mock();peer.getsockopt.return_value=struct.pack('3i',pid,os.getuid(),1000)
        def recv(count):
            part=bytes(data[:min(count,3)]);del data[:len(part)];return part
        peer.recv.side_effect=recv
        context=Mock();context.__enter__=Mock(return_value=peer);context.__exit__=Mock(return_value=False)
        endpoint=Mock();endpoint.lstat.return_value=SimpleNamespace(st_mode=stat.S_IFSOCK,st_uid=os.getuid())
        with patch.object(reader.socket,'socket',return_value=context):
            result=reader.backend_outputs(endpoint,42)
        return result,peer

    def test_fragmented_authenticated_reply_uses_only_read_only_get_outputs(self):
        value,peer=self.query()
        self.assertEqual(value[0]['name'],'Virtual-1')
        peer.sendall.assert_called_once_with(b'i3-ipc'+struct.pack('<II',0,3))

    def test_wrong_peer_type_size_and_truncated_payload_refuse(self):
        for changes in ({'pid':43},{'kind':1},{'size':1048577},{'body':b'{'}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.query(**changes)
        peer=Mock();peer.recv.side_effect=[b'i3',b'']
        with self.assertRaisesRegex(ValueError,'closed'):reader.read_exact(peer,14)


if __name__=='__main__':unittest.main()
