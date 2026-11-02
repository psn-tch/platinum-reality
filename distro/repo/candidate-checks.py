#!/usr/bin/env python3
"""Synthetic uncompressed tar fixtures only; no package build/install or VM."""
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('candidate',Path(__file__).with_name('prepare-vm-candidate.py'))
candidate=importlib.util.module_from_spec(spec);spec.loader.exec_module(candidate)
META=b'pkgname = platinum-reality\npkgver = 0.1.0-1\narch = x86_64\npkgdesc = Fixture\nurl = https://example.invalid\nlicense = MIT\ndepend = qt6-base\n'


class CandidateChecks(unittest.TestCase):
    def package(self,root,name,entries):
        path=root/name
        with tarfile.open(path,'w') as tar:
            for name,data,kind,mode in [('.PKGINFO',META,'file',0o644),*entries]:
                item=tarfile.TarInfo(name);item.mode=mode
                if kind=='link':item.type=tarfile.SYMTYPE;item.linkname=data;tar.addfile(item)
                elif kind=='device':item.type=tarfile.CHRTYPE;tar.addfile(item)
                elif kind=='directory':item.type=tarfile.DIRTYPE;tar.addfile(item)
                else:item.size=len(data);tar.addfile(item,io.BytesIO(data))
        return path

    def test_host_or_root_refusal_precedes_package_reads_and_commands(self):
        for uid,hostname in [(1000,'ordinary-host'),(0,'pr-iso-builder')]:
            with self.subTest(uid=uid),patch.object(candidate.producer.os,'geteuid',return_value=uid), \
                    patch.object(candidate.producer.socket,'gethostname',return_value=hostname), \
                    patch.object(candidate.producer,'checked_package') as packages,patch.object(candidate.subprocess,'run') as command:
                with self.assertRaises(candidate.producer.Refused):candidate.prepare(SimpleNamespace())
                packages.assert_not_called();command.assert_not_called()

    def test_inventory_detects_program_byte_mode_and_directory_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            baseline=self.package(root,'old',[('usr/bin/fixture',b'program','file',0o755),('usr','', 'directory',0o755)])
            fields,before=candidate.inspect(baseline)
            self.assertEqual(fields['pkgver'],['0.1.0-1'])
            for defect in ('bytes','mode','directory'):
                entries=[('usr/bin/fixture',b'changed' if defect=='bytes' else b'program','file',0o644 if defect=='mode' else 0o755)]
                entries+=[('usr','','directory',0o700 if defect=='directory' else 0o755)]
                new=self.package(root,defect,entries)
                self.assertNotEqual(candidate.inspect(new)[1],before)

    def test_metadata_and_explicit_marker_are_the_only_excluded_payload_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);entries=[('usr/bin/fixture',b'program','file',0o755)]
            old=self.package(root,'old',entries)
            new=self.package(root,'new',entries+[(candidate.PROOF,candidate.MARKER,'file',0o644),('.BUILDINFO',b'fixture','file',0o644)])
            self.assertEqual(candidate.inspect(old)[1],candidate.inspect(new)[1])
            changed=self.package(root,'added',entries+[('usr/bin/other',b'new','file',0o755)])
            self.assertNotEqual(candidate.inspect(old)[1],candidate.inspect(changed)[1])

    def test_archive_escape_links_devices_duplicates_and_install_scripts_refuse(self):
        for entry in [('../outside',b'x','file',0o644),('/absolute',b'x','file',0o644),
                      ('usr/link','/etc/secret','link',0o777),('usr/device',b'','device',0o600),
                      ('.INSTALL',b'not executable in this test','file',0o644),('.PKGINFO',META,'file',0o644)]:
            with self.subTest(entry=entry[0]),tempfile.TemporaryDirectory() as folder:
                path=self.package(Path(folder),'fixture',[entry])
                with self.assertRaises(candidate.producer.Refused):candidate.inspect(path)

    def fields(self):
        return {'pkgname':['platinum-reality'],'arch':['x86_64'],'pkgdesc':['Fixture'],'url':['https://example.invalid'],
                'license':['MIT'],'depend':['qt6-base'],'replaces':['platinum-dreams']}

    def test_generated_recipe_has_local_pinned_source_and_no_native_build_or_install_step(self):
        text=candidate.recipe(self.fields(),'0.1.0.r4vmtest','a'*64)
        self.assertIn('noextract=(baseline.pkg.tar.zst)',text);self.assertIn('sha256sums=('+'a'*64+')',text)
        self.assertIn('options=(!debug !strip)',text);self.assertIn('NOT A RELEASE',text)
        self.assertNotIn('build()',text);self.assertNotIn('sudo',text);self.assertNotIn('pacman ',text)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'PKGBUILD';path.write_text(text)
            subprocess.run(['/bin/bash','-n',str(path)],check=True)

    def test_metadata_is_quoted_as_data_and_version_injection_is_refused(self):
        fields=self.fields();fields['pkgdesc']=["fixture' $(printf dangerous) `printf dangerous`"]
        text=candidate.recipe(fields,'0.1.0.r4vmtest','a'*64)
        self.assertIn("pkgdesc='fixture'",text)
        for version in ('bad;command','$(command)','1-1','../version'):
            with self.subTest(version=version),self.assertRaises(candidate.producer.Refused):candidate.recipe(fields,version,'a'*64)
        fields['pkgname']=['other']
        with self.assertRaises(candidate.producer.Refused):candidate.recipe(fields,'1','a'*64)

    def test_ambiguous_or_unsupported_identity_metadata_refuses(self):
        fields=self.fields();fields['arch'].append('aarch64')
        with self.assertRaises(candidate.producer.Refused):candidate.recipe(fields,'1','a'*64)
        fields=self.fields();fields['install']=['unreviewed']
        with self.assertRaises(candidate.producer.Refused):candidate.recipe(fields,'1','a'*64)


if __name__=='__main__':unittest.main()
