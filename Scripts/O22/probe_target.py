"""Read-only native target solve and Sequencer API inventory."""
import json
from pathlib import Path
import unreal
R=Path(unreal.Paths.project_dir())
D=Path('E:/UnrealProjects/PoseDollLab_UE58_Design/PoseDoll_HW44_Plan/design/Hardware/PoseDoll44/bench/revO22')
out={'status':'RUNNING','targets':{},'api':{}}
try:
 for name in ['manny','quinn']:
  rows={}
  for pose in ['a_stand','left_elbow','asymmetric','right_forearm']:
   rows[pose]=json.loads(unreal.PoseDollEditorLibrary.solve_measured_pose22(str(D/'ue'/f'{pose}.payload.json'),str(R/'Shared/Profiles/O22'/f'{name}.json'),True))
  out['targets'][name]=rows
 for name in dir(unreal.ControlRigSequencerLibrary):
  if any(s in name for s in ('create_control','set_local','set_control_rig_bool','get_control_rigs')):
   out['api'][name]=getattr(unreal.ControlRigSequencerLibrary,name).__doc__
 out['status']='PASS' if all(x.get('ok') for v in out['targets'].values() for x in v.values()) else 'FAIL'
finally:
 (R/'reports/o22_target_probe.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
 unreal.log('O22_TARGET_PROBE '+out['status'])
