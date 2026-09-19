import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Scripts'
for name in ('test_capture_reopen.py','test_contacts_reopen.py'):
    runpy.run_path(str(root/name),run_name='__main__')
unreal.log('POSEDOLL_POST_SOAK_DELIVERY_SUCCESS')
