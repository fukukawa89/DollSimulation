"""Real UE + simulated PDS1/stream: one-shot capture, editing and cancellation.
Creates a unique test map/sequence; never requires a physical doll.
"""
import json, math, os, subprocess, time, traceback, uuid
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
RUN='run_'+uuid.uuid4().hex[:8]
CONTROL=ROOT/'reports/pose_editing_static_source'/RUN
CONTROL.mkdir(parents=True)
report={'passed':False,'physical_hardware':False,'cases':[]}
proc=None

def cmd(action,arg='',ok=True):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,json.dumps(arg) if isinstance(arg,dict) else arg))
    if ok is not None:assert r['ok']==ok,(action,r)
    return r

def pump(predicate,seconds=5):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        cmd('tick');s=cmd('status')
        if predicate(s):return s
        time.sleep(.01)
    raise AssertionError(cmd('status'))

def source(**data):
    token=uuid.uuid4().hex;data['id']=token
    (CONTROL/('command_'+token+'.json')).write_text(json.dumps(data),encoding='utf-8')
    if data.get('stop'):return
    until=time.monotonic()+3
    while not (CONTROL/(token+'.ack')).exists():
        assert time.monotonic()<until
        time.sleep(.01)

def channels():
    return {str(c.channel_name):[(k.get_time().frame_number.value,k.get_time().sub_frame,k.get_value()) for k in c.get_keys()] for c in track.get_sections()[0].get_all_channels()}

