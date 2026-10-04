"""Fresh-process persistence and editing without opening/binding the doll panel."""
import json, math, traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
report={'passed':False,'cases':[]}
def cmd(a,arg=''):
    result=json.loads(unreal.PoseDollEditorLibrary.session_command(a,arg));assert result.get('ok'),result
    return result
def snapshot(rig):
    assert rig.execute('Forwards Solve')
    h=rig.get_hierarchy()
    return {str(k.name):h.get_global_transform(k) for k in h.get_all_keys() if k.type==unreal.RigElementType.BONE}
def compare(expected,actual):
    pe,re=0.,0.
    for name,value in expected.items():
        t=actual[name];pe=max(pe,math.dist(value['p'],[t.translation.x,t.translation.y,t.translation.z]))
        q=unreal.Quat(*value['q']);re=max(re,math.degrees(q.angular_distance(t.rotation)))
    assert pe<.1 and re<.5,(pe,re)
    return {'position_cm':pe,'rotation_deg':re}
try:
    expected=json.loads((ROOT/'reports/pose_editing.json').read_text(encoding='utf-8'));assert expected['passed']
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert level.load_level(expected['map'])
    seq=unreal.load_asset(expected['sequence']);assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    report['readback']={}
    for frame,pose in expected['expected_frames'].items():
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(int(frame))
        report['readback'][frame]=compare(pose,snapshot(rig));cmd('tick')
    report['cases'].append('saved native rig poses reopen without a source connection')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30);cmd('tick')
    frame=unreal.FrameNumber(30)
    t=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,'hand_l_fk_ctrl',frame)
    t.rotation=unreal.Rotator(t.rotation.pitch,t.rotation.yaw+15,t.rotation.roll)
    with unreal.ScopedEditorTransaction('Manual edit after reopening'):
        unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,'hand_l_fk_ctrl',frame,t,set_key=True)
    before=snapshot(rig)
    with unreal.ScopedEditorTransaction('Rebuild IK without panel or source'):
        unreal.ControlRigSequencerLibrary.set_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',frame,True,set_key=True)
    assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',frame),cmd('status')
    after=snapshot(rig)
    for n in ('pelvis','spine_04','head','hand_l','hand_r','foot_l','foot_r'):
        assert (before[n].translation-after[n].translation).length()<.1,n
        assert math.degrees(before[n].rotation.angular_distance(after[n].rotation))<.5,n
    report['cases'].append('native IK switch after reopen uses the latest manual FK edit without panel/binding')
    report['passed']=True
except Exception:
    report['exception']=traceback.format_exc();unreal.log_error(report['exception'])
finally:
    (ROOT/'reports/pose_editing_reopen.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_EDITING_REOPEN_'+('PASS' if report['passed'] else 'FAIL'))
    unreal.SystemLibrary.quit_editor()
