import copy
import json
import math
import struct

import numpy as np
import pytest

from posedoll_sim.core import DeviceProfile, FrameDecoder, SensorDecoder, default_shared, encode, uint64


@pytest.fixture
def profile():
    return DeviceProfile(default_shared())


def sample_for(p, q=None, seq=1, timestamp=1000000, body35=False):
    hello = p.hello('test-session', body35)
    raw, states = p.encode_angles(q or dict.fromkeys(p.order, 0.0), body35)
    sample = {k: hello[k] for k in ('protocol','device_id','session_id','profile_id','profile_sha256','calibration_id','calibration_sha256')}
    sample.update(type='sample', sequence=str(seq), sender_monotonic_us=str(timestamp), raw_angles_rad=raw, axis_status=states)
    return hello, sample


def test_golden_all_nodes(profile):
    cases = json.loads((profile.shared/'Fixtures/golden_vectors.json').read_text(encoding='utf-8'))['cases']
    for case in cases:
        pose = profile.fk(case['angles_rad'])
        for node, expected in case['source_base_matrices'].items():
            np.testing.assert_allclose(pose[node], expected, atol=1e-10)


def test_fixtures_calibration(profile):
    hello = json.loads((profile.shared/'Fixtures/hello.json').read_text(encoding='utf-8'))
    for path in (profile.shared/'Fixtures').glob('*.sample.json'):
        decoder = SensorDecoder(profile)
        decoder.handshake(hello)
        assert len(decoder.sample(json.loads(path.read_text(encoding='utf-8')))) == 44


def test_body35(profile):
    hello, sample = sample_for(profile, body35=True)
    decoder = SensorDecoder(profile)
    decoder.handshake(hello)
    assert sum(x == 'fixed' for x in sample['axis_status']) == 9
    assert len(decoder.sample(sample)) == 44


@pytest.mark.parametrize('mutation', ['missing','invalid','fixed','count','nan','unknown','sequence','overflow','identity'])
def test_bad_samples(profile, mutation):
    hello, sample = sample_for(profile)
    decoder = SensorDecoder(profile)
    decoder.handshake(hello)
    if mutation in ('missing','invalid','fixed'):
        sample['axis_status'][0], sample['raw_angles_rad'][0] = mutation, None
    elif mutation == 'count': sample['raw_angles_rad'].pop()
    elif mutation == 'nan': sample['raw_angles_rad'][0] = math.nan
    elif mutation == 'unknown': sample['extra'] = 1
    elif mutation == 'sequence': sample['sequence'] = 2
    elif mutation == 'overflow': sample['sequence'] = str(2**64)
    elif mutation == 'identity': sample['session_id'] = 'old-device'
    with pytest.raises(ValueError): decoder.sample(sample)
    assert decoder.sequence == -1


def test_wrap_and_atomic_rejection(profile):
    # Synthetic encoder zero is changed without editing the canonical profile file.
    p = copy.deepcopy(profile)
    p.cal['pelvis.yaw']['zero_raw_rad'] = 0.0
    q = dict.fromkeys(p.order, 0.0)
    q['pelvis.yaw'] = -.01
    hello, sample = sample_for(p, q)
    d = SensorDecoder(p)
    d.handshake(hello)
    assert d.sample(sample)['pelvis.yaw'] == pytest.approx(-.01)
    bad = copy.deepcopy(sample)
    bad['sequence'] = '2'
    bad['sender_monotonic_us'] = '1016667'
    bad['axis_status'][-1], bad['raw_angles_rad'][-1] = 'missing', None
    with pytest.raises(ValueError): d.sample(bad)
    q['pelvis.yaw'] = .01
    _, good = sample_for(p, q, 3, 1033334)
    assert d.sample(good)['pelvis.yaw'] == pytest.approx(.01)


def test_packet_fragments_and_coalescing(profile):
    h,s = sample_for(profile)
    packet = encode(h)+encode(s)
    decoder = FrameDecoder()
    result = []
    for start in range(0,len(packet),7): result.extend(decoder.feed(packet[start:start+7]))
    assert result == [h,s]


@pytest.mark.parametrize('packet', [struct.pack('>I',0),struct.pack('>I',65537), b'\x00\x00\x00\x02\xff\xff', b'\x00\x00\x00\x02[]'])
def test_packet_rejection(packet):
    with pytest.raises((ValueError,UnicodeError)): FrameDecoder().feed(packet)


def test_uint64():
    assert uint64(str(2**64-1)) == 2**64-1
    for bad in ['01','-1','1.0','１２',str(2**64)]:
        with pytest.raises(ValueError): uint64(bad)


def test_handshake_rejects_wrong_hash(profile):
    hello,_ = sample_for(profile)
    hello['profile_sha256'] = '0'*64
    with pytest.raises(ValueError,match='ProfileHashMismatch'): SensorDecoder(profile).handshake(hello)


def test_tree_rejects_cycle(profile):
    profile.nodes[0]['parent'] = profile.nodes[-1]['id']
    with pytest.raises(ValueError,match='tree'): profile.validate()
