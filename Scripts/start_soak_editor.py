"""GUI + real Slate tick soak; no long blocking game-thread loops."""
import json,time,ctypes,runpy
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());REPORT=ROOT/'reports/soak_gui.json'
report={'passed':False,'phase':'initializing','history':[],'panel_cycles':0,'unique_sessions':[],
        'engine':unreal.SystemLibrary.get_engine_version(),'mcp_required_for_pipeline':False,
        'engine_command_line':unreal.SystemLibrary.get_command_line(),
        'rendering':'Real Editor / Slate / D3D12; background CPU throttling disabled for test',
        'source_rate_hz':60,
        'latency_scope':'UE receive -> native rig / preview CPU application; source-to-photon not measured'}
def call(action,arg=''):
    r=json.loads(unreal.PoseDollEditorLibrary.session_command(action,arg));assert r.get('ok',True),r;return r
def control(**settings):
    (ROOT/'reports/soak_control.json').write_text(json.dumps(settings),encoding='utf-8')
class MEMORY(ctypes.Structure):
    _fields_=[('cb',ctypes.c_ulong),('PageFaultCount',ctypes.c_ulong),('PeakWorkingSetSize',ctypes.c_size_t),('WorkingSetSize',ctypes.c_size_t),('QuotaPeakPagedPoolUsage',ctypes.c_size_t),('QuotaPagedPoolUsage',ctypes.c_size_t),('QuotaPeakNonPagedPoolUsage',ctypes.c_size_t),('QuotaNonPagedPoolUsage',ctypes.c_size_t),('PagefileUsage',ctypes.c_size_t),('PeakPagefileUsage',ctypes.c_size_t)]
def memory_mb():
    v=MEMORY();v.cb=ctypes.sizeof(v)
    ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(-1),ctypes.byref(v),v.cb)
    return v.WorkingSetSize/1048576

# The boundary regression is executed once before live input; it touches only its dedicated test sequence.
runpy.run_path(str(ROOT/'Scripts/test_winding.py'),run_name='__main__')
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/PoseDollLab/PoseDoll_Test')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=[a for a in actors.get_all_level_actors() if a.get_actor_label()=='PoseDoll_TestManny'][0]
sequence=unreal.load_asset('/Game/PoseDollLab/PoseDoll_Poses')
assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(sequence)
actors.set_selected_level_actors([actor])
assert unreal.PoseDollEditorLibrary.bind_target(sequence,actor.skeletal_mesh_component)
unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(8)
baseline=call('key_report');assert not baseline['dirty']
performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle=performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
old_max_fps=unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')
unreal.SystemLibrary.execute_console_command(actor.get_world(),'t.MaxFPS 60')
call('open_panel');call('fixture','neutral.sample.json');call('connect')
begin=time.monotonic();next_poll=begin;steady_begin=None;last_history=begin;panel_stage=0
phase_time=begin;fault_stage=0;fps_frames=0;fps_begin=begin;phases=[]
report['phase']='warmup'
def finish(success,error=''):
    report['passed']=success;report['error']=error;report['phase']='complete' if success else 'failed'
    report['final_status']=call('status');report['memory_final_mb']=memory_mb()
    call('freeze');control();(ROOT/'reports/soak_stop.signal').write_text('complete',encoding='utf-8')
    performance.set_editor_property('bThrottleCPUWhenNotForeground',old_throttle)
    unreal.SystemLibrary.execute_console_command(actor.get_world(),f't.MaxFPS {old_max_fps}')
    unreal.unregister_slate_post_tick_callback(callback)
    REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('POSEDOLL_GUI_SOAK_'+('SUCCESS' if success else 'FAILED')+' '+error)
