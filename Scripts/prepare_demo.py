"""Open the delivered demo for interactive use; no asset edits or implicit capture."""
import json,time
import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny')
sequence=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Poses')
assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
actors.set_selected_level_actors([actor])
unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0)
assert unreal.PoseDollEditorLibrary.bind_target(sequence,actor.skeletal_mesh_component)
unreal.PoseDollEditorLibrary.session_command('mask','FullBody')
unreal.PoseDollEditorLibrary.session_command('open_panel')
unreal.PoseDollEditorLibrary.session_command('fixture','neutral.sample.json')
unreal.PoseDollEditorLibrary.session_command('connect')
begin=time.monotonic()
def connected(delta):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command('status'))
    if r['state']=='Ready':
        unreal.unregister_slate_post_tick_callback(callback)
        unreal.log('POSEDOLL_INTERACTIVE_DEMO_READY: click Capture to transfer one pose')
    elif time.monotonic()-begin>15:
        unreal.unregister_slate_post_tick_callback(callback);unreal.log('POSEDOLL_DEMO_WAITING_FOR_SIMULATOR')
callback=unreal.register_slate_post_tick_callback(connected)
