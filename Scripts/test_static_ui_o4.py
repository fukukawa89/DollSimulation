"""Qt widget behavior only, no OpenGL visual/physical qualification."""
import os,sys,json
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'Tools/PoseDollSimulator/src'))
from PySide6.QtWidgets import QApplication
from posedoll_sim.core import DeviceProfile
from posedoll_sim.static_transport import StaticSensorServer
from posedoll_sim.ui import SimulatorWindow
app=QApplication([]);p=DeviceProfile(root/'Shared');server=StaticSensorServer(p);w=SimulatorWindow(p,server)
checks=[]
assert w.static_mode and w.variant.count()==1 and not w.variant.isEnabled();checks.append('Static41 label, no legacy capability switch')
for aid in p.order[:3]:
 assert not w.controls[aid][0].isEnabled();w.set_angle(aid,.1);assert w.q[aid]==0
checks.append('three fixed roots reject widget/drag editing')
w.preset.setCurrentIndex(4);w.apply_preset();assert all(w.q[k]==0 for k in p.order[:3]);checks.append('asymmetric preset retains fixed pelvis')
w.tick();assert server.sent==0 and server.latest is None;checks.append('idle UI does not fabricate scans')
w.close()
result={'passed':True,'scope':__doc__,'checks':checks};(root/'reports/o4/ui_final.json').write_text(json.dumps(result,indent=2));print(result)