try:
    python=os.environ.get('POSEDOLL_TEST_PYTHON',str(ROOT/'Tools/PoseDollSimulator/.venv/Scripts/python.exe'))
    proc=subprocess.Popen([python,'-X','utf8',str(ROOT/'Scripts/o4_simulator_driver.py'),'--root',str(ROOT),'--control-dir',str(CONTROL)],creationflags=0x08000000)
    end=time.monotonic()+5
    while not (CONTROL/'o4_source_ready').exists():
        assert time.monotonic()<end
        time.sleep(.01)
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert level.new_level('/Game/PoseDollCaptureTests/'+RUN+'/StaticMap')
    actor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector())
    actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
    seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('StaticPoses','/Game/PoseDollCaptureTests/'+RUN,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(24000,1001));seq.set_playback_start(0);seq.set_playback_end(100)
    bind=seq.add_possessable(actor)
    cls=unreal.load_asset('/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body').generated_class()
    track=unreal.ControlRigSequencerLibrary.find_or_create_control_rig_track(actor.get_world(),seq,cls,bind,False)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(12)
    cmd('connect','static');pump(lambda s:s.get('static_source') and s['state']=='Ready')
    initial=channels()
    source(fixture='asymmetric_pose.sample.json')
    # Moving a source without clicking must never author animation.
    for _ in range(5):cmd('tick');time.sleep(.02)
    assert channels()==initial
    before=cmd('status')
    assert before['capture_mode']=='OneShot' and 'live' not in before
    assert before['applied']==0
    for action in ('resume','freeze','snapshot_clutch','contact','placement'):
        rejected=cmd(action,ok=False)
        assert rejected['error']=='Unknown session action',(action,rejected)
    assert cmd('status')['applied']==0 and channels()==initial
    report['cases'].append('retired continuous/Clutch/contact/placement commands cannot apply or key a pose')
    cid=cmd('capture_current',{'advance':4})['capture_id']
    assert cmd('capture_current',{'advance':4})['capture_id']==cid
    accepted=pump(lambda s:s['snapshot_state'] in ('Committed','Fault','Cancelled','TimedOut'))
    assert accepted['snapshot_state']=='Committed',accepted
    assert accepted['captures']==1
    assert unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()==16
    report['cases'].append('one click commits once and advances at 24000/1001 fps; duplicate click idempotent')
    keys=channels()
    applied=cmd('status')['applied']
    held_pose=cmd('pose_report',ok=None)
    source(fixture='neutral.sample.json')
    for _ in range(20):cmd('tick');time.sleep(.01)
    assert channels()==keys
    assert cmd('status')['applied']==applied
    assert cmd('pose_report',ok=None)==held_pose
    report['cases'].append('source changes and idle ticks do not overwrite keyed pose')
    cmd('custom_parts',{'parts':['hand_l']})
    cmd('capture_current');accepted=pump(lambda s:s['snapshot_state'] in ('Committed','Fault','Cancelled','TimedOut'))
    assert accepted['snapshot_state']=='Committed',accepted
    after=channels()
    assert all(after[n]==v for n,v in keys.items() if not n.startswith('hand_l_fk_ctrl'))
    report['cases'].append('static wrist-only capture changes only selected FK rotation channels')
    count=cmd('status')['captures']
    for change in ('manual_edit','time','mask','cancel','target','disconnect'):
        cmd('capture_current')
        if change=='manual_edit':
            rig=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)[0].control_rig
            frame=unreal.FrameNumber(16)
            v=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,'hand_r_fk_ctrl',frame)
            v.rotation=unreal.Rotator(v.rotation.pitch,v.rotation.yaw+17,v.rotation.roll)
            with unreal.ScopedEditorTransaction('Manual edit while source scans'):
                unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,'hand_r_fk_ctrl',frame,v,set_key=True)
        elif change=='time':unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(17)
        elif change=='mask':cmd('custom_parts',{'parts':['hand_r']})
        elif change=='cancel':cmd('snapshot_cancel')
        elif change=='target':assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
        elif change=='disconnect':cmd('disconnect')
        expected=channels()
        for _ in range(80):cmd('tick');time.sleep(.01)
        assert cmd('status')['captures']==count,(change,cmd('status'))
        assert channels()==expected,change
        assert cmd('status')['snapshot_state']=='Cancelled',(change,cmd('status'))
        report['cases'].append('pending request cancelled by '+change)
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(16)
        if change=='disconnect':cmd('connect','static');pump(lambda s:s.get('static_source') and s['state']=='Ready')
    for fault in ('missing','crc','timeout'):
        source(fault=fault);before=channels()
        cmd('capture_current')
        result=pump(lambda s:s['snapshot_state'] in ('Fault','TimedOut','Cancelled'))
        assert result['captures']==count and channels()==before,(fault,result)
        report['cases'].append('invalid sample rejected: '+fault)
        source(fault='');cmd('disconnect');cmd('connect','static');pump(lambda s:s.get('static_source') and s['state']=='Ready')
    # The simulator can still send continuously; the session must only buffer it.
    cmd('disconnect');source(stop=True);proc.wait(timeout=5);proc=None
    CONTROL=ROOT/'reports/pose_editing_static_source'/(RUN+'_stream')
    CONTROL.mkdir(parents=True)
    proc=subprocess.Popen([python,'-X','utf8',str(ROOT/'Scripts/o4_simulator_driver.py'),'--root',str(ROOT),'--control-dir',str(CONTROL),'--stream'],creationflags=0x08000000)
    end=time.monotonic()+5
    while not (CONTROL/'o4_source_ready').exists():
        assert time.monotonic()<end
        time.sleep(.01)
    cmd('mask','FullBody')
    source(fixture='asymmetric_pose.sample.json')
    before=cmd('status');held_keys=channels();held_pose=cmd('pose_report',ok=None)
    cmd('connect');ready=pump(lambda s:s['state']=='Ready' and s.get('received',0)>=10)
    assert not ready['static_source']
    assert ready['applied']==before['applied'] and ready['captures']==before['captures']
    assert channels()==held_keys and cmd('pose_report',ok=None)==held_pose
    report['cases'].append('continuous simulator input only buffers until explicit capture')
    first=cmd('capture_current')
    assert first['applied']==before['applied']+1 and first['captures']==before['captures']+1
    held_keys=channels();held_pose=cmd('pose_report',ok=None)
    source(fixture='neutral.sample.json')
    later=pump(lambda s:s.get('received',0)>=first['received']+20)
    assert later['applied']==first['applied'] and later['captures']==first['captures']
    assert channels()==held_keys and cmd('pose_report',ok=None)==held_pose
    report['cases'].append('new streaming samples cannot overwrite the last captured pose')
    second=cmd('capture_current')
    assert second['applied']==first['applied']+1 and second['captures']==first['captures']+1
    assert channels()!=held_keys and cmd('pose_report',ok=None)!=held_pose
    report['cases'].append('each explicit click commits the latest buffered sample once')
    report['passed']=True
except Exception:
    report['exception']=traceback.format_exc();unreal.log_error(report['exception'])
finally:
    cmd('disconnect',ok=None)
    if proc is not None:
        source(stop=True)
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.terminate()
    (ROOT/'reports/pose_editing_static.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_EDITING_STATIC_'+('PASS' if report['passed'] else 'FAIL'))
    unreal.SystemLibrary.quit_editor()
