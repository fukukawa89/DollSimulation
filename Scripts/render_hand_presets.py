import unreal,json,traceback
from pathlib import Path
root=Path(unreal.Paths.project_dir());out={'passed':False}
try:
    library=json.loads(unreal.PoseDollEditorLibrary.session_command('hand_presets','reload'))
    assert 'presets' in library,library
    for start in range(0,len(library['presets']),8):
        r=json.loads(unreal.PoseDollEditorLibrary.session_command('hand_render_thumbnails',json.dumps({'start':start,'count':min(8,len(library['presets'])-start)})))
        assert r.get('ok'),r
    out.update(passed=True,count=len(library['presets']))
except Exception:out['exception']=traceback.format_exc()
(root/'reports/hand_render.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
