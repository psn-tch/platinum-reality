#!/usr/bin/env python3
"""Pure metadata/guard fixtures only. Never GPG, signing, pacman or a VM."""
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('producer',Path(__file__).with_name('produce-test-repo.py'))
producer=importlib.util.module_from_spec(spec);spec.loader.exec_module(producer)
PROOF='usr/share/doc/platinum-reality/r4-vm-test.txt'
META=b'pkgname = platinum-reality\npkgver = 0.1.0-2\narch = x86_64\n'


class RepoChecks(unittest.TestCase):
    def package(self,root,entries):
        output=root/'platinum-reality-0.1.0-2-x86_64.pkg.tar.zst'
        # Uncompressed tar fixture under the real suffix; no package build.
        with tarfile.open(output,'w') as archive:
            for name,data,kind in entries:
                entry=tarfile.TarInfo(name)
                if kind=='link':
                    entry.type=tarfile.SYMTYPE;entry.linkname=data;archive.addfile(entry)
                else:
                    entry.size=len(data);archive.addfile(entry,io.BytesIO(data))
        return output

    def test_host_and_root_refuse_before_any_file_or_gpg_activity(self):
        for uid,host in [(1000,'ordinary-host'),(0,'pr-iso-builder')]:
            with self.subTest(uid=uid),patch.object(producer.os,'geteuid',return_value=uid), \
                    patch.object(producer.socket,'gethostname',return_value=host), \
                    patch.object(producer.subprocess,'run') as command:
                with self.assertRaises(producer.Refused):producer.produce(SimpleNamespace())
                command.assert_not_called()

    def test_builder_role_still_requires_qemu(self):
        with patch.object(producer.os,'geteuid',return_value=1000), \
                patch.object(producer.socket,'gethostname',return_value='pr-iso-builder'), \
                patch.object(producer.subprocess,'run',return_value=SimpleNamespace(returncode=1,stdout='none\n')):
            with self.assertRaises(producer.Refused):producer.require_builder()

    def test_container_on_top_of_qemu_refuses_before_inputs_or_key_operations(self):
        with patch.object(producer.os,'geteuid',return_value=1000), \
                patch.object(producer.socket,'gethostname',return_value='pr-iso-builder'), \
                patch.object(producer.subprocess,'run',side_effect=[
                    SimpleNamespace(returncode=0,stdout='kvm\n'),
                    SimpleNamespace(returncode=0,stdout='systemd-nspawn\n')]) as commands, \
                patch.object(producer,'checked_package') as files:
            with self.assertRaisesRegex(producer.Refused,'Containers'):producer.produce(SimpleNamespace())
            files.assert_not_called()
            self.assertEqual([call.args[0] for call in commands.call_args_list],[
                ['/usr/bin/systemd-detect-virt','--vm'],['/usr/bin/systemd-detect-virt','--container']])
            self.assertTrue(all(call.kwargs['timeout']==5 for call in commands.call_args_list))

    def test_indeterminate_container_probe_refuses_before_inputs_or_keys(self):
        for status in (2,-15):
            with self.subTest(status=status),patch.object(producer.os,'geteuid',return_value=1000), \
                    patch.object(producer.socket,'gethostname',return_value='pr-iso-builder'), \
                    patch.object(producer.subprocess,'run',side_effect=[
                        SimpleNamespace(returncode=0,stdout='kvm\n'),
                        SimpleNamespace(returncode=status,stdout='')]), \
                    patch.object(producer,'checked_package') as files:
                with self.assertRaises(producer.Refused):producer.produce(SimpleNamespace())
                files.assert_not_called()

    def test_plain_owned_vm_role_passes_both_environment_checks(self):
        with patch.object(producer.os,'geteuid',return_value=1000), \
                patch.object(producer.socket,'gethostname',return_value='pr-iso-builder'), \
                patch.object(producer.subprocess,'run',side_effect=[
                    SimpleNamespace(returncode=0,stdout='qemu\n'),
                    SimpleNamespace(returncode=1,stdout='none\n')]):
            producer.require_builder()

    def test_proof_path_is_only_relative_installed_payload(self):
        self.assertEqual(producer.proof_name(PROOF),PROOF)
        for bad in ('/etc/shadow','etc/secret','usr/../outside','usr/a/./b','usr/a\nb','usr/../../outside'):
            with self.subTest(bad=bad),self.assertRaises(producer.Refused):producer.proof_name(bad)

    def test_regular_metadata_and_changed_payload_are_read_without_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            package=self.package(root,[('.PKGINFO',META,'file'),(PROOF,b'VM TEST v2','file')])
            info=producer.package_info(package,PROOF)
            self.assertEqual(info['pkgver'],'0.1.0-2');self.assertTrue(info['proof_sha256'])
            self.assertFalse((root/'usr').exists())
            package=self.package(root,[('.PKGINFO',META,'file')])
            self.assertIsNone(producer.package_info(package,PROOF)['proof_sha256'])

    def test_ambiguous_identity_duplicate_member_and_link_proof_refuse(self):
        variants=[ [('.PKGINFO',META+b'pkgver = 9\n','file')],
                   [('.PKGINFO',META,'file'),('.PKGINFO',META,'file')],
                   [('.PKGINFO',META.replace(b'platinum-reality',b'other-package'),'file')],
                   [('.PKGINFO',META,'file'),(PROOF,'/etc/shadow','link')],
                   [('.PKGINFO',b'x'*65537,'file')] ]
        for entries in variants:
            with self.subTest(entries=str(entries)[:80]),tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(producer.Refused):producer.package_info(self.package(Path(folder),entries),PROOF)

    def test_input_paths_and_build_receipt_are_bounded_and_matched(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);package=self.package(root,[('.PKGINFO',META,'file')])
            receipt=root/'build.json';receipt.write_text(json.dumps({'package_sha256':producer.digest(package)}))
            with patch.object(producer,'GUEST_HOME',root):
                self.assertEqual(producer.checked_package(package,receipt,PROOF)[0],package)
                receipt.write_text(json.dumps({'package_sha256':'0'*64}))
                with self.assertRaisesRegex(producer.Refused,'receipt'):producer.checked_package(package,receipt,PROOF)
                alias=root/'alias';alias.symlink_to(package)
                with self.assertRaises(producer.Refused):producer.checked_path(alias)
            with self.assertRaises(producer.Refused):producer.checked_path(package)

    def test_no_keys_or_output_for_metadata_only_or_downgrade_candidates(self):
        args=SimpleNamespace(proof_file=PROOF,baseline=Path('baseline'),baseline_receipt=Path('old.json'),
                             candidate=Path('candidate'),candidate_receipt=Path('new.json'))
        old={'proof_sha256':'old','pkgver':'0.1.0-2'}
        for proof in (None,'old'):
            with self.subTest(proof=proof),patch.object(producer,'require_builder'), \
                    patch.object(producer,'checked_package',side_effect=[(Path('base'),old),(Path('new'),{'proof_sha256':proof})]), \
                    patch.object(producer.subprocess,'run') as commands,patch.object(producer.subprocess,'check_output') as version:
                with self.assertRaisesRegex(producer.Refused,'proof file must change'):producer.produce(args)
                commands.assert_not_called();version.assert_not_called()
        with patch.object(producer,'require_builder'), \
                patch.object(producer,'checked_package',side_effect=[(Path('base'),old),(Path('new'),{'proof_sha256':'changed','pkgver':'0.1.0-1'})]), \
                patch.object(producer.subprocess,'check_output',return_value='-1'),patch.object(producer.subprocess,'run') as commands:
            with self.assertRaisesRegex(producer.Refused,'newer than baseline'):producer.produce(args)
            commands.assert_not_called()

    def test_fingerprint_is_full_primary_and_unambiguous(self):
        fingerprint='A'*40
        self.assertEqual(producer.signing_fingerprint('sec:::::::::\nfpr:::::::::'+fingerprint+':\n'),fingerprint)
        for value in ('A'*16,'A'*41,'a'*40):
            with self.subTest(value=value),self.assertRaises(producer.Refused):
                producer.signing_fingerprint('sec:::::::::\nfpr:::::::::'+value+':\n')
        with self.assertRaises(producer.Refused):producer.signing_fingerprint('')
        with self.assertRaises(producer.Refused):
            producer.signing_fingerprint(('sec:::::::::\nfpr:::::::::'+fingerprint+':\n')*2)

    def test_public_inventory_does_not_collect_sibling_private_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);public=root/'public';public.mkdir();private=root/'private';private.mkdir()
            (private/'synthetic-data').write_text('fixture only, no key material')
            (public/'test.db.tar.gz').write_text('public fixture');(public/'test.db').symlink_to('test.db.tar.gz')
            inventory=producer.public_inventory(public)
            self.assertEqual(set(inventory),{'test.db','test.db.tar.gz'})
            (public/'escape').symlink_to('../private/synthetic-data')
            with self.assertRaises(producer.Refused):producer.public_inventory(public)

    def test_consumer_policy_requires_trusted_package_database_and_local_file_signatures(self):
        for name in ('baseline','candidate'):
            config=producer.consumer_config(name)
            self.assertIn('SigLevel = Required TrustedOnly',config)
            self.assertIn('LocalFileSigLevel = Required TrustedOnly',config)
            self.assertIn('RemoteFileSigLevel = Required TrustedOnly',config)
            self.assertNotIn('Optional',config);self.assertNotIn('TrustAll',config)
            self.assertIn('/snapshots/'+name,config)
            self.assertNotIn('[core]',config)
        with self.assertRaises(producer.Refused):producer.consumer_config('../../etc')


if __name__=='__main__':unittest.main()
