"""Contact heights, position-only mode and saved FK/switches in an owned sequence."""
import json,sys,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());sys.path.insert(0,str(ROOT/'Scripts'))
from test_capture import snapshot
report={'passed':False,'cases':[]}
out=ROOT/'reports/contacts_extended.json'
def call(action,arg=''):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg));assert r.get('ok',True),r;return r
try:
    call('disconnect')
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
    path='/Game/PoseDollLab/PoseDoll_Contacts';seq=unreal.load_asset(path)
    if seq:
        for b in seq.get_bindings():b.remove()
    else:seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('PoseDoll_Contacts','/Game/PoseDollLab',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_end(48);seq.add_possessable(actor)
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    call('mask','FullBody');call('fixture','neutral.sample.json')
    neutral=call('pose_report')['bones'];hand=neutral['hand_l']['translation_cm']
    report['poses']={}
    for i,(height,rotation) in enumerate(((15,True),(35,True),(55,True),(35,False))):
        for chain in ('leg_l','leg_r','arm_l'):call('contact',json.dumps({'chain':chain,'enabled':False}))
        call('fixture','neutral.sample.json')
        for chain in ('leg_l','leg_r'):call('contact',json.dumps({'chain':chain}))
        target=[hand[0]+10,hand[1]+15,hand[2]+height]
        call('contact',json.dumps({'chain':'arm_l','position_cm':target,'rotation':rotation}))
        r=call('fixture','asymmetric_pose.sample.json')
        p=call('pose_report')
        case={'height_delta_cm':height,'target_position_cm':target,'lock_rotation':rotation,
              'position_error_cm':r['contact_position_error_cm'],'rotation_error_deg':r['contact_rotation_error_deg'],
              'reachable':r['contacts_reachable'],'pole_degenerate':r['pole_degenerate']}
        assert r['contacts_reachable'] and r['contact_position_error_cm']<=.5,case
        if rotation:assert r['contact_rotation_error_deg']<=1,case
        report['cases'].append(case)
        frame=i*4;call('capture',json.dumps({'frame':frame}))
        proxy=next(p for p in unreal.ControlRigSequencerLibrary.get_control_rigs(seq) if str(p.track.get_display_name())=='PoseDoll / Manny')
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        report['poses'][str(frame)]=snapshot(proxy.control_rig)
    report['key_report']=call('key_report')
    assert unreal.EditorAssetLibrary.save_loaded_asset(seq,False)
    report.update(passed=True,sequence=path)
    unreal.log('POSEDOLL_CONTACTS_EXTENDED_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc);raise
finally:
    call('disconnect')
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
