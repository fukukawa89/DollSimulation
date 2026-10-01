"""Owned O22 digital fixtures only; never labels synthetic input as hardware."""
import sys,json
from pathlib import Path
import unreal
R=Path(unreal.Paths.project_dir());sys.path.insert(0,str(R/'Scripts/O22'))
from capture_bridge import solve,snapshot,errors
D=Path('E:/UnrealProjects/PoseDollLab_UE58_Design/PoseDoll_HW44_Plan/design/Hardware/PoseDoll44/bench/revO22')
OUT=R/'reports/o22_capture.json';report={'passed':False,'source_kind':'SYNTHETIC_P21R','physical_test':False,'targets':{}}
POSES=['a_stand','left_elbow','asymmetric','right_forearm']
def run():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    path='/Game/PoseDoll_O22/MeasurementTest'
    if unreal.EditorAssetLibrary.does_asset_exist(path):assert level.load_level(path)
    else:assert level.new_level(path)
    for target in ['manny','quinn']:
        cfg=json.loads((R/'Shared/Profiles/O22'/f'{target}.json').read_text())
        label='PoseDoll_O22_'+target
        found=[a for a in actors.get_all_level_actors() if a.get_actor_label()==label];assert len(found)<=1
        actor=found[0] if found else actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(0,0 if target=='manny' else 150,0))
        actor.set_actor_label(label);actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset(cfg['mesh']))
        seqpath='/Game/PoseDoll_O22/Measured_'+target
        seq=unreal.load_asset(seqpath)
        if seq:
            for b in seq.get_bindings():b.remove()
        else:seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('Measured_'+target,'/Game/PoseDoll_O22',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
        seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_start(0);seq.set_playback_end(20)
        binding=seq.add_possessable(actor);assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
        cls=unreal.load_class(None,cfg['rig']+'.'+cfg['rig'].split('/')[-1]+'_C')
        track=unreal.ControlRigSequencerLibrary.find_or_create_control_rig_track(actor.get_world(),seq,cls,binding,False)
        proxies=unreal.ControlRigSequencerLibrary.get_control_rigs(seq);assert len(proxies)==1
        unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence();unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0)
        rig=proxies[0].control_rig;record={'sequence':seqpath,'poses':{},'checks':[]}
        report['targets'][target]=record
        for i,pose in enumerate(POSES):
            frame=i*4;solved=solve(D/'ue'/f'{pose}.payload.json',R/'Shared/Profiles/O22'/f'{target}.json',True)
            assert not solved['hardware_capture_eligible']
            keyed=json.loads(unreal.PoseDollEditorLibrary.capture_measured_pose22(seq,rig,frame,str(D/'ue'/f'{pose}.payload.json'),str(R/'Shared/Profiles/O22'/f'{target}.json'),True));assert keyed['ok'],keyed
            unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
            actual=snapshot(rig);err=errors(solved['bones'],actual)
            record['checks'].append({'frame':frame,'pose':pose,**err});record['poses'][str(frame)]=actual
            OUT.write_text(json.dumps(report,indent=2),encoding='utf-8')
            assert err['position_cm']<.1 and err['rotation_deg']<.5,err
        assert len(binding.get_tracks())==1
        assert unreal.EditorAssetLibrary.save_loaded_asset(seq,False)
        unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
    assert level.save_current_level();report['passed']=True
try:run()
except Exception as ex:report['error']=repr(ex);raise
finally:
    OUT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
