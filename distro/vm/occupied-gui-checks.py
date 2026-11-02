#!/usr/bin/env python3
"""Synthetic OCR/plan and mocked QMP only; no VM, GUI runtime or disk tools."""
import importlib.util
import contextlib
import io
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch


def load(filename):
    spec=importlib.util.spec_from_file_location(filename.replace('-','_'),Path(__file__).with_name(filename+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


gui=load('gui-drive');proof=load('install-proof')


def words(text):
    return [{'text':word,'x':i*40,'y':100,'w':30,'h':16,'line':('1','1','1','1')}
            for i,word in enumerate(text.split())]


class OccupiedGuiChecks(unittest.TestCase):
    def test_nontext_icon_does_not_duplicate_the_following_label_match(self):
        actual=words('= Control Panels')
        found=gui.matches(actual,'Control Panels')
        self.assertEqual(len(found),1)
        self.assertEqual(found[0]['left'],40)

    def test_path_pixel_fallback_is_source_geometry_and_exact_bytes_bound(self):
        from PIL import Image
        receipt={'native_source_commit':'6505ef833072e03083bae642565877050bdc294e',
                 'engine_sha256':'02dd983a2fa0ba3f47ec22f803c83619b0f367a95d93c1456b5d2f8b4979e1b2'}
        with tempfile.TemporaryDirectory() as folder:
            guest=Mock();guest.path=Path(folder);image=Image.new('RGB',(1280,800),'gray')
            file=guest.path/'gui-11-erase-confirmation.ppm';image.save(file)
            sha=hashlib.sha256(image.crop((230,285,293,304)).tobytes()).hexdigest()
            with patch.object(gui,'OCCUPIED_PATH_RGB_SHA256',sha):
                self.assertTrue(gui.occupied_path_pixels(guest,receipt))
                self.assertTrue(gui.occupied_path_pixels(guest,receipt|{
                    'native_source_commit':'9d36a317a9ccb0a6aa901a545e74d332b7f8a687'}))
                self.assertTrue(gui.occupied_path_pixels(guest,receipt|{
                    'native_source_commit':'996311d013129443657d1081142f747bc36a95c8'}))
                self.assertFalse(gui.occupied_path_pixels(guest,receipt|{'native_source_commit':'0'*40}))
                self.assertFalse(gui.occupied_path_pixels(guest,receipt|{'engine_sha256':'0'*64}))
                image.putpixel((232,290),(1,2,3));image.save(file)
                self.assertFalse(gui.occupied_path_pixels(guest,receipt))
                Image.new('RGB',(640,480),'gray').save(file)
                self.assertFalse(gui.occupied_path_pixels(guest,receipt))

    def test_return_requires_unchanged_full_disk_and_visible_summary(self):
        with tempfile.TemporaryDirectory() as folder:
            guest=Mock();guest.path=Path(folder);(guest.path/'hash-before.jsonl').write_text('a'*64+' /dev/vda\n')
            for changed in (False,True):
                receipt={};remote=Mock(return_value=('b' if changed else 'a')*64+' /dev/vda\n');key=Mock()
                with patch.object(gui,'capture',return_value=words('Click Install to begin')):
                    if changed:
                        with self.assertRaisesRegex(RuntimeError,'changed the target disk'):gui.verify_return_cancel(guest,receipt,remote,key)
                        self.assertFalse(receipt['erase_return_cancel']['passed'])
                    else:
                        gui.verify_return_cancel(guest,receipt,remote,key)
                        self.assertTrue(receipt['erase_return_cancel']['passed'])
                key.assert_called_once_with(guest,'ret')

    def test_footer_action_reads_its_button_interior_and_never_body_prose(self):
        guest=Mock()
        with patch.object(gui,'wait',return_value=(words('Click Install to begin'),[{'x':645,'y':527}])) as waiting, \
                patch.object(gui,'click') as click:
            gui.button(guest,'summary','Install')
            self.assertEqual(waiting.call_args.kwargs,{'region':gui.FOOTER_PRIMARY_TEXT_REGION,'psm':'7'})
            click.assert_called_once_with(guest,645,527)
        with patch.object(gui,'wait') as waiting,patch.object(gui,'click') as click:
            with self.assertRaises(ValueError):gui.button(guest,'unknown','Agree')
            waiting.assert_not_called();click.assert_not_called()

    def test_choice_navigation_never_triggers_the_forms_default_return_button(self):
        selected=words('English (United Kingdom)')
        for word in selected:word['y']=348;word['h']=12
        keys=[]
        def key(guest,code):
            if code=='ret':raise RuntimeError('Default Continue was activated before verification')
            keys.append(code)
        with patch.object(gui,'wait',return_value=([],[{'left':134,'y':354}])), \
                patch.object(gui,'click'),patch.object(gui,'capture',return_value=selected):
            gui.choose(Mock(),'Language',['C.UTF-8','en_GB.UTF-8'],'en_GB.UTF-8','English (United Kingdom)',key)
        self.assertEqual(keys,['esc','home','down','tab'])

    def test_choice_navigation_still_rejects_the_wrong_visible_value(self):
        with patch.object(gui,'wait',return_value=([],[{'left':134,'y':354}])), \
                patch.object(gui,'click'),patch.object(gui,'capture',return_value=words('unrelated')), \
                patch.object(gui.time,'monotonic',side_effect=[0,6]):
            with self.assertRaisesRegex(RuntimeError,'visibly select'):
                gui.choose(Mock(),'Language',['en_GB.UTF-8'],'en_GB.UTF-8','English (United Kingdom)',Mock())

    def test_account_value_crop_follows_actual_label_row_not_old_fixed_coordinates(self):
        with patch.object(gui,'wait') as wait:
            gui.wait_account_value(Mock(),'short-name',{'y':226},'tester')
            self.assertEqual(wait.call_args.kwargs,{'timeout':10,'region':(246,218,440,17),'psm':'7'})
            self.assertEqual(wait.call_args.args[2],'tester')
        with patch.object(gui,'wait') as wait:
            with self.assertRaises(ValueError):gui.wait_account_value(Mock(),'short-name',{'y':100},'tester')
            wait.assert_not_called()

    def test_license_consent_uses_actual_button_text_after_modal_recognition(self):
        captured=words('Software License Agreement To continue installing Disagree')
        guest=Mock()
        with patch.object(gui,'wait',side_effect=[(captured,[]),(words('Agree'),[{'x':612,'y':355}])]) as waiting, \
                patch.object(gui,'click') as click:
            gui.agree_to_license(guest)
            click.assert_called_once_with(guest,612,355)
            self.assertEqual(waiting.call_args_list[0].kwargs['region'],gui.LICENSE_DIALOG_REGION)
            self.assertEqual(waiting.call_args_list[1].kwargs,{'region':gui.AGREE_TEXT_REGION,'psm':'7'})

    def test_body_mentions_of_agree_never_substitute_for_an_actual_modal_button(self):
        for captured,points in [(words('Software License Agreement Agree Disagree'),[{'x':393,'y':323}]),
                                (words('Software License Agreement To continue installing Disagree'),[{'x':1,'y':2},{'x':3,'y':4}])]:
            with patch.object(gui,'wait',side_effect=[(captured,[]),(words('Agree'),points)]), \
                    patch.object(gui,'click') as click:
                with self.assertRaises(RuntimeError):gui.agree_to_license(Mock())
                click.assert_not_called()

    def test_visible_fixture_and_wrapped_warning_are_required_before_an_erase_click(self):
        text='/dev/vda1 1 GB vfat VM_DATA every listed partition will be removed'
        for missing in ('/dev/vda1','VM_DATA','vfat','removed'):
            with self.subTest(missing=missing):
                captured=words(text.replace(missing,'unrelated'))
                receipt={'occupied_gui_fixture':True}
                with patch.object(gui,'wait',return_value=(captured,[{'x':300,'y':400}])),patch.object(gui,'click') as click:
                    with self.assertRaisesRegex(RuntimeError,'no erase click'):gui.confirm_erase(Mock(),receipt)
                    click.assert_not_called()
                self.assertNotIn('gui_erase_confirmation',receipt)
        captured=words(text)
        for word in captured[7:]:word['line']=('1','1','1','2')
        self.assertTrue(gui.occupied_confirmation_visible(captured))

    def test_valid_question_retains_evidence_before_the_explicit_button_click(self):
        receipt={'occupied_gui_fixture':True};guest=Mock()
        captured=words('/dev/vda1 vfat VM_DATA every listed partition will be removed')
        def click(*args):
            self.assertEqual(receipt['gui_erase_confirmation']['screenshot'],'gui-11-erase-confirmation.ppm')
            self.assertEqual(receipt['gui_erase_confirmation']['visual_review'],'REQUIRED')
        with patch.object(gui,'wait',return_value=(captured,[{'x':2,'y':3},{'x':4,'y':5}])), \
                patch.object(gui,'click',side_effect=click) as clicked:
            gui.confirm_erase(guest,receipt)
            clicked.assert_called_once_with(guest,4,5)

    def test_blank_disk_mode_keeps_the_established_explicit_confirmation(self):
        guest=Mock();receipt={}
        with patch.object(gui,'wait',return_value=(words('Erase and Install'),[{'x':2,'y':3}])), \
                patch.object(gui,'click') as clicked:
            gui.confirm_erase(guest,receipt)
        clicked.assert_called_once_with(guest,2,3)
        self.assertNotIn('gui_erase_confirmation',receipt)

    def test_occupied_option_without_gui_refuses_before_environment_or_process_work(self):
        with patch.object(proof.os,'geteuid') as uid,patch.object(proof.subprocess,'run') as process:
            diagnostic=io.StringIO()
            with contextlib.redirect_stderr(diagnostic),self.assertRaises(SystemExit) as error:
                proof.main(['unused.iso','--occupied-gui'])
            self.assertEqual(error.exception.code,2)
            self.assertIn('--occupied-gui requires --gui',diagnostic.getvalue())
            uid.assert_not_called();process.assert_not_called()

    def test_legacy_plan_cannot_claim_partition_evidence(self):
        with self.assertRaisesRegex(ValueError,'existing test partition'):
            proof.check_occupied_plan({'ok':True,'erases':['/dev/vda'],'warnings':[]})


if __name__=='__main__':unittest.main()
