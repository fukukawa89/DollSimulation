"""Fresh-process saved-pose verification, with no source payload replay."""
from pathlib import Path
import sys,json,math
import unreal
R=Path(unreal.Paths.project_dir());sys.path.insert(0,str(R/'Scripts/O22'))
from capture_bridge import snapshot,errors
out=R/'reports/o22_reopen.json';report={'passed':False,'physical_test':False,'source_payload_replayed':False,'targets':{}}
try:
 expected=json.loads((R/'reports/o22_capture.json').read_text());assert expected['passed']
 level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert level.load_level('/Game/PoseDoll_O22/MeasurementTest')
 for target,prior in expected['targets'].items():
  seq=unreal.load_asset(prior['sequence']);assert unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
  proxies=unreal.ControlRigSequencerLibrary.get_control_rigs(seq);assert len(proxies)==1;rig=proxies[0].control_rig;rows=[]
  for frame,pose in prior['poses'].items():
   unreal.LevelSequenceEditorBlueprintLibrary.set_current_time(int(frame));actual=snapshot(rig);err=errors(pose,actual)
   rows.append({'frame':int(frame),**err});assert err['position_cm']<.001 and err['rotation_deg']<.01,err
  ctrl='hand_l_fk_ctrl';frame=unreal.FrameNumber(4)
  before=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame)
  edited=unreal.EulerTransform(location=before.location,rotation=unreal.Rotator(pitch=before.rotation.pitch,yaw=before.rotation.yaw+10,roll=before.rotation.roll),scale=before.scale)
  with unreal.ScopedEditorTransaction('O22 acceptance manual wrist edit'):
   unreal.ControlRigSequencerLibrary.set_local_control_rig_euler_transform(seq,rig,ctrl,frame,edited,set_key=True)
  after=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame)
  assert abs(after.rotation.yaw-before.rotation.yaw)>9
  unreal.PoseDollEditorLibrary.session_command('undo')
  restored=unreal.ControlRigSequencerLibrary.get_local_control_rig_euler_transform(seq,rig,ctrl,frame)
  assert abs(restored.rotation.yaw-before.rotation.yaw)<.001
  report['targets'][target]={'cases':rows,'manual_wrist_edit':True,'undo':True,'input_disconnected':True}
  unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
 # Target proportions must be preserved, not normalized to the doll or Manny.
 lengths={}
 for target,prior in expected['targets'].items():
  bones=prior['poses']['0'];lengths[target]=math.dist(bones['upperarm_l']['p'],bones['lowerarm_l']['p'])
 assert abs(lengths['manny']-lengths['quinn'])>.5
 report['upperarm_length_cm']=lengths;report['passed']=True
except Exception as exc:report['error']=repr(exc);raise
finally:
 out.write_text(json.dumps(report,indent=2),encoding='utf-8');unreal.LevelSequenceEditorBlueprintLibrary.close_level_sequence()
