"""Fresh UE process, saved Snapshot keys, no running source. Tests actual Rig bones."""
import json,math,os,traceback
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());run=os.environ['O4_TEST_RUN'];folder=root/'reports/o4'/run;expected=json.loads((folder/'editor.json').read_text(encoding='utf-8'));report={'passed':False,'cases':[],'scope':'Fresh UE Editor reopening static-capture assets; source disconnected'}
try:
    assert expected['passed']
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/PoseDollO4/'+run+'/StaticCaptureTest')
    seq=unreal.load_asset('/Game/PoseDollO4/'+run+'/O4_StaticPoses');assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    proxies=[p for p in unreal.ControlRigSequencerLibrary.get_control_rigs(seq) if str(p.track.get_display_name())=='PoseDoll / Manny'];assert len(proxies)==1;rig=proxies[0].control_rig
    for frame,bones in expected['expected_bones'].items():
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(int(frame));assert rig.execute('Forwards Solve');h=rig.get_hierarchy();pos=ang=0
        # Compare only mapped body controls; fingers and corrective helpers are native-rig-owned.
        mapping=json.loads((root/'Shared/Profiles/manny_body_ue582_v1.json').read_text())['controls']
        for entry in mapping:
            name=entry['bone'];v=bones[name];t=h.get_global_transform(unreal.RigElementKey(type=unreal.RigElementType.BONE,name=name))
            a=[t.translation.x,t.translation.y,t.translation.z];q=[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w];pos=max(pos,math.dist(a,v['translation_cm']));dot=abs(sum(x*y for x,y in zip(q,v['rotation_xyzw'])));ang=max(ang,math.degrees(2*math.acos(min(1,dot))))
        report['cases'].append({'frame':int(frame),'max_position_cm':pos,'max_rotation_deg':ang});assert pos<.1 and ang<.5,report['cases'][-1]
    # Native key editing after the simulator is gone remains editable and undoable.
    ctrl='hand_l_fk_ctrl';frame=unreal.FrameNumber(24);before=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame)
    edited=unreal.EulerTransform(location=before.location,rotation=unreal.Rotator(before.rotation.pitch,before.rotation.yaw+10,before.rotation.roll),scale=before.scale)
    with unreal.ScopedEditorTransaction('O4 offline manual wrist edit'):
        unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,ctrl,frame,edited,set_key=True)
    after=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame);assert abs(after.rotation.yaw-before.rotation.yaw)>9
    unreal.PoseDollEditorLibrary.session_command('undo')
    restored=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame);assert abs(restored.rotation.yaw-before.rotation.yaw)<.001
    report.update(passed=True,manual_edit_and_undo=True)
except:report['exception']=traceback.format_exc();unreal.log_error(report['exception'])
finally:
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence();(folder/'reopen.json').write_text(json.dumps(report,indent=2),encoding='utf-8');unreal.SystemLibrary.quit_editor()
