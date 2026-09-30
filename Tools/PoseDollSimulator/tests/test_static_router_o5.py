import copy
import pytest
from posedoll_sim.core import FrameDecoder, encode
from posedoll_sim.static_protocol import StaticRouter, StaticWindow, envelope
from test_static import make_case

@pytest.mark.parametrize('order', ['before', 'after', 'mixed'])
@pytest.mark.parametrize('fragment', [False, True])
def test_cancel_then_new_request(order, fragment):
    p,h,cid,rows=make_case(); router=StaticRouter(p,h)
    router.begin('retired',99);router.retire('retired',99.5);router.begin(cid,100)
    old=[copy.deepcopy(x[0]) for x in rows[:2]]
    for msg in old:msg['capture_id']='retired'
    ack=rows[0][0]
    seq=old+[ack] if order=='before' else [ack]+old if order=='after' else [old[0],ack,old[1]]
    seq += [msg for msg,t in rows[1:8]]
    decoder=FrameDecoder();window=StaticWindow(p,h,cid,100)
    # At each scan arrival preserve its original wall time; fragment and coalesce ACK/old traffic.
    groups=[(seq[:3],100.021)]+[([msg],t) for msg,t in rows[1:8]]
    for messages,now in groups:
        wire=b''.join(encode(envelope(msg)) for msg in messages)
        chunks=[wire[i:i+11] for i in range(0,len(wire),11)] if fragment else [wire]
        for chunk in chunks:
            for message in decoder.feed(chunk):
                accepted=router.route(message,now)
                if accepted is not None:window.push(accepted,now)
    assert window.final['capture_id']==cid and router.dropped==2
    router.retire(cid,100.8)
    assert router.route(envelope(rows[7][0]),100.9) is None

@pytest.mark.parametrize('fault',['unknown','crc','boot','profile','fields','raw','duration'])
def test_retired_is_not_a_validation_bypass(fault):
    p,h,cid,rows=make_case();r=StaticRouter(p,h);r.begin(cid,99);r.retire(cid,99.5);r.begin('next',100)
    m=copy.deepcopy(rows[1][0])
    if fault=='unknown':m['capture_id']='unknown'
    if fault=='boot':m['source_boots'][0]='restart'
    if fault=='profile':m['profile_sha256']='bad'
    if fault=='fields':m['extra']=1
    if fault=='raw':m['raw_angles_rad'][3]=None
    if fault=='duration':m['duration_us']=2**32
    wire=envelope(m)
    if fault=='crc':wire['crc32']='00000000'
    with pytest.raises(ValueError):r.route(wire,100.1)
    assert r.dropped==0

def test_retired_bound_expiry_and_reconnect():
    p,h,cid,rows=make_case();r=StaticRouter(p,h)
    for i in range(40):r.begin(str(i),100+i*.01);r.retire(str(i),100+i*.01)
    assert len(r.retired)==32 and '0' not in r.retired
    r.begin(cid,101);r.retire(cid,101);r.retire(cid,125)
    with pytest.raises(ValueError):r.route(envelope(rows[0][0]),131)
    r=StaticRouter(p,h)
    with pytest.raises(ValueError):r.route(envelope(rows[0][0]),101)
