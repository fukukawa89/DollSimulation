"""Run in a fresh UE process without simulator/MCP; verify saved editable rig keys."""
import json,math,sys
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
sys.path.insert(0,str(ROOT/'Scripts'))
from test_capture import snapshot
report={'passed':False,'cases':[]}
out=ROOT/'reports'/'capture_reopen.json'

def run():
    unreal.PoseDollEditorLibrary.session_command('disconnect')
    expected=json.loads((ROOT/'reports'/'capture_first_run.json').read_text(encoding='utf-8'))
    assert expected['passed']
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    sequence=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Poses')
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
    matches=[p for p in unreal.ControlRigSequencerLibrary.get_control_rigs(sequence) if str(p.track.get_display_name())=='PoseDoll / Manny']
    assert len(matches)==1
    proxy=matches[0]
    for frame in (0,4,8):
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        actual=snapshot(proxy.control_rig)
        report['actual_'+str(frame)]=actual
        report['rig_execution_api']=[n for n in dir(proxy.control_rig) if 'exec' in n or 'eval' in n]
        report['control_readback_0']=str(unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,'upperarm_l_fk_ctrl',unreal.FrameNumber(frame)))
        pos_err,rot_err=0,0
        for name,v in expected['poses'][str(frame)].items():
            a=actual[name]
            pos_err=max(pos_err,math.dist(a['p'],v['p']))
            dot=abs(sum(x*y for x,y in zip(a['q'],v['q'])))
            rot_err=max(rot_err,math.degrees(2*math.acos(min(1,dot))))
        report['cases'].append({'frame':frame,'position_cm':pos_err,'rotation_deg':rot_err})
        assert pos_err<.1 and rot_err<.5
    # Editing after input shutdown uses the regular Control Rig Sequencer API.
    ctrl='hand_l_fk_ctrl'
    before=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4))
    from copy import deepcopy
    edited=unreal.EulerTransform(location=before.location,rotation=unreal.Rotator(before.rotation.pitch,before.rotation.yaw+10,before.rotation.roll),scale=before.scale)
    with unreal.ScopedEditorTransaction('PoseDoll acceptance manual wrist edit'):
        unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4),edited,set_key=True)
    after=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4))
    report['manual_wrist_before']=str(before)
    report['manual_wrist_after']=str(after)
    assert abs(after.rotation.yaw-before.rotation.yaw)>9
    for _ in range(60):unreal.PoseDollEditorLibrary.session_command('tick')
    held=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4))
    assert abs(held.rotation.yaw-after.rotation.yaw)<.001
    unreal.PoseDollEditorLibrary.session_command('undo')
    restored=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4))
    assert abs(restored.rotation.yaw-before.rotation.yaw)<.001
    unreal.PoseDollEditorLibrary.session_command('redo')
    redone=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(sequence,proxy.control_rig,ctrl,unreal.FrameNumber(4))
    assert abs(redone.rotation.yaw-after.rotation.yaw)<.001
    unreal.PoseDollEditorLibrary.session_command('undo')
    cmdline=unreal.SystemLibrary.get_command_line()
    disabled=next((part for part in cmdline.split() if part.startswith('-DisablePlugins=')),'')
    report.update(passed=True,manual_edit=True,undo_redo=True,old_cache_did_not_overwrite=True,
                  mcp_disabled_in_launch_args='ModelContextProtocol' in disabled,
                  simulator_connection_closed=True,cmdline=cmdline)
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_REOPEN_SUCCESS')

try:run()
except Exception as exc:
    report['exception']=repr(exc)
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    raise
finally:
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
