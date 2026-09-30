"""PDS1/1 host transport and independent static acceptance oracle.
CRC-32/ISO-HDLC covers the exact UTF-8 payload string. This is integrity, not authentication.
Static times are one gateway's monotonic domain. No legacy PD41 fields are reinterpreted.
"""
from __future__ import annotations
import math, time, zlib, json
from .core import strict_json, uint64

PROTOCOL='PDS1/1'
IDENTITY=('protocol','device_id','gateway_boot','profile_sha256','calibration_sha256')
BASE=set(IDENTITY)|{'type','capture_id'}
ACK=BASE|{'request_start_us','source_boots'}
SCAN=ACK|{'scan_id','start_us','end_us','duration_us','raw_angles_rad','axis_status'}
REGIONS=(3,6,9,9,7,7)


def envelope(payload):
    text=json.dumps(payload,ensure_ascii=False,separators=(',',':'),allow_nan=False)
    data=text.encode('utf-8')
    if not 0<len(data)<=32768:raise ValueError('PDS1 payload length')
    return {'type':'pds1','payload':text,'crc32':f'{zlib.crc32(data):08x}'}


def unwrap(message):
    if set(message)!={'type','payload','crc32'} or message['type']!='pds1' or type(message['payload']) is not str:raise ValueError('PDS1 envelope')
    data=message['payload'].encode('utf-8')
    if not 0<len(data)<=32768 or message['crc32']!=f'{zlib.crc32(data):08x}':raise ValueError('PDS1 CRC/length')
    p=strict_json(data)
    if not isinstance(p,dict) or p.get('protocol')!=PROTOCOL:raise ValueError('PDS1 version')
    return p


def hello(profile,device,boot,boots):
    return dict(protocol=PROTOCOL,type='hello',device_id=device,gateway_boot=boot,
                profile_sha256=profile.profile_hash,calibration_sha256=profile.calibration_hash,
                axis_order=profile.order,source_boots=list(boots),source_kind='simulated')


def command(hello,capture_id,kind='request'):
    return {**{k:hello[k] for k in IDENTITY},'type':kind,'capture_id':capture_id}


def validate_command(p,h):
    if set(p)!=BASE or p['type'] not in ('request','cancel','ack') or any(p[k]!=h[k] for k in IDENTITY) or type(p['capture_id']) is not str or not 1<=len(p['capture_id'])<=128:raise ValueError('PDS1 command identity/fields')


def absolute(profile,raw,status):
    if len(raw)!=44 or len(status)!=44 or raw[:3]!=[None]*3 or status[:3]!=['fixed']*3:raise ValueError('PDS1 fixed root layout')
    out=[0.0]*3
    for i,aid in enumerate(profile.order[3:],3):
        c=profile.cal[aid];value=raw[i]
        if status[i]!='valid' or type(value) not in (float,int) or not math.isfinite(value) or not 0<=value<c['raw_period_rad']:raise ValueError('PDS1 missing/fault/raw')
        gain=c['sign']*c['joint_rad_per_sensor_rad'];period=c['raw_period_rad'];base=value-c['zero_raw_rad'];lo,hi=sorted(v/gain for v in profile.axes[aid]['limits_rad'])
        first=math.ceil((lo-base-1e-9)/period);last=math.floor((hi-base+1e-9)/period)
        if first!=last:raise ValueError('AMBIGUOUS_OR_OUTSIDE_HARD_LIMITS:'+aid)
        q=(base+first*period)*gain
        if not profile.axes[aid]['limits_rad'][0]-1e-8<=q<=profile.axes[aid]['limits_rad'][1]+1e-8:raise ValueError('PDS1 joint limit')
        out.append(q)
    return out


def validate_reply(profile, h, p):
    """Full wire validation precedes routing, including for retired transactions."""
    if set(p) not in (ACK, SCAN) or any(p.get(k) != h[k] for k in IDENTITY):
        raise ValueError('PDS1 identity/fields')
    if type(p.get('capture_id')) is not str or not 1 <= len(p['capture_id']) <= 128 or p.get('source_boots') != h['source_boots']:
        raise ValueError('PDS1 capture/boot')
    req = uint64(p['request_start_us'])
    if p['type'] == 'accepted' and set(p) == ACK:
        return
    if p['type'] != 'scan' or set(p) != SCAN:
        raise ValueError('PDS1 reply type')
    start, end, sid = (uint64(p[k]) for k in ('start_us', 'end_us', 'scan_id'))
    duration = p['duration_us']
    if not sid or type(duration) is not int or not 1 <= duration <= 2**32-1 or not req <= start < end or end-start != duration:
        raise ValueError('PDS1 duration/range')
    raw, status = p['raw_angles_rad'], p['axis_status']
    if type(raw) is not list or type(status) is not list or len(raw) != 44 or len(status) != 44 or raw[:3] != [None]*3 or status[:3] != ['fixed']*3:
        raise ValueError('PDS1 fixed root layout')
    for i, aid in enumerate(profile.order[3:], 3):
        if status[i] != 'valid' or type(raw[i]) not in (float, int) or not math.isfinite(raw[i]) or not 0 <= raw[i] < profile.cal[aid]['raw_period_rad']:
            raise ValueError('PDS1 missing/fault/raw')


