import json,traceback,math
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());previous=json.loads((root/'reports/hand_presets.json').read_text());out={'passed':False}
def angle(a,b):
    av=[a.x,a.y,a.z,a.w];bv=[b.x,b.y,b.z,b.w]
    dot=abs(sum(x*y for x,y in zip(av,bv)))/math.sqrt(sum(x*x for x in av)*sum(x*x for x in bv))
    return math.degrees(2*math.acos(min(1.,dot)))
try:
    assert previous['passed']
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(previous['map'])
    seq=unreal.load_asset(previous['sequence']);assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    proxy=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0];rig=proxy.control_rig;rig.execute('Forwards Solve');h=rig.get_hierarchy()
    for n,v in previous['expected'].items():
        t=h.get_global_transform(unreal.RigElementKey(name=n,type=unreal.RigElementType.BONE))
        assert (t.translation-unreal.Vector(*v['p'])).length()<.02,n
        assert angle(t.rotation,unreal.Quat(*v['q']))<.06,n
    channels={str(c.channel_name):[[k.get_time().frame_number.value,k.get_time().sub_frame,k.get_value()] for k in c.get_keys()] for c in proxy.track.get_sections()[0].get_all_channels()}
    assert channels==previous['keys'];out['passed']=True
except Exception:out['exception']=traceback.format_exc()
(root/'reports/hand_presets_reopen.json').write_text(json.dumps(out,indent=2),encoding='utf-8')

# Explicitly release the editor Sequencer before process teardown.
unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
unreal.SystemLibrary.quit_editor()
