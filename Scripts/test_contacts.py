import json,sys
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
sys.path.insert(0,str(ROOT/'Scripts'))
from test_capture import snapshot
out=ROOT/'reports'/'contacts.json'
report={'passed':False,'cases':[]}
def call(action,arg=''):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg))
    assert r['ok'],r
    return r
try:
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny'][0]
    sequence=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Poses')
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
    assert unreal.PoseDollEditorLibrary.bind_target(sequence,actor.skeletal_mesh_component)
    call('fixture','neutral.sample.json')
    baseline=json.loads((ROOT/'reports'/'capture_first_run.json').read_text())['poses']['0']
    for chain,bone in [('leg_l','foot_l'),('leg_r','foot_r'),('arm_l','hand_l')]:
        p=list(baseline[bone]['p'])
        if chain=='arm_l':p=[p[0]+10,p[1]+15,p[2]+25]
        call('contact',json.dumps({'chain':chain,'position_cm':p}))
        r=call('fixture','neutral.sample.json')
        report['cases'].append({'name':'lock_'+chain,**r})
        assert r['contacts_reachable'] and r['contact_position_error_cm']<=.5 and r['contact_rotation_error_deg']<=1,r
    r=call('fixture','asymmetric_pose.sample.json')
    report['cases'].append({'name':'dual_feet_hand_asymmetric',**r})
    assert r['contacts_reachable'],r
    r=call('capture',json.dumps({'frame':12}))
    report['capture_constraints']=r
    call('contact',json.dumps({'chain':'arm_l','position_cm':[500,500,500]}))
    r=call('fixture','neutral.sample.json');report['unreachable']=r
    assert not r['contacts_reachable'] and r['contact_position_error_cm']>100
    rejected=json.loads(unreal.PoseDollEditorLibrary.session_command('capture',json.dumps({'frame':16})))
    assert not rejected['ok'] and rejected['captures']==r['captures']
    report['unreachable_capture_rejected']=True
    # The verification capture is reverted; preserve the three-pose deliverable.
    call('undo')
    for chain in ('leg_l','leg_r','arm_l'):call('contact',json.dumps({'chain':chain,'enabled':False}))
    report['passed']=True
    unreal.log('POSEDOLL_CONTACTS_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc)
    raise
finally:
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
