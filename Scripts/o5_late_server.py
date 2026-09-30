"""Adversarial simulated TCP peer for cancel/new-request Editor integration only."""
import argparse,json,select,socket,sys,time
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--case',required=True);a=ap.parse_args()
sys.path.insert(0,str(a.root/'Tools/PoseDollSimulator/src'))
from posedoll_sim.core import DeviceProfile,FrameDecoder,encode
from posedoll_sim.static_protocol import hello,command,envelope,unwrap
p=DeviceProfile(a.root/'Shared');h=hello(p,'o5-late-test','test-boot',['boot'+str(i) for i in range(6)])
raw,status=p.encode_angles(dict.fromkeys(p.order,0.0));raw[:3]=[None]*3;status[:3]=['fixed']*3
listener=socket.socket();listener.bind(('127.0.0.1',39178));listener.listen(1);listener.settimeout(10)
(a.root/'reports/o5/server_ready').write_text(a.case)
def ack(cid,req):return {**command(h,cid,'accepted'),'request_start_us':str(req),'source_boots':h['source_boots']}
def scan(cid,req,sid):
 start=time.monotonic_ns()//1000;time.sleep(.002);end=time.monotonic_ns()//1000
 return {**ack(cid,req),'type':'scan','scan_id':str(sid),'start_us':str(start),'end_us':str(end),'duration_us':end-start,'raw_angles_rad':raw,'axis_status':status}
def send(client,packets):
 wire=b''.join(encode(envelope(m)) for m in packets)
 if a.case=='crc':
  bad=envelope(packets[0]);bad['crc32']='00000000';wire=encode(bad)+b''.join(encode(envelope(m)) for m in packets[1:])
 if a.case=='fragment':
  for offset in range(0,len(wire),17):client.sendall(wire[offset:offset+17])
 else:client.sendall(wire)
try:
 with listener.accept()[0] as c:
  c.settimeout(5);c.sendall(encode(envelope(h)));dec=FrameDecoder();old=[];n=0;active=None;sid=0;req=0;due=0;staged=False
  while True:
   ready,_,_=select.select([c],[],[],.003)
   if ready:
    data=c.recv(65536)
    if not data:break
    for obj in dec.feed(data):
     if obj.get('type')=='welcome':continue
     m=unwrap(obj)
     if m['type'] in ('ack','cancel'):
      if active==m['capture_id']:active=None
      continue
     if m['type']!='request':continue
     n+=1;cid=m['capture_id'];req=time.monotonic_ns()//1000
     limit=5 if a.case=='many' else 1
     if n<=limit:
      old.extend([ack(cid,req),scan(cid,req,1)]);continue
     if a.case=='unknown':old[0]['capture_id']='unknown-id'
     if a.case=='boot':old[0]['source_boots']=['changed']+h['source_boots'][1:]
     new_ack=ack(cid,req)
     if a.case=='after':packets=[new_ack]+old
     elif a.case=='mixed':packets=[old[0],new_ack]+old[1:]
     else:packets=old+[new_ack]
     send(c,packets);active=cid;sid=0;due=time.monotonic();staged=True
   if active and time.monotonic()>=due:
    sid+=1;c.sendall(encode(envelope(scan(active,req,sid))));due+=.1
except (ConnectionError,OSError):pass
finally:listener.close()
