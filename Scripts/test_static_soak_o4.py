"""1000 serial wall-clock loopback transactions. Simulated source, no hardware/UE."""
import argparse,gzip,hashlib,json,math,socket,sys,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools/PoseDollSimulator/src'))
from posedoll_sim.core import DeviceProfile,SensorDecoder,FrameDecoder,encode
from posedoll_sim.static_protocol import StaticWindow,PROTOCOL,command,envelope,unwrap
from posedoll_sim.static_transport import StaticSensorServer
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--requests',type=int,default=1000);args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    source_paths=[ROOT/'Tools/PoseDollSimulator/src/posedoll_sim'/n for n in ('core.py','transport.py','static_protocol.py','static_transport.py')]+[Path(__file__)]
    fingerprint=lambda:{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
    hashes=fingerprint();p=DeviceProfile(ROOT/'Shared');server=StaticSensorServer(p,0);server.faults.noise_degrees=.005;server.faults.fragment=True;server.start();results=[];exceptions=[];wall=time.monotonic()
    fixtures=[]
    for name in ('neutral','left_elbow_90','left_shoulder_forward_90','left_knee_90','asymmetric_pose'):
        d=SensorDecoder(p);d.handshake(json.loads((p.shared/'Fixtures/hello.json').read_text()));fixtures.append(d.sample(json.loads((p.shared/'Fixtures'/(name+'.sample.json')).read_text())))
    # Noise at an exact hard stop is correctly rejected. Healthy trials stay 1 degree
    # inside measured-axis limits; root slots remain explicitly fixed.
    for q in fixtures:
        for aid in p.order[3:]:
            lo,hi=p.axes[aid]['limits_rad'];q[aid]=min(hi-math.radians(1),max(lo+math.radians(1),q[aid]))
    try:
        with gzip.open(args.output/'wire.jsonl.gz','wt',encoding='utf-8') as wire, socket.create_connection(('127.0.0.1',server._socket.getsockname()[1]),timeout=4) as c:
            c.settimeout(4);decoder=FrameDecoder();rows=[]
            while not rows:rows=decoder.feed(c.recv(65536))
            h=unwrap(rows[0]);wire.write(json.dumps({'hello':rows[0]})+'\n');c.sendall(encode(dict(type='welcome',protocol=PROTOCOL,session_id=h['gateway_boot'],accepted=True)))
            for i in range(args.requests):
                server.set_pose(fixtures[i%len(fixtures)]);cid=str(uuid.uuid4());start=time.monotonic();window=StaticWindow(p,h,cid,start);request=envelope(command(h,cid));c.sendall(encode(request));wire.write(json.dumps({'request':request,'wall_start':start})+'\n')
                while window.final is None:
                    data=c.recv(65536)
                    if not data:raise ConnectionError('Source disconnected')
                    for msg in decoder.feed(data):
                        received=time.monotonic();wire.write(json.dumps({'wire':msg,'received':received})+'\n');parsed=unwrap(msg)
                        if parsed.get('capture_id')!=cid:raise AssertionError('Old request reply')
                        window.push(parsed,received)
                elapsed=(time.monotonic()-start)*1000;c.sendall(encode(envelope(command(h,cid,'ack'))));results.append({'request':i+1,'capture_id':cid,'latency_ms':elapsed,'scan_id':window.final['scan_id'],'scan_span_us':window.final['duration_us'],'stable_us':window.stable_us})
                if (i+1)%100==0:print(f'{i+1}/{args.requests} accepted',flush=True)
    except Exception as e:exceptions.append(repr(e))
    finally:server.close()
    ms=sorted(r['latency_ms'] for r in results);p95=ms[math.ceil(.95*len(ms))-1] if ms else None
    report={'scope':'ACTUAL_SERIAL_LOOPBACK_WALL_CLOCK_SIMULATED_SOURCE_NOT_HARDWARE_OR_UE','requested':args.requests,'accepted':len(results),'under_3_seconds':sum(r['latency_ms']<3000 for r in results),'p95_ms':p95,'max_ms':max(ms,default=None),'elapsed_seconds':time.monotonic()-wall,'proposed_target_met':len(results)>=args.requests-1 and p95 is not None and p95<=1000 and not exceptions,'exceptions':exceptions,'source_sha256':hashes,'source_unchanged':hashes==fingerprint(),'results':results,'wire_sha256':hashlib.sha256((args.output/'wire.jsonl.gz').read_bytes()).hexdigest(),'physical_tested':False}
    (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k not in ('results','source_sha256')},indent=2));return 0 if report['proposed_target_met'] and report['source_unchanged'] else 1
if __name__=='__main__':raise SystemExit(main())
