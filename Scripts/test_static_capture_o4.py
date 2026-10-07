"""Real Editor + TCP integration. Only create transient/dedicated O4 test assets."""
import json,time,sys,traceback,os,uuid
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());RUN_ID=os.environ.get('O4_TEST_RUN','o4_'+uuid.uuid4().hex[:8]);REPORT=ROOT/'reports/o4'/RUN_ID/'editor.json';REPORT.parent.mkdir(parents=True,exist_ok=True);CONTROL=REPORT.parent/'source';CONTROL.mkdir(exist_ok=True)
report={'passed':False,'scope':'real UE Editor, simulated PDS1 source; no physical hardware','steps':[],'latencies_ms':[]}
import subprocess,os,uuid
class ExternalServer:
    def __init__(self):
        self.proc=subprocess.Popen([os.environ['POSEDOLL_TEST_PYTHON'],'-X','utf8',str(ROOT/'Scripts/o4_simulator_driver.py'),'--root',str(ROOT),'--control-dir',str(CONTROL)],creationflags=0x08000000)
        deadline=time.monotonic()+5
        while not (CONTROL/'o4_source_ready').exists():
            if time.monotonic()>deadline:raise RuntimeError('External simulated source did not start')
            time.sleep(.01)
    def control(self,**data):
        token=str(uuid.uuid4());data['id']=token;p=CONTROL/('command_'+token+'.json');p.write_text(json.dumps(data),encoding='utf-8')
        if data.get('stop'):return
        deadline=time.monotonic()+3
        while not (CONTROL/(token+'.ack')).exists():
            if time.monotonic()>deadline:raise RuntimeError('Source control not acknowledged: '+str(data))
            time.sleep(.01)
    @property
    def static_fault(self):return ''
    @static_fault.setter
    def static_fault(self,value):self.control(fault=value)
    def close(self):self.control(stop=True);self.proc.wait(timeout=5)
server=ExternalServer()

def cmd(action,arg='',expect=True):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg))
    if expect is not None:assert r['ok']==expect,(action,r)
    return r

def pump_until(predicate,seconds=5):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        cmd('tick');s=cmd('status')
        if predicate(s):return s
        time.sleep(.01)
    raise AssertionError(('wait timed out',cmd('status')))

def request(write=False):
    begin=time.monotonic();r=cmd('snapshot_capture' if write else 'snapshot');cid=r['capture_id']
    same=cmd('snapshot_capture' if write else 'snapshot');assert same['capture_id']==cid
    s=pump_until(lambda x:x['snapshot_state'] in ('SnapshotReady','Committed','Fault','TimedOut','Cancelled'))
    report['steps'].append({'request':cid,'result':s});return s,(time.monotonic()-begin)*1000

