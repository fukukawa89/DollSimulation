"""Real editor capture acceptance. Writes only the dedicated /Game/PoseDollLab assets."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
REPORT=ROOT/'reports'/'capture_first_run.json'
report={'passed':False,'steps':[]}

def command(action,argument=''):
    result=json.loads(unreal.PoseDollEditorLibrary.session_command(action,argument))
    report['steps'].append({'action':action,**result})
    REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    assert result['ok'],result
    return result

def snapshot(rig):
    # Commandlet Python runs within one editor tick. Explicitly execute the native
    # Forward Solve after Sequencer evaluates its channels (no pose inputs written).
    assert rig.execute('Forwards Solve')
    h=rig.get_hierarchy()
    out={}
    for key in h.get_all_keys():
        if key.type!=unreal.RigElementType.BONE:continue
        if str(key.name) not in ('pelvis','head','upperarm_l','lowerarm_l','hand_l','hand_r','thigh_l','calf_l','foot_l','foot_r'):continue
        t=h.get_global_transform(key)
        out[str(key.name)]={'p':[t.translation.x,t.translation.y,t.translation.z],
                            'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]}
    return out

def run():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    map_path='/Game/PoseDollLab/PoseDoll_Test'
    seq_path='/Game/PoseDollLab/PoseDoll_Poses'
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        assert level.load_level(map_path)
    else:assert level.new_level(map_path)
    matches=[a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny']
    assert len(matches)<=1,'Ambiguous owned demonstration actor'
    actor=matches[0] if matches else actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(120,35,0),unreal.Rotator(0,25,0))
    actor.set_actor_label('PoseDoll_TestManny')
    mesh=unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    component=actor.skeletal_mesh_component
    component.set_skeletal_mesh_asset(mesh)
    sequence=unreal.load_asset(seq_path)
    if sequence:
        for b in sequence.get_bindings():b.remove()
    else:sequence=unreal.AssetToolsHelpers.get_asset_tools().create_asset('PoseDoll_Poses','/Game/PoseDollLab',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    sequence.set_display_rate(unreal.FrameRate(24,1))
    sequence.set_playback_start(0);sequence.set_playback_end(48)
    binding=sequence.add_possessable(actor)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
    assert unreal.PoseDollEditorLibrary.bind_target(sequence,component)
    report['binding']=str(binding.get_id())
    report['poses']={}
    for frame,fixture in [(0,'neutral.sample.json'),(4,'asymmetric_pose.sample.json'),(8,'left_shoulder_forward_90.sample.json')]:
        command('fixture',fixture)
        command('capture',json.dumps({'frame':frame,'advance':4}))
        proxies=unreal.ControlRigSequencerLibrary.get_control_rigs(sequence)
        matches=[p for p in proxies if str(p.track.get_display_name())=='PoseDoll / Manny']
        report['rig_proxies']=[{'track':p.track.get_path_name(),'display':str(p.track.get_display_name()),'binding':str(p.proxy.binding_id)} for p in proxies]
        assert len(matches)==1,report['rig_proxies']
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        report['poses'][str(frame)]=snapshot(matches[0].control_rig)
    report['tracks']=len(binding.get_tracks())
    assert report['tracks']==1
    assert unreal.EditorAssetLibrary.save_loaded_asset(sequence,False)
    assert level.save_current_level()
    report['passed']=True
    REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_CAPTURE_SUCCESS')

if __name__=='__main__':
    try:run()
    except Exception as exc:
        report['exception']=str(exc)
        REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
        raise
    finally:
        unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
