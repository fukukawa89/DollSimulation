"""Request-driven simulated G0. No hardware driver or cached legacy-frame conversion."""
from __future__ import annotations
import socket,time,uuid,select,random
from .core import FrameDecoder,encode
from .transport import SensorServer
from .static_protocol import hello,command,envelope,unwrap,validate_command,REGIONS

class StaticSensorServer(SensorServer):
    def __init__(self,profile,port=39178):
        super().__init__(profile,port)
        self.device='posedoll-static-simulator';self.static_fault='';self.request_count=0
        self.delay_per_region=.002;self.active_hz=10
    def _scan(self,h,req,start_us,sid):
        begin=time.monotonic_ns()//1000
        raw=[None]*3;status=['fixed']*3;offset=3
        # Each virtual region is acquired AFTER the explicit request, with actual
        # monotonic read bounds. Pose changes between regions are not hidden.
        for count in REGIONS:
            sample=self.snapshot()
            raw.extend(sample['raw_angles_rad'][offset:offset+count]);status.extend(sample['axis_status'][offset:offset+count]);offset+=count
            self._stop.wait(self.delay_per_region)
        end=time.monotonic_ns()//1000
        p={**command(h,req,'scan'),'request_start_us':str(start_us),'source_boots':list(h['source_boots']),
           'scan_id':str(sid),'start_us':str(begin),'end_us':str(end),'duration_us':end-begin,'raw_angles_rad':raw,'axis_status':status}
        f=self.static_fault
        if f=='wrong_request':p['capture_id']='old-request'
        if f=='node_restart':p['source_boots'][2]='restarted'
        if f=='satellite_restart':p['source_boots'][-2]='restarted'
        if f=='old_scan':p['start_us']=str(start_us-1)
        if f=='duplicate_scan' and sid>1:p['scan_id']=str(sid-1)
        if f=='profile_change':p['profile_sha256']='0'*64
        if f=='missing':p['axis_status'][10]='missing';p['raw_angles_rad'][10]=None
        if f=='fault':p['axis_status'][10]='invalid';p['raw_angles_rad'][10]=None
        packet=envelope(p)
        if f=='crc':packet['crc32']='00000000'
        return packet
    def _run(self):
        while not self._stop.is_set():
            self.state=f'静态采集 · 监听 127.0.0.1:{self.port}'
            try:client,_=self._socket.accept()
            except socket.timeout:continue
            except OSError as e:self.error=str(e);break
            with client:
                try:
                    self._restart.clear();client.settimeout(.25);client.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                    h=hello(self.profile,self.device,self.session,[str(uuid.uuid4()) for _ in range(8)])
                    client.sendall(encode(envelope(h)));decoder=FrameDecoder();deadline=time.monotonic()+3;welcomed=False
                    while not welcomed and time.monotonic()<deadline and not self._stop.is_set():
                        try:data=client.recv(65536)
                        except socket.timeout:continue
                        if not data:raise ConnectionError('Disconnected during hello')
                        for p in decoder.feed(data):
                            if p.get('type')!='welcome' or p.get('protocol')!='PDS1/1' or p.get('session_id')!=self.session or p.get('accepted') is not True:raise ValueError('PDS1 welcome rejected')
                            welcomed=True
                    if not welcomed:raise TimeoutError('PDS1 handshake timeout')
                    active=None;seen=set();next_scan=0;sid=0;request_start=0;self.error='';self.state='已连接 · 静态采集待命'
                    while not self._stop.is_set() and not self._restart.is_set():
                        ready,_,_=select.select([client],[],[],.005)
                        if ready:
                            data=client.recv(65536)
                            if not data:break
                            for msg in decoder.feed(data):
                                p=unwrap(msg);validate_command(p,h);cid=p['capture_id']
                                if p['type'] in ('cancel','ack'):
                                    if cid==active:active=None;self.state='已连接 · 保留历史快照'
                                elif p['type']=='request':
                                    if cid==active:
                                        client.sendall(encode(envelope({**command(h,cid,'accepted'),'request_start_us':str(request_start),'source_boots':h['source_boots']})))
                                    elif cid not in seen:
                                        if len(seen)>=4096:raise ValueError('Session request budget; reconnect')
                                        seen.add(cid);active=cid;sid=0;request_start=time.monotonic_ns()//1000;next_scan=time.monotonic();self.request_count+=1
                                        client.sendall(encode(envelope({**command(h,cid,'accepted'),'request_start_us':str(request_start),'source_boots':h['source_boots']})))
                                        self.state='正在读取 · 请保持姿势'
                        if active and time.monotonic_ns()//1000-request_start>3000000:active=None;self.state='采集超时 · 等待新请求'
                        if active and time.monotonic()>=next_scan and not self.faults.paused and self.static_fault!='timeout':
                            if self.static_fault=='disconnect':break
                            sid+=1;packet=encode(self._scan(h,active,request_start,sid))
                            if self.faults.fragment:
                                client.sendall(packet[:2]);client.sendall(packet[2:17]);client.sendall(packet[17:])
                            else:client.sendall(packet)
                            self.sent+=1
                            next_scan=max(next_scan+1/self.active_hz+random.uniform(-.004,.004),time.monotonic())
                except (OSError,ValueError,ConnectionError) as e:self.error=str(e)
        self.state='已停止'
