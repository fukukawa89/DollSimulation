import copy,json,math,socket,time,uuid
from pathlib import Path
import pytest
from posedoll_sim.core import DeviceProfile,FrameDecoder,encode
from posedoll_sim.static_protocol import *
from posedoll_sim.static_transport import StaticSensorServer

ROOT=Path(__file__).resolve().parents[3]

def profile():return DeviceProfile(ROOT/'Shared')
def make_case(kind='healthy',req=1_000_000):
    if kind=='near_u64_max':req=2**64-1000000
    p=profile();h=hello(p,'device','g0',['n'+str(i) for i in range(8)]);cid='test-'+kind
    ack={**command(h,cid,'accepted'),'request_start_us':str(req),'source_boots':h['source_boots']}
    messages=[(ack,100.001)];axis=p.index['elbow_l.flex']
    for i in range(40 if kind=="buffer_overflow" else 8):
        q=dict.fromkeys(p.order,0.0)
        if kind=='drift':q['elbow_l.flex']=math.radians(.25*i*.1)
        if kind=='transient':q['elbow_l.flex']=math.radians(1 if i==4 else 0)
        raw,status=p.encode_angles(q);raw[:3]=[None]*3;status[:3]=['fixed']*3
        start=req+1000+i*100000;end=start+20000
        m={**command(h,cid,'scan'),'request_start_us':str(req),'source_boots':list(h['source_boots']),'scan_id':str(i+1),'start_us':str(start),'end_us':str(end),'duration_us':20000,'raw_angles_rad':raw,'axis_status':status}
        if kind=='buffer_overflow':
            start=req+1000+i*1000;end=start+500;m.update(start_us=str(start),end_us=str(end),duration_us=500)
        if i==4:
            if kind=='wrong_id':m['capture_id']='wrong'
            if kind=='restart':m['source_boots'][2]='new'
            if kind=='satellite_restart':m['source_boots'][6]='new'
            if kind=='profile':m['profile_sha256']='f'*64
            if kind=='calibration':m['calibration_sha256']='f'*64
            if kind=='missing':m['raw_angles_rad'][axis]=None;m['axis_status'][axis]='missing'
            if kind=='fault':m['raw_angles_rad'][axis]=None;m['axis_status'][axis]='invalid'
            if kind=='fixed_axis':m['raw_angles_rad'][axis]=None;m['axis_status'][axis]='fixed'
            if kind=='root_zero':m['raw_angles_rad'][0]=0;m['axis_status'][0]='valid'
            if kind=='duplicate':m['scan_id']=str(i)
            if kind=='missing_scan':m['scan_id']=str(i+2)
            if kind=='overlap':m['start_us']=str(start-95000);m['duration_us']=115000
            if kind=='old':m['start_us']=str(req-1)
            if kind=='negative':m['start_us']='-1'
            if kind=='uint64_overflow':m['start_us']=str(2**64)
            if kind=='uint32_overflow':m['duration_us']=2**32
            if kind=='uint16_truncate':m['end_us']=str(start+100000);m['duration_us']=34464
            if kind=='300ms':m['end_us']=str(start+300000);m['duration_us']=300000
            if kind=='bool_duration':m['duration_us']=True
        received=100+(end-req)/1e6
        if kind=='queue_stale':received+=.3
        if kind=='future':received-=.02
        if kind=='timeout' and i==4:received+=4
        messages.append((m,received))
    if kind=='no_ack':messages=messages[1:]
    return p,h,cid,messages

FAULTS=['buffer_overflow','wrong_id','restart','satellite_restart','profile','calibration','missing','fault','fixed_axis','root_zero','duplicate','missing_scan','overlap','old','negative','uint64_overflow','uint32_overflow','uint16_truncate','300ms','bool_duration','queue_stale','future','timeout','no_ack']

def run_case(kind,req=1_000_000):
    p,h,cid,rows=make_case(kind,req);w=StaticWindow(p,h,cid,100)
    for msg,t in rows:
        if w.push(unwrap(envelope(msg)),t):break
    return w

@pytest.mark.parametrize('kind',FAULTS)
def test_reject(kind):
    with pytest.raises(ValueError):run_case(kind)
@pytest.mark.parametrize('kind',['drift','transient'])
def test_motion(kind):assert run_case(kind).final is None
@pytest.mark.parametrize('req',[0,2**53+987654,2**64-1000000])
def test_monotonic_integer_precision(req):assert run_case('healthy',req).final is not None

def test_final_whole_scan():
    w=run_case('healthy');assert w.final['scan_id']=='7';assert w.stable_us==580000

def test_crc():
    m=envelope({'protocol':PROTOCOL,'value':100000});m['payload']=m['payload'].replace('100000','300000')
    with pytest.raises(ValueError):unwrap(m)

def test_duration_100ms_not_truncated():
    p,h,cid,rows=make_case();w=StaticWindow(p,h,cid,100)
    w.push(*rows[0]);m,t=rows[1];m['duration_us']=100000;m['end_us']=str(int(m['start_us'])+100000)
    assert not w.push(m,100.101)

def test_unknown_multiturn_rejected():
    p=profile();raw,status=p.encode_angles(dict.fromkeys(p.order,0.0));raw[:3]=[None]*3;status[:3]=['fixed']*3
    p.axes[p.order[3]]['limits_rad']=[-7,7]
    with pytest.raises(ValueError,match='AMBIGUOUS'):absolute(p,raw,status)

def test_absolute_large_pose_jump():
    p=profile();aid=p.order[3];p.axes[aid]['limits_rad']=[math.radians(-175),math.radians(175)]
    for degrees in (170,-170):
        q=dict.fromkeys(p.order,0.0);q[aid]=math.radians(degrees);raw,status=p.encode_angles(q);raw[:3]=[None]*3;status[:3]=['fixed']*3
        assert abs(math.degrees(absolute(p,raw,status)[3])-degrees)<1e-8

def test_tcp_request_and_fragmentation():
    p=profile();s=StaticSensorServer(p,0);s.faults.fragment=True;s.start();port=s._socket.getsockname()[1]
    try:
        with socket.create_connection(('127.0.0.1',port),timeout=2) as c:
            c.settimeout(2);d=FrameDecoder();messages=[]
            while not messages:messages=d.feed(c.recv(65536))
            h=unwrap(messages.pop());c.sendall(encode(dict(type='welcome',protocol=PROTOCOL,session_id=h['gateway_boot'],accepted=True)))
            cid=str(uuid.uuid4());start=time.monotonic();w=StaticWindow(p,h,cid,start);c.sendall(encode(envelope(command(h,cid))))
            while w.final is None:
                for msg in d.feed(c.recv(65536)):
                    if w.push(unwrap(msg),time.monotonic()):break
            c.sendall(encode(envelope(command(h,cid,'ack'))));assert time.monotonic()-start<1.5
            assert w.stable_us>=500000 and s.request_count==1
    finally:s.close()


def golden(path):
    cases=[]
    for kind in ['healthy','near_u64_max','drift','transient',*FAULTS]:
        p,h,cid,rows=make_case(kind)
        cases.append(dict(name=kind,hello=envelope(h),capture_id=cid,wall_start=100.,messages=[dict(wire=envelope(m),received=t) for m,t in rows],expected='fault' if kind in FAULTS else ('ready' if kind in ('healthy','near_u64_max') else 'waiting')))
    Path(path).write_text(json.dumps(dict(schema='pds1-cross-language-v1',cases=cases),indent=2),encoding='utf-8')
