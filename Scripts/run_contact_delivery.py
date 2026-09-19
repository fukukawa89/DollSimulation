import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Scripts'
for name in ('test_contacts_extended.py','setup_demo_scene.py'):
    runpy.run_path(str(root/name),run_name='__main__')
unreal.log('POSEDOLL_CONTACT_DELIVERY_SUCCESS')