class StaticRouter:
    """Connection-local bounded retired IDs. Unknown IDs remain errors."""
    MAX_RETIRED = 32
    RETIRED_SECONDS = 30.0

    def __init__(self, profile, h):
        self.profile, self.hello = profile, h
        self.active = None
        self.retired = {}
        self.dropped = 0

    def _prune(self, now):
        self.retired = {k:v for k,v in self.retired.items() if now < v}

    def begin(self, capture, now):
        self._prune(now)
        if type(capture) is not str or not 1 <= len(capture) <= 128 or capture == self.active or capture in self.retired:
            raise ValueError('PDS1 reused capture ID')
        self.retire(self.active, now)
        self.active = capture

    def retire(self, capture, now):
        self._prune(now)
        if not capture or capture != self.active:
            return
        if len(self.retired) == self.MAX_RETIRED:
            del self.retired[next(iter(self.retired))]
        self.retired[capture] = now + self.RETIRED_SECONDS
        self.active = None

    def route(self, message, now):
        p = unwrap(message)
        validate_reply(self.profile, self.hello, p)
        self._prune(now)
        if p['capture_id'] == self.active:
            return p
        if p['capture_id'] in self.retired:
            self.dropped += 1
            return None
        raise ValueError('Unknown PDS1 capture ID')


class StaticWindow:
    """Independent oracle for C++ semantics; simulated source clocks must be explicit."""
    def __init__(self,profile,hello,capture_id,wall_start):
        self.profile=profile;self.hello=hello;self.capture_id=capture_id;self.wall_start=wall_start
        self.state='Requested';self.window=[];self.angles=[];self.ack=None;self.last=None;self.final=None
        self.peak_deg=0.;self.drift_deg_s=0.;self.stable_us=0
    def deadline(self,now):
        if now<self.wall_start or now-self.wall_start>3:
            self.state='TimedOut';self.window.clear();self.angles.clear();raise ValueError('TRANSACTION_TIMEOUT')
    def push(self,p,received):
        try:return self._push(p,received)
        except ValueError:
            self.window.clear();self.angles.clear()
            if self.state!='TimedOut':self.state='Fault'
            raise
    def _push(self,p,received):
        if self.state not in ('Requested','WaitStable'):raise ValueError('No pending capture')
        self.deadline(received)
        if set(p) not in (ACK,SCAN) or any(p.get(k)!=self.hello[k] for k in IDENTITY) or p.get('capture_id')!=self.capture_id or p.get('source_boots')!=self.hello['source_boots']:raise ValueError('PDS1 request/profile/boot')
        req=uint64(p['request_start_us'])
        if p['type']=='accepted' and set(p)==ACK:
            if self.ack is not None and self.ack!=req:raise ValueError('Conflicting ACK')
            self.ack=req;return False
        if p['type']!='scan' or set(p)!=SCAN or self.ack is None or self.ack!=req:raise ValueError('Unacknowledged scan')
        start,end,sid=(uint64(p[k]) for k in ('start_us','end_us','scan_id'))
        duration=p['duration_us']
        if type(duration) is not int or not 1<=duration<=2**32-1 or not req<=start<end or end-start!=duration or duration>100000:raise ValueError('PDS1 duration')
        elapsed=(received-self.wall_start)*1e6
        if end-req>elapsed+5000:raise ValueError('Future source interval')
        if sid!=(self.last['id']+1 if self.last else 1) or (self.last and (start<=self.last['start'] or start<self.last['end'])):raise ValueError('PDS1 scan continuity')
        if self.last and start-self.last['start']>150000:self.window.clear();self.angles.clear()
        self.last=dict(id=sid,start=start,end=end)
        q=absolute(self.profile,p['raw_angles_rad'],p['axis_status'])
        self.window.append(dict(p));self.angles.append(q);self.state='WaitStable'
        while len(self.window)>6 and start-int(self.window[1]['end_us'])>=500000:self.window.pop(0);self.angles.pop(0)
        span=start-int(self.window[0]['end_us'])
        if len(self.window)>32:raise ValueError('PDS1 window budget')
        if len(self.window)<6 or span<500000:return False
        t=[((int(w['start_us'])-int(self.window[0]['start_us']))+(int(w['end_us'])-int(self.window[0]['start_us'])))*.5e-6 for w in self.window]
        mean_t=sum(t)/len(t);den=sum((x-mean_t)**2 for x in t)
        self.peak_deg=self.drift_deg_s=0.
        for i in range(3,44):
            values=[q[i] for q in self.angles];mean=sum(values)/len(values)
            self.peak_deg=max(self.peak_deg,math.degrees(max(values)-min(values)))
            self.drift_deg_s=max(self.drift_deg_s,math.degrees(abs(sum((x-mean_t)*(y-mean) for x,y in zip(t,values))/den)))
        if self.peak_deg>.3+1e-9 or self.drift_deg_s>.2+1e-9:return False
        if elapsed-(start-req)>200000:raise ValueError('STALE_FINAL_SCAN')
        self.final=dict(p);self.stable_us=span;self.state='SnapshotReady';return True
