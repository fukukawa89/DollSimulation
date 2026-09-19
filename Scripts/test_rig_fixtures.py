import json
from pathlib import Path
import unreal

result=json.loads(unreal.PoseDollEditorLibrary.test_rig_fixtures())
path=Path(unreal.Paths.project_dir())/'reports'/'rig_fixture_results.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
if not result.get('passed'):raise RuntimeError('Rig fixture readback failed; inspect '+str(path))
unreal.log('POSEDOLL_RIG_FIXTURES_SUCCESS')
