import json
from pathlib import Path
import unreal

result=json.loads(unreal.PoseDollEditorLibrary.inspect_rig('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple','/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body'))
path=Path(unreal.Paths.project_dir())/'reports'/'rig_runtime_probe.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
if 'error' in result:raise RuntimeError(result['error'])
unreal.log('POSEDOLL_RUNTIME_PROBE_SUCCESS')