def run():
    # New transient world and a new sequence in this isolated project only.
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert level.new_level('/Game/PoseDollO4/'+RUN_ID+'/StaticCaptureTest')
    actor=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(0,0,0),unreal.Rotator())
    actor.set_actor_label('O4 Static Test Manny');actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
    sequence=unreal.AssetToolsHelpers.get_asset_tools().create_asset('O4_StaticPoses','/Game/PoseDollO4/'+RUN_ID,unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
    sequence.set_display_rate(unreal.FrameRate(24,1));sequence.set_playback_start(0);sequence.set_playback_end(120);binding=sequence.add_possessable(actor)
    assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
    assert unreal.PoseDollEditorLibrary.bind_target(sequence,actor.skeletal_mesh_component)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(12)
    cmd('connect','static');pump_until(lambda s:s.get('static_source') and s['state']=='Ready')
    cmd('resume',expect=False)
    s,ms=request();assert s['snapshot_state']=='SnapshotReady',s;report['latencies_ms'].append(ms)
    report['expected_bones']={'12':cmd('pose_report',expect=None)['bones']}
    snap_time=s['snapshot_captured_utc'];scan_id=s['snapshot_scan_id'];assert s['snapshot_stable_us']>=500000
    time.sleep(1.1);cmd('tick');s=cmd('status');assert s['snapshot_state']=='SnapshotReady' and s['snapshot_captured_utc']==snap_time and s['snapshot_scan_id']==scan_id
    assert s['captures']==0
    cmd('capture',json.dumps({'frame':12,'advance':4}));assert cmd('status')['captures']==1
    assert unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()==16
    cmd('capture',json.dumps({'frame':12}),expect=False)
    keys=cmd('key_report',expect=None);report['first_keys']=keys;assert any(keys['control_keys'].values())
    # Undo is one operation and restores the prior animation, without reapplying a reply.
    cmd('undo');report['undo_keys']=cmd('key_report',expect=None);cmd('redo')
    assert unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()==16
    # Real asymmetric pose through existing calibration, FK and authored Rig path.
    server.control(fixture='asymmetric_pose.sample.json')
    for frame in (24,28,32):
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(frame)
        s,ms=request(True);assert s['snapshot_state']=='Committed',s;report['latencies_ms'].append(ms)
        assert unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()==frame
        report['expected_bones'][str(frame)]=cmd('pose_report',expect=None)['bones']
    # Partial masks preserve untouched limb keys; static Clutch uses locked baselines.
    before=cmd('key_report',expect=None)['control_keys']
    cmd('mask','UpperBody');unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(36)
    s,ms=request(True);assert s['snapshot_state']=='Committed',s
    after=cmd('key_report',expect=None)['control_keys']
    for name,number in before.items():
        if name.startswith(('thigh_','calf_','foot_','ball_','leg_')):assert after[name]==number,(name,number,after[name])
    cmd('mask','FullBody');unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(38)
    cmd('snapshot_clutch');s=pump_until(lambda x:x['snapshot_state']=='SnapshotReady')
    cmd('capture',json.dumps({'frame':38}))
    report['mask_preserved_lower_body']=True;report['static_clutch_committed']=True
    count=cmd('status')['captures']
    # Changes during request invalidate it before any write.
    for change in ('time','mask','undo','target'):
        unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(40)
        cmd('snapshot_capture')
        if change=='time':unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(41)
        elif change=='mask':cmd('mask','UpperBody')
        elif change=='undo':cmd('undo')
        elif change=='target':assert unreal.PoseDollEditorLibrary.bind_target(sequence,actor.skeletal_mesh_component)
        cmd('tick');s=cmd('status');assert s['captures']==count and s['snapshot_state']=='Cancelled',(change,s)
        cmd('mask','FullBody')
    held_stamp=cmd('status')['snapshot_captured_utc']
    for fault in ('wrong_request','node_restart','satellite_restart','missing','fault','crc','profile_change','duplicate_scan','timeout','disconnect'):
        server.static_fault=fault
        if not cmd('status').get('static_source'):cmd('connect','static')
        pump_until(lambda s:s.get('static_source') and s['state']=='Ready') if cmd('status')['state'] in ('Disconnected','Connecting') else None
        cmd('snapshot_capture')
        s=pump_until(lambda x:x['snapshot_state'] in ('Fault','Cancelled','TimedOut'),4)
        assert s['captures']==count and s['snapshot_captured_utc']==held_stamp,(fault,s)
        report['steps'].append({'fault':fault,'result':s})
        server.static_fault='';cmd('disconnect');cmd('connect','static');pump_until(lambda x:x.get('static_source') and x['state']=='Ready')
    # A cancelled request must not overwrite the last accepted snapshot timestamp.
    assert cmd('status')['has_snapshot']
    report['final']=cmd('status');report['final_keys']=cmd('key_report',expect=None)
    # Save and reopen the dedicated sequence; no socket is needed for playback.
    assert level.save_current_level()
    assert unreal.EditorAssetLibrary.save_loaded_asset(sequence,False)
    unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence();assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
    unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(24)
    report['persisted_tracks']=len(binding.get_tracks());assert report['persisted_tracks']==1
    report['passed']=True
try:run()
except Exception:
    report['exception']=traceback.format_exc();unreal.log_error(report['exception'])
finally:
    cmd('disconnect',expect=None);server.close();unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence();REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_O4_STATIC_'+('PASS' if report['passed'] else 'FAIL'))
    unreal.SystemLibrary.quit_editor()
