"""Discover a legal raw-input pair that crosses a real Manny control Euler branch."""
import json,math,runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());fixtures=root/'Shared/Fixtures'
base=json.loads((fixtures/'neutral.sample.json').read_text(encoding='utf-8'))
call=lambda a,b='':json.loads(unreal.PoseDollEditorLibrary.session_command(a,b))
try:
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/PoseDollLab/PoseDoll_Test')
    actor=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
    seq=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Winding')
    found=False
    for abduct in (130,140,150):
        for b in seq.get_bindings():b.remove()
        seq.add_possessable(actor);unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
        assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
        previous=None
        for frame,flex in enumerate(range(-30,31,2)):
            sample=json.loads(json.dumps(base))
            sample['raw_angles_rad'][14]=math.pi+math.radians(flex)
            sample['raw_angles_rad'][15]=math.pi+math.radians(abduct)
            (fixtures/'winding_probe.sample.json').write_text(json.dumps(sample),encoding='utf-8')
            assert call('fixture','winding_probe.sample.json')['ok']
            capture=call('capture',json.dumps({'frame':frame}));assert capture['ok'],capture
            rotations=call('key_report')['rotation_keys_degrees']
            hits=[(c,i,v[-2:]) for c,axes in rotations.items() for i,v in enumerate(axes) if len(v)>1 and min(abs(x) for x in v[-2:])>170 and min(abs(x) for x in v[-2:])<180<max(abs(x) for x in v[-2:]) and abs(v[-1]-v[-2])<8]
            if hits:
                (fixtures/'euler_boundary_a.sample.json').write_text(json.dumps(previous,indent=2),encoding='utf-8')
                (fixtures/'euler_boundary_b.sample.json').write_text(json.dumps(sample,indent=2),encoding='utf-8')
                (root/'reports/winding_discovery.json').write_text(json.dumps({'abduct':abduct,'flex_pair':[flex-2,flex],'crossings':hits},indent=2),encoding='utf-8')
                found=True;break
            previous=sample
        if found:break
    assert found,'No adjacent Euler boundary pair found in legal shoulder scan'
finally:
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
runpy.run_path(str(root/'Scripts/test_winding.py'),run_name='__main__')
