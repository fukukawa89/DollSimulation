"""Replay recorded wall-clock traffic through the current bounded decoder."""
import gzip,hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'Tools/PoseDollSimulator/src'))
from posedoll_sim.core import DeviceProfile
from posedoll_sim.static_protocol import StaticWindow,unwrap
source=root/'reports/o4/soak_20260924_r2';p=DeviceProfile(root/'Shared');ready=0;w=None
with gzip.open(source/'wire.jsonl.gz','rt',encoding='utf-8') as stream:
 for line in stream:
  r=json.loads(line)
  if 'hello' in r:h=unwrap(r['hello'])
  elif 'request' in r:
   if w is not None:assert w.final is not None
   request=unwrap(r['request']);w=StaticWindow(p,h,request['capture_id'],r['wall_start'])
  else:
   if w.push(unwrap(r['wire']),r['received']):ready+=1
assert ready==1000
decoder=root/'Tools/PoseDollSimulator/src/posedoll_sim/static_protocol.py'
result={'passed':True,'accepted':ready,'scope':'OFFLINE_REPLAY_OF_RECORDED_WALL_CLOCK_TRAFFIC_NOT_NEW_NETWORK_OR_HARDWARE_TEST','original_soak_report_sha256':hashlib.sha256((source/'report.json').read_bytes()).hexdigest(),'wire_sha256':hashlib.sha256((source/'wire.jsonl.gz').read_bytes()).hexdigest(),'current_decoder_sha256':hashlib.sha256(decoder.read_bytes()).hexdigest(),'change_after_soak':'Moved 32-scan overflow check ahead of minimum-duration return; healthy semantics unchanged; new overflow golden case separately exercised.'}
(root/'reports/o4/replay_final.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
