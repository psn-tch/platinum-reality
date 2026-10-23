"""Owned backend snapshots and acknowledgements; no physical display operations."""
import importlib.util
import json
import math
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('standalone_session', Path(__file__).resolve().parents[1] / 'scripts/standalone-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)
GOLDEN = json.loads(Path(__file__).with_name('fixtures').joinpath('display-topology.json').read_text())


def inventory():
    return [dict(name=o['name'], active=True, rect={k:o[k] for k in ('x','y','width','height')},
                 scale=o['scale'], transform=o['transform'], current_mode=dict(width=o['width'],height=o['height']))
            for o in GOLDEN['outputs']]


class DisplaySnapshotTests(unittest.TestCase):
    def test_shared_consumer_fixture_and_atomic_private_publication(self):
        description = session.describe_displays(inventory(), 'HEADLESS-1')
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)/'display-topology.json'
            session.publish_displays(path, description, GOLDEN['session'], 1)
            self.assertEqual(json.loads(path.read_text()), GOLDEN)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            old_descriptor = os.open(path, os.O_RDONLY)
            try:
                old = os.read(old_descriptor, 65536)
                session.publish_displays(path, description, GOLDEN['session'], 2)
                self.assertEqual(json.loads(old)['generation'], 1)
                self.assertEqual(json.loads(path.read_text())['generation'], 2)
                self.assertEqual(os.fstat(old_descriptor).st_nlink, 0)
            finally:
                os.close(old_descriptor)
            self.assertEqual([p.name for p in path.parent.iterdir()], ['display-topology.json'])

    def test_disabled_and_non_desktop_outputs_are_not_published(self):
        outputs = inventory()
        outputs[0]['active'] = False
        result = session.describe_displays(outputs, 'HEADLESS-1')
        self.assertEqual(result['primary'], 'HEADLESS-2')
        self.assertEqual(len(result['outputs']), 1)
        outputs[1]['non_desktop'] = True
        with self.assertRaises(ValueError): session.describe_displays(outputs, 'HEADLESS-1')

    def test_invalid_or_overlapping_geometry_is_never_published(self):
        for key,value in [('x',-20000),('width',True),('width','960'),('width',0)]:
            outputs = inventory();outputs[1]['rect'][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):
                session.describe_displays(outputs,'HEADLESS-1')
        for key,value in [('scale',math.nan),('transform','45'),('transform',{}),('name','DP-3; exit')]:
            outputs=inventory();outputs[1][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):
                session.describe_displays(outputs,'HEADLESS-1')
        outputs=inventory();outputs[1]['rect']['x']=500
        with self.assertRaises(ValueError): session.describe_displays(outputs,'HEADLESS-1')
        outputs=inventory();outputs[1]['name']=outputs[0]['name']
        with self.assertRaises(ValueError): session.describe_displays(outputs,'HEADLESS-1')

    def test_origin_and_rotation_are_preserved_for_the_consumer(self):
        outputs=inventory();outputs[0]['rect']['x']=-1120;outputs[1]['rect']['x']=0
        description=session.describe_displays(outputs,'HEADLESS-2')
        self.assertEqual(description['outputs'][0]['x'],-1120)
        self.assertEqual(description['outputs'][1]['transform'],'90')

    def test_only_current_private_acknowledgements_are_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'display-topology.json.applied'
            self.assertFalse(session.display_acknowledged(path,GOLDEN['session'],2))
            good=dict(version=1,session=GOLDEN['session'],generation=2)
            path.write_text(json.dumps(good));path.chmod(0o600)
            self.assertTrue(session.display_acknowledged(path,GOLDEN['session'],2))
            self.assertFalse(session.display_acknowledged(path,GOLDEN['session'],3))
            for change in (dict(version=True),dict(generation=True),dict(session='other')):
                path.write_text(json.dumps({**good,**change}))
                self.assertFalse(session.display_acknowledged(path,GOLDEN['session'],2))
            path.write_text(json.dumps(good));path.chmod(0o644)
            self.assertFalse(session.display_acknowledged(path,GOLDEN['session'],2))
            path.chmod(0o600)
            link=path.with_name('link');link.symlink_to(path)
            self.assertFalse(session.display_acknowledged(link,GOLDEN['session'],2))


if __name__ == '__main__':
    unittest.main()
