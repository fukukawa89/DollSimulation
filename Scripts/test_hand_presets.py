"""Native Unreal integration: replacement, hand isolation, undo and persistence."""
import json,math,traceback,uuid
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()); report={'passed':False,'cases':[]}
key=lambda n:unreal.RigElementKey(name=n,type=unreal.RigElementType.BONE)
def cmd(action,arg='',ok=True):
    result=json.loads(unreal.PoseDollEditorLibrary.session_command(action,json.dumps(arg) if isinstance(arg,dict) else arg))
    if ok is not None:assert result.get('ok')==ok,(action,result)
    return result
def apply(pid,side='both'):return cmd('hand_apply',{'preset':pid,'side':side})
def current_rig():return unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
def bones(local=False):
    rig=current_rig();assert rig.execute('Forwards Solve');h=rig.get_hierarchy()
    return {str(k.name):(h.get_local_transform(k) if local else h.get_global_transform(k)) for k in h.get_all_keys() if k.type==unreal.RigElementType.BONE}
def hand(n,side):return n.startswith(('thumb_','index_','middle_','ring_','pinky_')) and n.endswith('_'+side)
def compare(a,b,keep=lambda n:True):
    errors={}
    for n,t in a.items():
        if not keep(n):continue
        d=(t.translation-b[n].translation).length();degrees=angle(t.rotation,b[n].rotation)
        if d>.02 or degrees>.06:errors[n]=[d,degrees]
    assert not errors,errors
def channels():
    tr=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].track
    return {str(c.channel_name):[(k.get_time().frame_number.value,k.get_time().sub_frame,k.get_value()) for k in c.get_keys()] for c in tr.get_sections()[0].get_all_channels()}
