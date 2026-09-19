"""60 Hz raw sensor test source. Run using Tools/PoseDollSimulator/.venv only."""
import json,math,time,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Tools/PoseDollSimulator/src'))
from posedoll_sim.core import DeviceProfile,default_shared
from posedoll_sim.transport import SensorServer
p=DeviceProfile(default_shared());server=SensorServer(p)
control=ROOT/'reports/soak_control.json';stop=ROOT/'reports/soak_stop.signal'
control.write_text('{}',encoding='utf-8')
if stop.exists():stop.unlink()
server.start();begin=time.monotonic();connected_at=None;restarts=0;last_control=None;next_report=begin;settings={}
try:
    while time.monotonic()-begin<2700 and not stop.exists():
        now=time.monotonic();elapsed=now-begin
        if connected_at is None and server.state.startswith('已连接'):connected_at=now
        if connected_at and not settings.get('disable_restarts',False) and restarts<10 and now-connected_at>2*(restarts+1):
            server.restart();restarts+=1
        try:
            data=control.read_text(encoding='utf-8')
            if data!=last_control:
                settings=json.loads(data);last_control=data
                server.faults.paused=bool(settings.get('pause',False))
                server.faults.missing=bool(settings.get('missing',False))
                server.faults.fragment=bool(settings.get('fragment',False))
                server.faults.duplicate_sequence=bool(settings.get('duplicate',False))
                server.faults.stale_sequence=bool(settings.get('stale',False))
                server.faults.coalesce=bool(settings.get('coalesce',False))
                body35=bool(settings.get('body35',False))
                if body35!=server.body35:server.body35=body35;server.restart()
        except (OSError,ValueError):pass
        q={}
        for i,aid in enumerate(p.order):
            lo,hi=p.axes[aid]['limits_rad'];amplitude=min(.18,(hi-lo)*.12)
            center=max(lo+amplitude,min(hi-amplitude,0.))
            q[aid]=center+amplitude*math.sin(elapsed*.55+i*.29)
        if settings.get('hold',False):q=dict.fromkeys(p.order,0.)
        q.update(settings.get('angles_rad',{}))
        server.set_pose(q)
        if now>=next_report:
            (ROOT/'reports/soak_source.json').write_text(json.dumps({'elapsed_seconds':elapsed,'sent':server.sent,'session':server.session,'restarts':restarts,'state':server.state,'error':server.error}),encoding='utf-8')
            next_report=now+1
        time.sleep(.008)
finally:
    server.close()
    (ROOT/'reports/soak_source_final.json').write_text(json.dumps({'elapsed_seconds':time.monotonic()-begin,'sent':server.sent,'restarts':restarts,'error':server.error}),encoding='utf-8')
