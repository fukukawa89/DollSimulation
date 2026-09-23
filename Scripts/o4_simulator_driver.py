"""External Python helper for real-UE tests. It owns only the simulated sensor TCP server."""
import sys,time,json,argparse
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--control-dir',type=Path,required=True);a=ap.parse_args();root=a.root
sys.path.insert(0,str(root/'Tools/PoseDollSimulator/src'))
from posedoll_sim.core import DeviceProfile,SensorDecoder
from posedoll_sim.static_transport import StaticSensorServer
server=StaticSensorServer(DeviceProfile(root/'Shared'));server.start()
report=a.control_dir;report.mkdir(parents=True,exist_ok=True);(report/'o4_source_ready').write_text('ready')
seen=set();done=False
try:
    while not done:
        for control in sorted(report.glob('command_*.json')):
            if control.name in seen:continue
            try:data=json.loads(control.read_text(encoding='utf-8'))
            except (OSError,ValueError):continue # Writer has not yet closed a newly created file.
            if data.get('stop'):done=True;break
            if 'fault' in data:server.static_fault=data['fault']
            if 'fixture' in data:
                decoder=SensorDecoder(server.profile);decoder.handshake(json.loads((root/'Shared/Fixtures/hello.json').read_text(encoding='utf-8')))
                server.set_pose(decoder.sample(json.loads((root/'Shared/Fixtures'/data['fixture']).read_text(encoding='utf-8'))))
            seen.add(control.name);(report/(data['id']+'.ack')).write_text('ack')
        time.sleep(.005)
finally:
    server.close();(report/'o4_source_result.json').write_text(json.dumps({'sent':server.sent,'requests':server.request_count,'error':server.error}),encoding='utf-8')
