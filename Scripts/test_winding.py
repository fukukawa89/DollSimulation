import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());out=root/'reports'/'winding.json'
report={'passed':False}
def call(a,b=''):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(a,b));assert r.get('ok',True),r;return r
try:
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/PoseDollLab/PoseDoll_Test')
    a=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny'][0]
    name='PoseDoll_Winding';seq=unreal.load_asset('/Game/PoseDollLab/'+name)
    if seq:
        for b in seq.get_bindings():b.remove()
    else:seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,'/Game/PoseDollLab',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_end(30);seq.add_possessable(a)
    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,a.skeletal_mesh_component)
    for frame,label in [(0,'a'),(4,'b')]:
        call('fixture',f'euler_boundary_{label}.sample.json');call('capture',json.dumps({'frame':frame,'linear':True}))
    keys=call('key_report');report['rotation_keys_degrees']=keys['rotation_keys_degrees']
    report['crossings']=[{'control':c,'axis':i,'keys':v} for c,axes in keys['rotation_keys_degrees'].items() for i,v in enumerate(axes) if len(v)==2 and min(abs(x) for x in v)>170 and min(abs(x) for x in v)<180<max(abs(x) for x in v) and abs(v[1]-v[0])<8]
    assert report['crossings'],report['rotation_keys_degrees']['upperarm_l_fk_ctrl']
    report['passed']=True
    assert unreal.EditorAssetLibrary.save_loaded_asset(seq,False)
    unreal.log('POSEDOLL_WINDING_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc);raise
finally:
    out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
