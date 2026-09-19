import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());out=ROOT/'reports'/'capture_advanced.json'
report={'passed':False,'cases':[]}
def call(action,arg=''):
    result=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg))
    assert result.get('ok',True),result
    return result
try:
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny'][0]
    component=actor.skeletal_mesh_component
    component.set_relative_location(unreal.Vector(15,-10,5),False,False)
    component.set_relative_rotation(unreal.Rotator(8,12,3),False,False)
    for numerator,denominator in [(24,1),(30,1),(24000,1001)]:
        name=f'PoseDoll_Timing_{numerator}_{denominator}'
        seq=unreal.load_asset('/Game/PoseDollLab/'+name)
        if seq:
            for b in seq.get_bindings():b.remove()
        else:seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,'/Game/PoseDollLab',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
        seq.set_display_rate(unreal.FrameRate(numerator,denominator));seq.set_playback_start(0);seq.set_playback_end(50)
        seq.add_possessable(actor)
        assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
        assert unreal.PoseDollEditorLibrary.bind_target(seq,component)
        call('mask','FullBody');call('fixture','neutral.sample.json');call('capture',json.dumps({'frame':7,'linear':False}))
        initial=call('key_report')['control_keys']
        call('fixture','asymmetric_pose.sample.json');call('capture',json.dumps({'frame':11,'linear':True,'advance':4}))
        twice=call('key_report')['control_keys']
        assert twice['hand_l_fk_ctrl']==initial['hand_l_fk_ctrl']*2
        call('undo');assert call('key_report')['control_keys']==initial
        call('redo');assert call('key_report')['control_keys']==twice
        call('mask','UpperBody');call('fixture','left_shoulder_forward_90.sample.json');call('capture',json.dumps({'frame':15}))
        masked=call('key_report')['control_keys']
        assert masked['hand_l_fk_ctrl']>twice['hand_l_fk_ctrl']
        for ctrl,count in twice.items():
            if ctrl.startswith(('thigh_','calf_','foot_','ball_','leg_','finger_','thumb_','index_','middle_','ring_','pinky_')):assert masked[ctrl]==count,(ctrl,count,masked[ctrl])
        # A rejected transform must not leave partial control keys.
        component.set_relative_scale3d(unreal.Vector(1,2,1))
        rejected=json.loads(unreal.PoseDollEditorLibrary.session_command('capture',json.dumps({'frame':19})))
        assert not rejected['ok']
        assert call('key_report')['control_keys']==masked
        component.set_relative_scale3d(unreal.Vector(1,1,1))
        call('mask','FullBody');call('placement',json.dumps({'x_cm':10,'y_cm':-5,'z_cm':3,'yaw_deg':20,'pitch_deg':5,'roll_deg':2}))
        call('fixture','neutral.sample.json');call('capture',json.dumps({'frame':19}))
        report['cases'].append({'fps':[numerator,denominator],'undo_redo':True,'upper_body_mask':True,'nonuniform_rejected':True,'placement_capture':True,'nonzero_actor_component':True})
        unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
    report['passed']=True
    unreal.log('POSEDOLL_ADVANCED_CAPTURE_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc)
    raise
finally:
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
