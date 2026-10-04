"""Real UE integration: one-shot capture, native IK/FK edits, masks and persistence.
Creates assets only under /Game/PoseDollCaptureTests/<unique run>.
"""
import json, math, traceback, uuid
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
REPORT=ROOT/'reports/pose_editing_o22.json'
report={'passed':False,'cases':[]}
keys=lambda n,t=unreal.RigElementType.BONE:unreal.RigElementKey(name=n,type=t)

def cmd(action,arg='',ok=True):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,json.dumps(arg) if isinstance(arg,dict) else arg))
    if ok is not None:assert r.get('ok')==ok,(action,r)
    return r
def bones(rig):
    assert rig.execute('Forwards Solve')
    h=rig.get_hierarchy()
    names=('pelvis','spine_02','spine_03','spine_04','neck_01','neck_02','head',
           'clavicle_l','upperarm_l','lowerarm_l','hand_l','clavicle_r','upperarm_r','lowerarm_r','hand_r',
           'thigh_l','calf_l','foot_l','ball_l','thigh_r','calf_r','foot_r','ball_r')
    return {n:h.get_global_transform(keys(n)) for n in names}
def compare(a,b,exclude=()):
    errors={}
    for n in a:
        if n in exclude:continue
        d=(a[n].translation-b[n].translation).length()
        angle=math.degrees(a[n].rotation.angular_distance(b[n].rotation))
        if d>.1 or angle>.5:errors[n]=[d,angle]
    assert not errors,errors
def serialize(pose):
    return {n:{'p':[t.translation.x,t.translation.y,t.translation.z],
               'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]} for n,t in pose.items()}
def mode(seq,rig,name,value,key=True):
    unreal.ControlRigSequencerLibrary.set_local_control_rig_bool(seq,rig,name,unreal.FrameNumber(unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()),value,set_key=key)
def transform(seq,rig,name,dx=0,pitch=0,yaw=0,roll=0):
    frame=unreal.FrameNumber(unreal.LevelSequenceEditorBlueprintLibrary.get_current_time())
    t=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,name,frame)
    t.location=unreal.Vector(t.location.x+dx,t.location.y,t.location.z)
    t.rotation=unreal.Rotator(t.rotation.pitch+pitch,t.rotation.yaw+yaw,t.rotation.roll+roll)
    unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,name,frame,t,set_key=True)
    return t

try:
    run='run_'+uuid.uuid4().hex[:8]
    folder='/Game/PoseDollCaptureTests/'+run
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.new_level(folder+'/TestMap')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(100,35,0),unreal.Rotator(0,20,0))
    actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
    seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('Poses',folder,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_start(0);seq.set_playback_end(100)
    binding=seq.add_possessable(actor)
    rigclass=unreal.load_asset('/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body').generated_class()
    track=unreal.ControlRigSequencerLibrary.find_or_create_control_rig_track(actor.get_world(),seq,rigclass,binding,False)
    track.set_display_name('Artist Body Rig')
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component),cmd('status')
    report['cases'].append('bind existing artist Control Rig without renaming')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0)
    initial=bones(rig)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10)
    cmd('mask','FullBody');cmd('fixture','asymmetric_pose.sample.json');cmd('capture_current')
    report['cases'].append('full-body capture')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0)
    compare(initial,bones(rig))
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10)
    report['cases'].append('first capture preserves the earlier evaluated pose')
    assert str(track.get_display_name())=='Artist Body Rig'
    before=bones(rig)
    payload=ROOT.parent/'PoseDollLab_UE58_Design/PoseDoll_HW44_Plan/design/Hardware/PoseDoll44/bench/revO22/ue/asymmetric.payload.json'
    profile=ROOT/'Shared/Profiles/O22/manny.json'
    assert payload.exists()
    transform(seq,rig,'body_ctrl',yaw=14)
    before=bones(rig)
    result=json.loads(unreal.PoseDollEditorLibrary.capture_measured_pose22(seq,rig,10,str(payload),str(profile),True,'Custom','hand_l'))
    assert result['ok'],result
    compare(before,bones(rig),exclude=('hand_l',))
    assert result['capture_operation']=='ReplaceLocalRotation'
    report['cases'].append('O22 wrist capture preserves UE-authored body pose')
    result=json.loads(unreal.PoseDollEditorLibrary.capture_measured_pose22(seq,rig,10,str(payload),str(profile),True,'FullBody',''))
    assert result['ok'],result
    compare({'pelvis':before['pelvis']},bones(rig))
    assert 'body_ctrl' not in result['written_controls']
    report['cases'].append('O22 full-body import preserves the unmeasured pelvis')
    invalid=json.loads(unreal.PoseDollEditorLibrary.capture_measured_pose22(seq,rig,10,str(payload),str(profile),True,'Custom','pelvis'))
    assert not invalid['ok']
    report['cases'].append('O22 rejects an unmeasured pelvis selection')
    report['passed']=True
except Exception:
    report['exception']=traceback.format_exc();unreal.log_error(report['exception'])
finally:
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_EDITING_O22_'+('PASS' if report['passed'] else 'FAIL'))
    unreal.SystemLibrary.quit_editor()
