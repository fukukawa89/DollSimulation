import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Scripts'
for name in ('run_validation.py','test_contacts_reopen.py'):
    runpy.run_path(str(root/name),run_name='__main__')
unreal.log('POSEDOLL_FINAL_REOPEN_SUCCESS')
