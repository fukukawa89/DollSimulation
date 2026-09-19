import math
from PySide6.QtWidgets import QApplication
from posedoll_sim.core import DeviceProfile,default_shared
from posedoll_sim.transport import SensorServer
from posedoll_sim.ui import SimulatorWindow

app=QApplication([])
p=DeviceProfile(default_shared());s=SensorServer(p)
w=SimulatorWindow(p,s)
w.preset.setCurrentIndex(4);w.apply_preset()
for aid,(spin,slider,box) in w.controls.items():
    assert abs(spin.value()-math.degrees(w.q[aid]))<.06,(aid,spin.value(),w.q[aid])
original=dict(w.q);w.mirror();w.mirror()
assert w.q==original
w.variant.setCurrentIndex(1)
assert all(w.q[aid]==value for aid,value in p.fixed.items())
w.tick()
assert len(w.decoded)==44
w.close()
print('PASS: 44 displayed angles, mirror involution, Body35 locked axes, local raw diagnostics, clean UI close')
