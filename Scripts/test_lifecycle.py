"""Controlled lifecycle/Clutch/rollback acceptance using only the PoseDoll test area.
Run against soak_driver.py after the timed soak; it must not run concurrently with it.
"""
import json,time,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());OUT=ROOT/'reports/lifecycle.json'
report={'passed':False,'checks':{}}
def call(action,arg='',require=True):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg))
    if require:assert r.get('ok',True),r
    return r
def control(**settings):
    (ROOT/'reports/soak_control.json').write_text(json.dumps({'hold':True,'disable_restarts':True,**settings}),encoding='utf-8')
def pump(seconds=.25):
    end=time.monotonic()+seconds
    while time.monotonic()<end:time.sleep(.02);call('tick')
def live(clutch=False):
    call('connect')
    end=time.monotonic()+4
    while time.monotonic()<end:
        pump(.06)
        if call('status')['state']=='Ready':break
    pump(.15);call('resume','clutch' if clutch else '');pump(.2)
    assert call('status')['state']=='Live'
def pose_difference(a,b):
    worst=0.
    for name,value in a.items():
        assert name in b,name
        q,r=value['rotation_xyzw'],b[name]['rotation_xyzw']
        d=abs(sum(x*y for x,y in zip(q,r)))
        worst=max(worst,math.degrees(2*math.acos(min(1.,d))))
    return worst
def make_sequence(name,actor):
    path='/Game/PoseDollLab/'+name
    seq=unreal.load_asset(path)
    if seq:
        for binding in seq.get_bindings():binding.remove()
        for track in seq.get_tracks():seq.remove_track(track)
    else:seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,'/Game/PoseDollLab',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_end(48)
    if actor:seq.add_possessable(actor)
    return seq
try:
    control();call('freeze')
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
    seq=make_sequence('PoseDoll_Lifecycle',actor)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    call('mask','FullBody');call('fixture','neutral.sample.json');call('capture',json.dumps({'frame':0}))
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    ctrl='hand_l_fk_ctrl'
    t=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,unreal.FrameNumber(0))
    rotation=t.rotation;rotation.roll+=12;t.rotation=rotation
    unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,ctrl,unreal.FrameNumber(0),t,set_key=True)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0);rig.execute('Forwards Solve')
    live(True)
    initial=call('pose_report');assert initial['clutch']
    assert pose_difference(initial['controls'],{k:initial['target_baseline'][k] for k in initial['controls']})<.001
    pump(1.2);same=call('pose_report')['controls']
    assert pose_difference(initial['controls'],same)<.001
    control(angles_rad={'elbow_l.flex':.2});pump(.5)
    changed=call('pose_report')['controls'];assert pose_difference(initial['controls'],changed)>5
    pump(1.2);assert pose_difference(changed,call('pose_report')['controls'])<.001
    control();pump(.5);assert pose_difference(initial['controls'],call('pose_report')['controls'])<.001
    report['checks']['clutch_fixed_baseline_no_drift']=True
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(4);pump(.06)
    assert call('status')['state']=='Frozen'
    report['checks']['clutch_time_change_freezes']=True

    call('disconnect');call('fixture','neutral.sample.json')
    section=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].track.get_sections()[0]
    before=call('key_report')['control_keys']
    with unreal.ScopedEditorTransaction('PoseDoll test remove channel'):
        section.modify();assert section.remove_transform_parameter('head_ctrl')
    incomplete=call('key_report')['control_keys']
    failed=call('capture',json.dumps({'frame':8}),False)
    assert not failed['ok'] and call('key_report')['control_keys']==incomplete
    call('undo');assert call('key_report')['control_keys']==before
    report['checks']['capture_transaction_rollback']=True

    other=make_sequence('PoseDoll_OtherSequence',None)
    live();unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(other);pump(.06)
    assert call('status')['state']=='Fault'
    assert not call('capture',json.dumps({'frame':8}),False)['ok']
    report['checks']['sequence_switch_stops_old_session']=True
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    call('fixture','neutral.sample.json')
    sub=other.add_track(unreal.MovieSceneSubTrack).add_section();sub.set_sequence(seq);sub.set_range(0,48)
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(other)
    unreal.LevelSequenceEditorBlueprintLibrary.focus_level_sequence(sub)
    assert not call('capture',json.dumps({'frame':8}),False)['ok']
    report['checks']['nested_sequence_rejected']=True
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    live()
    with unreal.ScopedEditorTransaction('PoseDoll test delete target'):
        assert actors.destroy_actor(actor)
    pump(.06);assert call('status')['state']=='Fault'
    assert not call('capture',json.dumps({'frame':8}),False)['ok']
    call('undo');actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    report['checks']['target_delete_and_undo']=True
    live();unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
    assert level.load_level('/Game/Maps/Map1');pump(.06)
    assert call('status')['state']=='Fault'
    report['checks']['level_change_stops_old_world']=True
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
    original=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Poses')
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(original)
    assert unreal.PoseDollEditorLibrary.bind_target(original,actor.skeletal_mesh_component)
    live()
    rig_asset=unreal.load_asset('/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body')
    rig_asset.recompile_vm();pump(.1)
    assert not call('status')['live'],call('status')
    report['checks']['rig_recompile_invalidates_session']=True
    assert unreal.PoseDollEditorLibrary.bind_target(original,actor.skeletal_mesh_component)
    call('fixture','neutral.sample.json')
    report['checks']['explicit_rebind_recovers']=True
    # Save only the dedicated test sequences, never the source Rig or original user map.
    unreal.EditorAssetLibrary.save_loaded_asset(seq,False);unreal.EditorAssetLibrary.save_loaded_asset(other,False)
    report['passed']=True;unreal.log('POSEDOLL_LIFECYCLE_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc);raise
finally:
    call('freeze');control()
    OUT.write_text(json.dumps(report,indent=2),encoding='utf-8')
