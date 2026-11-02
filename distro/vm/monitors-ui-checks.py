#!/usr/bin/env python3
"""Pure canvas arithmetic and mocked UI observations; no VM or pointer input."""
import importlib.util
from pathlib import Path
import tempfile,unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('monitors_ui',Path(__file__).with_name('monitors-ui.py'))
ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)


def words(text):
    return [{'text':w,'x':i*40,'y':100,'w':30,'h':16,'line':('1','1','1','1')} for i,w in enumerate(text.split())]


class MonitorsUiChecks(unittest.TestCase):
    def record(self):
        return {'topology':{'primary':'Virtual-1','outputs':[
            {'name':'Virtual-1','x':0,'y':0,'width':1280,'height':800,'scale':1,'transform':'normal'},
            {'name':'Virtual-2','x':1280,'y':0,'width':1280,'height':800,'scale':1,'transform':'normal'}]}}

    def test_absolute_input_uses_whole_guest_canvas_not_primary_screenshot_width(self):
        canvas=ui.input_canvas(self.record(),(1280,800));self.assertEqual(canvas,(2560,800))
        guest=Mock()
        with patch.object(ui.time,'sleep'):ui.tap(guest,13,11,canvas)
        events=guest.qmp.call_args_list[0].args[1]['events']
        self.assertEqual(events[0]['data']['value'],166)
        self.assertEqual(guest.qmp.call_args_list[-1].args[1]['events'][0]['data']['down'],False)

    def test_unreviewed_origin_scale_rotation_or_geometry_refuses_before_input(self):
        for defect in ('origin','scale','rotation','geometry'):
            value=self.record()
            if defect=='origin':value['topology']['outputs'][0]['x']=50
            elif defect=='scale':value['topology']['outputs'][1]['scale']=1.5
            elif defect=='rotation':value['topology']['outputs'][1]['transform']='90'
            size=(1600,900) if defect=='geometry' else (1280,800)
            guest=Mock()
            with patch.object(ui,'capture',return_value=(size,words('File Edit View'))):
                with self.assertRaises(ValueError):ui.open_panel(guest,value)
                guest.qmp.assert_not_called()

    def test_panel_opening_requires_actual_managed_control_text(self):
        for controls in ('Mirror displays Show the desktop on','Mirror displays unrelated'):
            guest=Mock();steps=[((1280,800),[],{'x':80,'y':100}),((1280,800),[],{'x':220,'y':160}),
                               ((1280,800),words(controls),{'x':300,'y':400})]
            with patch.object(ui,'capture',return_value=((1280,800),words('File Edit View'))), \
                    patch.object(ui,'wait',side_effect=steps),patch.object(ui,'tap') as tap:
                if 'unrelated' in controls:
                    with self.assertRaises(RuntimeError):ui.open_panel(guest,self.record())
                else:
                    result=ui.open_panel(guest,self.record());self.assertTrue(result['opened'])
                    self.assertIn('persistence NOT TESTED',result['scope'])
                    self.assertEqual(result['visual_review'],'REQUIRED')
                self.assertEqual(tap.call_count,3)

    def activation_record(self,pending=None):
        value=self.record();topology=value['topology']
        topology.update(mode='extend',generation=1 if pending is None else 2,pending=False if pending is None else pending)
        value.update(display_generation=topology['generation'],display_applied_generation=topology['generation'])
        value['backend_outputs']=[{'name':'Virtual-1','active':True},{'name':'Virtual-2','active':pending is not None}]
        if pending is None:
            topology['outputs'].pop()
            topology['available']=[{'name':'Virtual-2','label':'PR VM 2'}]
        else:topology['available']=[]
        return value

    def test_second_display_activation_requires_actual_connected_off_monitor_before_input(self):
        for defect in ('no-off','already-on','missing-connector'):
            value=self.activation_record()
            if defect=='no-off':value['topology']['available']=[]
            elif defect=='already-on':value['backend_outputs'][1]['active']=True
            else:value['backend_outputs'].pop()
            with self.subTest(defect=defect),patch.object(ui,'capture') as capture,patch.object(ui,'tap') as tap:
                with self.assertRaises(ValueError):ui.enable_second(Mock(),value,Mock())
                capture.assert_not_called();tap.assert_not_called()

    def test_actual_activation_and_keep_are_checked_against_acknowledged_backend_state(self):
        initial=self.activation_record();pending=self.activation_record(True);kept=self.activation_record(False)
        heading=words('Show the desktop on');label=words('PR VM 2')
        for w in label:w['y']=130
        question=words('Keep this display setting');button=words('Keep')
        button[0]['y']=160
        frames=[((1280,800),heading+label),((1280,800),question+button),((1280,800),[])]
        recollect=Mock(side_effect=[pending,kept]);guest=Mock()
        with patch.object(ui,'capture',side_effect=frames),patch.object(ui,'tap') as tap:
            final,record=ui.enable_second(guest,initial,recollect)
        self.assertIs(final,kept);self.assertEqual(record['enabled_connector'],'Virtual-2')
        self.assertEqual([call.args[3] for call in tap.call_args_list],[(1280,800),(2560,800)])
        self.assertEqual(recollect.call_count,2)

    def test_unknown_desktop_or_out_of_bounds_pointer_never_receives_input(self):
        guest=Mock()
        with patch.object(ui,'capture',return_value=((1280,800),words('unrelated'))):
            with self.assertRaises(RuntimeError):ui.open_panel(guest,self.record())
        with self.assertRaises(ValueError):ui.tap(guest,1280,0,(1280,800))
        guest.qmp.assert_not_called()

    def test_duplicate_monitor_labels_require_one_enabled_and_one_disabled_visible_row(self):
        from PIL import Image,ImageDraw
        labels=[{'left':40,'top':20,'x':70,'y':26},{'left':40,'top':42,'x':70,'y':48}]
        with tempfile.TemporaryDirectory() as directory:
            frame=Path(directory)/'labels.png'
            for ink in (136,17,0,100):
                image=Image.new('RGB',(160,80),'#ddd');draw=ImageDraw.Draw(image)
                draw.rectangle((42,22,50,27),fill=(ink,)*3)
                draw.rectangle((42,44,50,49),fill=(17,)*3)
                # Checkbox borders outside label ink must not affect selection.
                draw.rectangle((20,20,32,32),outline='black')
                image.save(frame)
                if ink==136:self.assertEqual(ui.enabled_duplicate_label(frame,labels),labels[1])
                else:self.assertIsNone(ui.enabled_duplicate_label(frame,labels))
            moved=[dict(labels[0]),{**labels[1],'left':50}]
            self.assertIsNone(ui.enabled_duplicate_label(frame,moved))
            self.assertIsNone(ui.enabled_duplicate_label(frame,labels[:1]))


if __name__=='__main__':unittest.main()