def tick(delta):
    global next_poll,steady_begin,last_history,panel_stage,phase_time,fault_stage,fps_frames,fps_begin
    now=time.monotonic();fps_frames+=1
    if now<next_poll:return
    next_poll=now+.2
    try:
        if (ROOT/'reports/abort_soak.signal').exists():finish(False,'Stopped by test controller');return
        status=call('status')
        sid=status.get('session')
        if sid and sid not in report['unique_sessions']:report['unique_sessions'].append(sid)
        # Ten real close/open cycles release preview scenes and socket workers.
        if panel_stage<20 and now-phase_time>.65:
            if panel_stage%2==0:call('close_panel')
            else:call('open_panel');call('bind_selection');call('connect');report['panel_cycles']+=1
            panel_stage+=1;phase_time=now
        if panel_stage>=20 and status['state']=='Ready' and fault_stage not in (1,2,4):
            resumed=json.loads(unreal.PoseDollEditorLibrary.session_command('resume'))
            if resumed.get('ok'):status=resumed
        if panel_stage>=20 and now-begin>35 and steady_begin is None:
            if fault_stage==0:
                control(pause=True);phase_time=now;fault_stage=1;report['phase']='fault_checks'
            elif fault_stage==1 and now-phase_time>.45:
                assert status['state']=='Stale',status
                report['stale_250ms']=True;fault_stage=2
            elif fault_stage==2 and now-phase_time>1.4:
                assert status['state']=='Frozen',status
                report['freeze_1s']=True;control();phase_time=now;fault_stage=3
            elif fault_stage==3 and now-phase_time>.6:
                assert status['state']=='Frozen' and not status['live'],status
                report['no_automatic_resume']=True;call('resume');control(missing=True);phase_time=now;fault_stage=4
            elif fault_stage==4 and now-phase_time>1.4:
                assert status['state']=='Frozen' and status['invalid']>0,status
                report['missing_axis_holds_pose']=True;control(body35=True);phase_time=now;fault_stage=5
            elif fault_stage==5 and now-phase_time>1.5:
                assert status['state']=='Live',status
                report['body35_live']=True;control(fragment=True);phase_time=now;fault_stage=6
            elif fault_stage==6 and now-phase_time>2:
                assert status['state']=='Live',status
                report['fragmented_stream_live']=True;control(coalesce=True);phase_time=now;fault_stage=7
            elif fault_stage==7 and now-phase_time>2:
                assert status['state']=='Live',status
                report['coalesced_stream_live']=True;control(stale=True);phase_time=now;fault_stage=8
            elif fault_stage==8 and now-phase_time>1.4:
                assert status['state']=='Frozen' and status['rejected']>0,status
                report['stale_sequence_rejected']=True;control();phase_time=now;fault_stage=9
            elif fault_stage==9 and now-phase_time>.5:
                call('resume');phase_time=now;fault_stage=10
            elif fault_stage==10 and now-phase_time>1:
                assert status['state']=='Live',status
                fault_stage=11;steady_begin=now;last_history=now
                report['phase']='steady_30_minutes';report['memory_start_mb']=memory_mb();report['initial_status']=status
                fps_frames=0;fps_begin=now
        if steady_begin is not None:
            elapsed=now-steady_begin
            report['steady_seconds']=elapsed
            assert status['state']=='Live',status
            assert status['invalid']==report['initial_status']['invalid'],status
            keys=call('key_report')
            assert keys['control_keys']==baseline['control_keys'] and not keys['dirty'],'Preview modified the formal sequence'
            if now-last_history>=30:
                report['history'].append({'seconds':elapsed,'fps':fps_frames/(now-fps_begin),'memory_mb':memory_mb(),**status})
                fps_frames=0;fps_begin=now;last_history=now
            if elapsed>=1800:
                report['formal_keys_unchanged']=True;report['formal_asset_clean']=True
                windows=[row for row in report['history'] if row['seconds']>=90]+[status]
                report['max_observed_processing_preview_p95_ms']=max(row['main_thread_with_preview_p95_ms'] for row in windows)
                report['max_observed_receive_preview_p95_ms']=max(row['receive_to_preview_p95_ms'] for row in windows)
                report['main_thread_target_met']=report['max_observed_processing_preview_p95_ms']<2
                report['receive_to_preview_target_met']=report['max_observed_receive_preview_p95_ms']<1000/30
                report['panel_cycles_passed']=report['panel_cycles']==10
                report['session_restarts_observed']=len(report['unique_sessions'])>=10
                required=('main_thread_target_met','receive_to_preview_target_met','panel_cycles_passed','session_restarts_observed')
                failed=[name for name in required if not report[name]]
                finish(not failed,', '.join(failed));return
        report['latest_status']=status;REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    except Exception as exc:
        finish(False,repr(exc))
callback=unreal.register_slate_post_tick_callback(tick)
REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('POSEDOLL_GUI_SOAK_STARTED')
