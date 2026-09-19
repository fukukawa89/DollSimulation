import socket
import time
import pytest
from posedoll_sim.core import DeviceProfile,default_shared,FrameDecoder,encode,SensorDecoder
from posedoll_sim.transport import SensorServer


def receive(client,decoder,condition,timeout=2):
    end=time.monotonic()+timeout
    messages=[]
    while time.monotonic()<end:
        try:data=client.recv(65536)
        except socket.timeout:continue
        if not data:break
        messages.extend(decoder.feed(data))
        if condition(messages):return messages
    raise AssertionError('Timed out waiting for protocol messages')


def test_welcome_gating_rate_and_reconnect():
    p=DeviceProfile(default_shared())
    server=SensorServer(p,0);server.start()
    port=server._socket.getsockname()[1]
    try:
        for attempt in range(3):
            with socket.create_connection(('127.0.0.1',port)) as client:
                client.settimeout(.12);decoder=FrameDecoder()
                hello=receive(client,decoder,lambda rows:len(rows)==1)[0]
                assert hello['type']=='hello'
                with pytest.raises(socket.timeout):client.recv(1024)
                client.sendall(encode(dict(type='welcome',protocol=hello['protocol'],session_id=hello['session_id'],accepted=True,receiver='test',max_frame_bytes=65536)))
                start=time.monotonic()
                frames=receive(client,decoder,lambda rows:len(rows)>=30)
                duration=time.monotonic()-start
                assert .35<duration<1.5
                sensor=SensorDecoder(p);sensor.handshake(hello)
                for frame in frames:assert len(sensor.sample(frame))==44
                if attempt==0:server.faults.fragment=True
                if attempt==1:server.faults.coalesce=True
            server.restart()
        assert server.sent>=90
    finally:server.close()
    assert not server._thread.is_alive()


def test_faults_and_capabilities():
    p=DeviceProfile(default_shared());s=SensorServer(p)
    s.faults.missing=True
    i=p.index[s.faults.axis]
    sample=s.snapshot();assert sample['raw_angles_rad'][i] is None and sample['axis_status'][i]=='missing'
    s.faults.missing=False;s.faults.stuck=True
    before=s.snapshot()['raw_angles_rad'][i]
    s.q[s.faults.axis]=1.0
    assert s.snapshot()['raw_angles_rad'][i]==before
    s.body35=True
    assert s.snapshot()['axis_status'].count('fixed')==9


def test_stale_sequence_injection_recovers_without_restarting():
    p=DeviceProfile(default_shared());s=SensorServer(p)
    decoder=SensorDecoder(p);decoder.handshake(p.hello(s.session))
    for _ in range(4):decoder.sample(s.snapshot())
    s.faults.stale_sequence=True
    rejected=s.snapshot()
    assert int(rejected['sequence'])<4
    with pytest.raises(ValueError):decoder.sample(rejected)
    assert s.snapshot()['sequence']==rejected['sequence']
    s.faults.stale_sequence=False
    assert len(decoder.sample(s.snapshot()))==44
