import json
from pathlib import Path
import unreal

result=json.loads(unreal.PoseDollEditorLibrary.run_transport_probe(8.0))
path=Path(unreal.Paths.project_dir())/'reports'/'transport_probe.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
if result.get('valid',0)<360 or result.get('invalid',1)!=0:raise RuntimeError(str(result))
unreal.log('POSEDOLL_TRANSPORT_PROBE_SUCCESS '+str(result))
