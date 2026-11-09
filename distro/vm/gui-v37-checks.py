#!/usr/bin/env python3
"""Synthetic update37 GUI safety controls; not an ISO/installation pass."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('gui37',Path(__file__).with_name('gui-drive-v37.py'))
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)


def words(text,y=250):
    return [{'text':word,'x':100+i*55,'y':y,'w':45,'h':12,'left':100+i*55,'line':(y,)} for i,word in enumerate(text.split())]


def point(x,y):return {'x':x,'y':y,'left':x}


class Gui37Checks(unittest.TestCase):
    def test_unknown_source_refuses_before_any_input_or_capture(self):
        with patch.object(v.gui,'wait') as wait,patch.object(v.gui,'click') as click:
            with self.assertRaises(ValueError):v.drive(Mock(),'synthetic-only',{},Mock(),Mock(),Mock())
            wait.assert_not_called();click.assert_not_called()

    def test_legacy_or_ambiguous_footer_cannot_trigger_new_primary_action(self):
        with patch.object(v.gui,'wait') as wait,patch.object(v.gui,'click') as click:
            with self.assertRaises(ValueError):v.button(Mock(),'summary','Install')
            wait.assert_not_called();click.assert_not_called()
        with patch.object(v.gui,'wait',return_value=([],[point(580,460),point(600,460)])),patch.object(v.gui,'click') as click:
            with self.assertRaises(RuntimeError):v.button(Mock(),'summary','Start')
            click.assert_not_called()

    def test_button_pair_requires_alignment_order_and_unique_lowest_row(self):
        data=words('Disagree Agree',220)+words('Disagree Agree',320)
        self.assertEqual(v.pair(data,'Disagree','Agree')[0]['y'],326)
        for data in (words('Agree Disagree'),words('Disagree',200)+words('Agree',250),
                     words('Disagree Agree Agree')):
            with self.subTest(data=data),self.assertRaises(RuntimeError):v.pair(data,'Disagree','Agree')

    def test_old_license_preface_cannot_substitute_for_production_new_question(self):
        data=words('Software License Agreement To continue installing Disagree Agree')
        with patch.object(v.gui,'wait',return_value=(data,[])),patch.object(v.gui,'click') as click:
            with self.assertRaises(RuntimeError):v.agree(Mock())
            click.assert_not_called()

    def test_wrong_target_or_missing_loss_warning_never_reaches_erase_click(self):
        for text in ('Erase Disk /dev/vdb Everything on this disk will be lost Cancel Erase and Install',
                     'Erase Disk /dev/vda Cancel Erase and Install'):
            with self.subTest(text=text),patch.object(v.gui,'wait',return_value=(words(text),[])),patch.object(v.gui,'click') as click:
                with self.assertRaises(RuntimeError):v.confirm_erase(Mock(),{},Mock(),Mock())
                click.assert_not_called()

    def test_new_geometry_cannot_use_legacy_digit_pixel_fallback(self):
        data=words('Erase Disk /dev/vda Everything on this disk will be lost /dev/vdal vfat VM_DATA every listed partition will be removed Cancel Erase and Install')
        with patch.object(v.gui,'wait',return_value=(data,[])),patch.object(v.gui,'click') as click:
            with self.assertRaisesRegex(RuntimeError,'literally'):v.confirm_erase(Mock(),{'occupied_gui_fixture':True},Mock(),Mock())
            click.assert_not_called()

    def test_changed_disk_on_return_prevents_reopen_and_explicit_erase(self):
        with tempfile.TemporaryDirectory() as directory:
            guest=Mock();guest.path=Path(directory);(guest.path/'hash-before.jsonl').write_text('a'*64+' /dev/vda\n')
            with patch.object(v,'recognize_erase',return_value=point(500,400)),patch.object(v,'button') as button,patch.object(v.gui,'click') as click:
                with self.assertRaisesRegex(RuntimeError,'Return changed'):
                    v.confirm_erase(guest,{'verify_erase_return_requested':True},Mock(return_value='b'*64+' /dev/vda\n'),Mock())
                button.assert_not_called();click.assert_not_called()

    def test_unknown_account_row_refuses_before_waiting_or_typing(self):
        with patch.object(v.gui,'wait') as wait:
            with self.assertRaises(ValueError):v.account_value(Mock(),'account',point(140,50),'Test User')
            wait.assert_not_called()


if __name__=='__main__':unittest.main()
