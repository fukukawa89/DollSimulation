import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Scripts'
for name in ('test_rig_fixtures.py','test_axis_sweep.py','test_capture_advanced.py','test_contacts.py','test_capture_reopen.py','test_winding.py'):
    unreal.log('POSEDOLL_RUNNING '+name)
    runpy.run_path(str(root/name),run_name='__main__')
unreal.log('POSEDOLL_VALIDATION_BUNDLE_SUCCESS')
