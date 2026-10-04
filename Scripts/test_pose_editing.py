"""Real UE integration: one-shot capture, native IK/FK edits, masks and persistence.
Creates assets only under /Game/PoseDollCaptureTests/<unique run>.
"""
import json, math, traceback, uuid
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
REPORT=ROOT/'reports/pose_editing.json'
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
    cmd('mask','FullBody');cmd('fixture','asymmetric_pose.sample.json')
    sourcepose=cmd('pose_report',ok=None)['bones']
    cmd('capture_current')
    h=rig.get_hierarchy()
    for name in bones(rig):
        parent=str(h.get_first_parent(keys(name)).name)
        q=unreal.Quat(*sourcepose[name]['rotation_xyzw'])
        if parent in sourcepose:
            pq=sourcepose[parent]['rotation_xyzw']
            q=unreal.Quat(-pq[0],-pq[1],-pq[2],pq[3])*q
        actual=h.get_local_transform(keys(name)).rotation
        assert math.degrees(actual.angular_distance(q))<.5,name
    report['cases'].append('captured joint rotations equal the source FK joint rotations')
    report['cases'].append('full-body capture')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(0)
    compare(initial,bones(rig))
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10)
    report['cases'].append('first capture preserves the earlier evaluated pose')
    assert str(track.get_display_name())=='Artist Body Rig'
    before=bones(rig)
    cmd('tick')
    with unreal.ScopedEditorTransaction('Test native IK switch'):
        mode(seq,rig,'arm_l_fk_ik_switch',True)
    after=bones(rig)
    report['native_switch_error']=cmd('status')['error']
    compare(before,after,exclude=('upperarm_l','lowerarm_l'))
    report['ik_representation_note']=cmd('status')['editing_note']
    assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',unreal.FrameNumber(10))
    report['cases'].append('native switch to IK matches latest FK')
    mode(seq,rig,'arm_r_fk_ik_switch',True)
    transform(seq,rig,'hand_l_ik_ctrl',dx=2)
    before=bones(rig)
    assert (before['hand_l'].translation-after['hand_l'].translation).length()>.5
    originalkeys=cmd('key_report',ok=None)['control_keys']
    cmd('custom_parts',{'parts':['hand_l']})
    cmd('fixture','neutral.sample.json')
    cmd('capture_current')
    after=bones(rig)
    compare(before,after,exclude=('hand_l',))
    assert not unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',unreal.FrameNumber(10))
    assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_r_fk_ik_switch',unreal.FrameNumber(10))
    newkeys=cmd('key_report',ok=None)['control_keys']
    for n,count in originalkeys.items():
        if n.endswith('_r_fk_ctrl') or n.startswith(('leg_','thigh_','calf_','foot_','ball_')):
            assert newkeys[n]==count,(n,count,newkeys[n])
    report['cases'].append('wrist-only IK capture preserves shoulder elbow and other chains')
    transform(seq,rig,'hand_l_fk_ctrl',yaw=21)
    before=bones(rig)
    cmd('tick')
    with unreal.ScopedEditorTransaction('Test rebuild IK after capture'):
        mode(seq,rig,'arm_l_fk_ik_switch',True)
    compare(before,bones(rig))
    assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',unreal.FrameNumber(10))
    report['cases'].append('IK rebuilt after manual FK wrist edit, no stale goal restored')
    # No source or panel is needed for normal editing.
    cmd('disconnect');transform(seq,rig,'hand_l_ik_ctrl',dx=1)
    before=bones(rig);beforekeys=cmd('key_report',ok=None)
    for _ in range(8):cmd('tick')
    compare(before,bones(rig));assert cmd('key_report',ok=None)==beforekeys
    report['cases'].append('idle ticks do not change animation')
    # Different source shoulder orientations with the same wrist joint angle must
    # produce the same wrist-local rotation.
    cmd('custom_parts',{'parts':['hand_l']})
    cmd('fixture','neutral.sample.json');cmd('capture_current')
    q1=rig.get_hierarchy().get_local_transform(keys('hand_l')).rotation
    cmd('fixture','left_shoulder_forward_90.sample.json');cmd('capture_current')
    q2=rig.get_hierarchy().get_local_transform(keys('hand_l')).rotation
    assert math.degrees(q1.angular_distance(q2))<.5
    report['cases'].append('wrist capture uses parent-relative rotation')
    cmd('custom_parts',{'parts':[]});cmd('capture_current',ok=False)
    report['cases'].append('empty selection rejected')
    # Presets exist, only select the intended control groups.
    for preset in ('FullBody','UpperBody','LowerBody','arm_l','arm_r','leg_l','leg_r'):
        cmd('mask',preset)
    cmd('custom_parts',{'parts':['head','hand_r']})
    cmd('fixture','neutral.sample.json');cmd('capture_current')
    # Existing keys at other frames and complete undo/redo of a capture.
    def channel_state():
        return {str(c.channel_name):[(k.get_time().frame_number.value,k.get_time().sub_frame,k.get_value()) for k in c.get_keys()] for c in track.get_sections()[0].get_all_channels()}
    placement=binding.add_track(unreal.MovieScene3DTransformTrack)
    placement.set_display_name('PoseDoll / Placement')
    placement_section=placement.add_section();placement_section.set_range(0,100)
    for i,c in enumerate(placement_section.get_all_channels()):
        c.add_key(unreal.FrameNumber(0),(100,35,0,0,20,0,1,1,1)[i])
        c.add_key(unreal.FrameNumber(30),(170,35,0,0,20,0,1,1,1)[i])
    placementkeys=[[(k.get_time().frame_number.value,k.get_value()) for k in c.get_keys()] for c in placement_section.get_all_channels()]
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(70)
    cmd('mask','FullBody');cmd('fixture','neutral.sample.json');cmd('capture_current')
    future=bones(rig)
    assert abs(actor.get_actor_location().x-170)<.1
    assert [[(k.get_time().frame_number.value,k.get_value()) for k in c.get_keys()] for c in placement_section.get_all_channels()]==placementkeys
    report['cases'].append('existing actor placement animation and keys remain unchanged')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    before=bones(rig);beforekeys=channel_state()
    cmd('custom_parts',{'parts':['hand_l']});cmd('fixture','asymmetric_pose.sample.json');cmd('capture_current')
    after=bones(rig);afterkeys=channel_state()
    compare(before,after,exclude=('hand_l',))
    cmd('undo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    assert channel_state()==beforekeys
    compare(before,bones(rig))
    cmd('redo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    assert channel_state()==afterkeys
    compare(after,bones(rig))
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(70)
    compare(future,bones(rig))
    report['cases'].append('one Undo/Redo restores capture keys and visible pose; future key unchanged')
    # Exercise each native mode and each preset in the actual rig, including torso.
    mode_parts=[('arm_l','hand_l'),('arm_r','hand_r'),('leg_l','foot_l'),('leg_r','foot_r'),('spine','chest'),('neck','head')]
    for index,(group,part) in enumerate(mode_parts):
        frame=40+index
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        cmd('mask','FullBody');cmd('fixture','neutral.sample.json');cmd('capture_current')
        rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
        cmd('tick')
        before=bones(rig);beforekeys=channel_state()
        with unreal.ScopedEditorTransaction('Native IK switch '+group):mode(seq,rig,group+'_fk_ik_switch',True)
        assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,group+'_fk_ik_switch',unreal.FrameNumber(frame)),cmd('status')
        after=bones(rig);afterkeys=channel_state()
        if group=='arm_l':
            cmd('undo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
            unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
            assert channel_state()==beforekeys
            compare(before,bones(rig))
            cmd('redo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
            unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
            assert channel_state()==afterkeys
            compare(after,bones(rig))
            report['cases'].append('one Undo/Redo restores native mode switch and matching keys')
        cmd('custom_parts',{'parts':[part]});cmd('fixture','asymmetric_pose.sample.json');cmd('capture_current')
        rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
        assert not unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,group+'_fk_ik_switch',unreal.FrameNumber(frame))
        report['cases'].append(group+' IK capture converts the affected chain to FK')
    for index,preset in enumerate(('FullBody','UpperBody','LowerBody','arm_l','arm_r','leg_l','leg_r')):
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(55+index)
        cmd('mask',preset);cmd('fixture','neutral.sample.json');cmd('capture_current')
        report['cases'].append('capture preset '+preset)
    # Capture then switch immediately, with no intervening evaluation/tick.
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(65)
    cmd('mask','FullBody');cmd('fixture','neutral.sample.json');cmd('capture_current')
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    cmd('tick');mode(seq,rig,'arm_l_fk_ik_switch',True)
    cmd('custom_parts',{'parts':['hand_l']});cmd('fixture','asymmetric_pose.sample.json');cmd('capture_current')
    mode(seq,rig,'arm_l_fk_ik_switch',True)
    assert unreal.ControlRigSequencerLibrary.get_local_control_rig_bool(seq,rig,'arm_l_fk_ik_switch',unreal.FrameNumber(65))
    assert not cmd('status')['error']
    report['cases'].append('native switch immediately after capture uses the new FK mode')
    # Refuse nested capture before touching caller-owned transaction data.
    beforekeys=channel_state()
    with unreal.ScopedEditorTransaction('Caller-owned edit'):
        cmd('capture_current',ok=False)
    assert channel_state()==beforekeys
    report['cases'].append('capture cannot undo a caller-owned transaction on failure')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
    report['expected_frames']={}
    savedkeys=channel_state()
    for frame in (0,10,30,40,45,55,61,70):
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        report['expected_frames'][str(frame)]=serialize(bones(rig))
        cmd('tick')
    assert channel_state()==savedkeys
    report['cases'].append('scrubbing and idle evaluation do not author keys')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30)
    assert level.save_current_level()
    assert unreal.EditorAssetLibrary.save_loaded_asset(seq,False)
    report['map']=folder+'/TestMap';report['sequence']=seq.get_path_name()
    report['expected']=serialize(bones(rig))
    # A failed bind must not allow Capture to create a second, incompatible rig.
    foreign=unreal.AssetToolsHelpers.get_asset_tools().create_asset('ForeignRig',folder,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    foreign.set_display_rate(unreal.FrameRate(24,1));foreign.set_playback_end(100)
    foreign_binding=foreign.add_possessable(actor)
    foreign_track=unreal.ControlRigSequencerLibrary.find_or_create_control_rig_track(actor.get_world(),foreign,unreal.FKControlRig,foreign_binding,False)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(foreign)
    assert not unreal.PoseDollEditorLibrary.bind_target(foreign,actor.skeletal_mesh_component)
    cmd('fixture','neutral.sample.json');cmd('capture_current',ok=False)
    assert len(foreign_binding.get_tracks())==1
    report['cases'].append('incompatible existing Rig is rejected without adding a competing track')
    report['passed']=True
except Exception:
    report['exception']=traceback.format_exc()
    unreal.log_error(report['exception'])
finally:
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_EDITING_'+('PASS' if report['passed'] else 'FAIL'))
    unreal.SystemLibrary.quit_editor()
