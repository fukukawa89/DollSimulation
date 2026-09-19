import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Scripts'
for name in ('test_capture.py','test_contacts_extended.py','test_lifecycle.py'):
    runpy.run_path(str(root/name),run_name='__main__')
unreal.log('POSEDOLL_DELIVERY_REFRESH_SUCCESS')
