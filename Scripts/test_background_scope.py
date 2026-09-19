"""Verify editor preference restoration across real panel lifetimes."""
import json
from pathlib import Path
import unreal
performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original=performance.get_editor_property('bThrottleCPUWhenNotForeground')
report={'passed':False,'original_background_throttle':original,'cycles':0}
try:
    unreal.PoseDollEditorLibrary.session_command('close_panel')
    for value in (True,False):
        performance.set_editor_property('bThrottleCPUWhenNotForeground',value)
        for _ in range(5):
            unreal.PoseDollEditorLibrary.session_command('open_panel')
            assert not performance.get_editor_property('bThrottleCPUWhenNotForeground')
            unreal.PoseDollEditorLibrary.session_command('close_panel')
            assert performance.get_editor_property('bThrottleCPUWhenNotForeground')==value
            report['cycles']+=1
    report['passed']=True
    unreal.log('POSEDOLL_BACKGROUND_SCOPE_SUCCESS')
except Exception as exc:
    report['exception']=repr(exc);raise
finally:
    unreal.PoseDollEditorLibrary.session_command('close_panel')
    performance.set_editor_property('bThrottleCPUWhenNotForeground',original)
    (Path(unreal.Paths.project_dir())/'reports/background_scope.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