def serialize(pose):return {n:{'p':[t.translation.x,t.translation.y,t.translation.z],'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]} for n,t in pose.items()}
def angle(a,b):
    av=[a.x,a.y,a.z,a.w];bv=[b.x,b.y,b.z,b.w]
    dot=abs(sum(x*y for x,y in zip(av,bv)))/math.sqrt(sum(x*x for x in av)*sum(x*x for x in bv))
    return math.degrees(2*math.acos(min(1.,dot)))
try:
    folder='/Game/PoseDollCaptureTests/hand_'+uuid.uuid4().hex[:8]
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert level.new_level(folder+'/TestMap')
    actor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(0,0,0))
    actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
    seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('HandPoses',folder,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_start(0);seq.set_playback_end(100);seq.add_possessable(actor)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component),cmd('status')
    data=json.loads((ROOT/'Plugins/PoseDoll/Resources/HandPresets/presets.json').read_text(encoding='utf-8-sig'))['presets'];a,b='hand-001','hand-002'
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10)
    apply(a);report['cases'].append('creates compatible rig track without hardware source')
    for side in ('left','right'):
        before=bones();apply(b,side);after=bones();short='l' if side=='left' else 'r'
        compare(before,after,lambda n:not hand(n,short))
        report['cases'].append(side+' preserves wrist, body and opposite fingers')
    apply(a);expected=bones();apply(b);apply(a);compare(expected,bones());apply(a);compare(expected,bones())
    report['cases'].append('A-B-A and repeated A are absolute and idempotent')
    rig=current_rig()
    for side in ('l','r'):
        for name in ('index_01_','pinky_metacarpal_'):
            e=unreal.EulerTransform(location=unreal.Vector(.2,.1,0),rotation=unreal.Rotator(12,5,7),scale=unreal.Vector(1.02,1.02,1.02))
            unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,name+side+'_ctrl',unreal.FrameNumber(10),e,set_key=True)
    apply(a);compare(expected,bones());report['cases'].append('replaces pre-existing finger joints, metacarpal, translation and scale')
    # Same E/B/C classification must preserve visibly distinct shapes and replace absolutely.
    apply('hand-062');straight_pose=bones();apply(a);natural_pose=bones()
    assert angle(straight_pose['index_03_l'].rotation,natural_pose['index_03_l'].rotation)>20
    apply('hand-062');compare(straight_pose,bones());apply(a);compare(natural_pose,bones())
    report['cases'].append('natural/straight variants share classification and replace without accumulation')
    maximum_straight_error=0.0
    def finger_directions(pose,f,side):
        p1,p2,p3=[pose[f'{f}_{j:02}_{side}'] for j in (1,2,3)]
        d1=p2.translation-p1.translation;d2=p3.translation-p2.translation
        q=p3.rotation;x,y,z,w=q.x,q.y,q.z,q.w;sign=1 if side=='l' else -1
        d3=unreal.Vector(1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y))*sign
        return d1,d2,d3
    def vector_angle(a,b):
        dot=a.x*b.x+a.y*b.y+a.z*b.z
        return math.degrees(math.acos(max(-1,min(1,dot/a.length()/b.length()))))
    # Every shipped value must be writable and read back in the actual rig.
    for p in data:
        apply(p['id']);rig=current_rig()
        for values in (p['left'],p['right']):
            for n,v in values.items():
                t=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,n,unreal.FrameNumber(10))
                assert (t.location-unreal.Vector(*v[:3])).length()<.001,n
                assert angle(t.rotation.quaternion(),unreal.Rotator(pitch=v[4],yaw=v[5],roll=v[3]).quaternion())<.01,n
                assert (t.scale-unreal.Vector(*v[6:])).length()<.001,n
        pose=bones()
        for side in ('l','r'):
            for f in p.get('authoring',{}).get('straight_fingers',[]):
                d1,d2,d3=finger_directions(pose,f,side)
                error=max(vector_angle(d1,d2),vector_angle(d2,d3));maximum_straight_error=max(maximum_straight_error,error)
                assert error<.08,(p['id'],side,f,error)
            bend=p.get('authoring',{}).get('bend')
            if bend:
                for f,state in zip(('thumb','index','middle','ring','pinky'),p['states']):
                    if f=='thumb' or state!='B':continue
                    d1,d2,d3=finger_directions(pose,f,side)
                    expected=(0,0) if bend=='base' else (75,45)
                    assert abs(vector_angle(d1,d2)-expected[0])<.1,(p['id'],f,'proximal bend')
                    assert abs(vector_angle(d2,d3)-expected[1])<.1,(p['id'],f,'distal bend')
    report['max_straight_joint_error_degrees']=maximum_straight_error
    report['cases'].append(f'all {len(data)} presets read back all 38 hand controls')
    report['cases'].append('straight variants are collinear on both hands; base-fold and hook bends retain their intended distribution')
    before=bones();old=channels();apply(a);after=bones();new=channels()
    cmd('undo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence();unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10);compare(before,bones());assert channels()==old
    cmd('redo');unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence();unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10);compare(after,bones());assert channels()==new
    report['cases'].append('one undo/redo restores all keys and evaluated bones')
    apply(b);before=bones(local=True);cmd('mask','FullBody');cmd('fixture','asymmetric_pose.sample.json');cmd('capture_current')
    compare(before,bones(local=True),lambda n:hand(n,'l') or hand(n,'r'))
    report['cases'].append('body capture after a hand preset preserves finger local poses')
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(60);apply(b);future=channels()
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(30);apply(a);now=channels()
    for n,values in future.items():
        assert [v for v in values if v[0]>=60]==[v for v in now[n] if v[0]>=60],n
    report['cases'].append('existing future key values are preserved')
    old=channels();cmd('hand_apply',{'preset':'missing','side':'left'},ok=False);assert channels()==old
    seq.set_read_only(True);cmd('hand_apply',{'preset':a,'side':'left'},ok=False);seq.set_read_only(False);assert channels()==old
    report['cases'].append('unknown preset and read-only sequence reject without key changes')
    apply('hand-062');final=bones();unreal.EditorAssetLibrary.save_loaded_asset(seq,only_if_is_dirty=False);assert level.save_current_level()
    report.update(passed=True,sequence=seq.get_path_name(),map=folder+'/TestMap',preset_count=len(data),expected=serialize(final),keys=channels())
except Exception:report['exception']=traceback.format_exc()
(ROOT/'reports/hand_presets.json').write_text(json.dumps(report,indent=2),encoding='utf-8');unreal.log('HAND_PRESETS_TEST_FINISHED '+str(report['passed']))

# Explicitly release the editor Sequencer before process teardown.
unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
unreal.SystemLibrary.quit_editor()
