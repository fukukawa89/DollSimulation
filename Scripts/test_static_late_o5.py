"""Real UE TCP late-message race regression, isolated O5 test project only."""
from pathlib import Path
import json,os,subprocess,time,traceback,unreal
root=Path(unreal.Paths.project_dir());out=root/'reports/o5';out.mkdir(parents=True,exist_ok=True)
result={'passed':False,'scope':'real UE + adversarial simulated TCP; no hardware','cases':[]};proc=None

def cmd(a,arg='',expect=True):
 r=json.loads(unreal.PoseDollEditorLibrary.session_command(a,arg))
 if expect is not None:assert r['ok']==expect,(a,r)
 return r

def wait(pred):
 end=time.monotonic()+5
 while time.monotonic()<end:
  cmd('tick');s=cmd('status')
  if pred(s):return s
  time.sleep(.01)
 raise AssertionError(cmd('status'))
try:
 level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
 assert level.new_level('/Game/PoseDollO5/LateReplies')
 actor=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(),unreal.Rotator())
 actor.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'))
 seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset('O5LateReplies','/Game/PoseDollO5',unreal.LevelSequence,unreal.LevelSequenceFactoryNew())
 seq.set_display_rate(unreal.FrameRate(24,1));seq.set_playback_end(120);seq.add_possessable(actor)
 assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
 assert unreal.PoseDollEditorLibrary.bind_target(seq,actor.skeletal_mesh_component)
 for idx,case in enumerate(['before','after','mixed','fragment','many','unknown','crc','boot']):
  ready=out/'server_ready';ready.unlink(missing_ok=True)
  proc=subprocess.Popen([os.environ['POSEDOLL_TEST_PYTHON'],'-X','utf8',str(root/'Scripts/o5_late_server.py'),'--root',str(root),'--case',case],creationflags=0x08000000)
  until=time.monotonic()+5
  while not ready.exists():
   assert time.monotonic()<until;time.sleep(.01)
  cmd('connect','static');wait(lambda s:s.get('static_source') and s['state']=='Ready')
  unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(10+idx*5);before=cmd('status')['captures']
  for _ in range(5 if case=='many' else 1):
   cmd('snapshot_capture');time.sleep(.04);cmd('snapshot_cancel')
  # New command goes immediately after cancel; no waiting for TCP drain.
  new=cmd('snapshot_capture')['capture_id']
  s=wait(lambda s:s['snapshot_state'] in ('Committed','Fault','Cancelled','TimedOut'))
  healthy=case not in ('unknown','crc','boot')
  assert s['captures']==before+int(healthy),(case,s)
  if healthy:
   assert s['snapshot_state']=='Committed' and s['capture_id']==new,(case,s)
   assert s['retired_replies']==(10 if case=='many' else 2),(case,s)
   cmd('capture',json.dumps({'frame':10+idx*5}),expect=False)
  result['cases'].append({'case':case,'result':s,'captures_added':s['captures']-before})
  cmd('disconnect');proc.wait(timeout=5);proc=None
 result['passed']=True
except:result['exception']=traceback.format_exc();unreal.log_error(result['exception'])
finally:
 cmd('disconnect',expect=None)
 if proc is not None:
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:proc.terminate()
 unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence();(out/'late_editor.json').write_text(json.dumps(result,indent=2),encoding='utf-8');unreal.SystemLibrary.quit_editor()
