"""Capture a qualified O22 file into the one Control Rig in the open sequence."""
import json
from pathlib import Path
import unreal

def capture(payload_file, target, mask="FullBody", parts=()):
    if target not in ('manny','quinn'):raise ValueError('Choose manny or quinn explicitly')
    seq=unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
    if not seq:raise ValueError('Open the intended Level Sequence first')
    rigs=unreal.ControlRigSequencerLibrary.get_control_rigs(seq)
    if len(rigs)!=1:raise ValueError('This helper needs one Control Rig in the open sequence; use the explicit API for more')
    unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    frame=unreal.LevelSequenceEditorBlueprintLibrary.get_current_time()
    profile=Path(unreal.Paths.project_dir())/'Shared/Profiles/O22'/f'{target}.json'
    result=json.loads(unreal.PoseDollEditorLibrary.capture_measured_pose22(seq,rigs[0].control_rig,frame,str(Path(payload_file).resolve()),str(profile),False,mask,",".join(parts)))
    if not result.get('ok'):raise ValueError(result.get('error','O22 capture failed'))
    unreal.log(f'O22 pose keyed at frame {frame}; use Sequencer Undo or Save as needed.')
    return result
