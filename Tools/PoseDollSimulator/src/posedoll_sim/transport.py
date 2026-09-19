from __future__ import annotations

import json
import math
import random
import socket
import threading
import time
import uuid
from dataclasses import dataclass

from .core import FrameDecoder, IDENTITY_FIELDS, encode


@dataclass
class Faults:
    axis: str = 'elbow_l.flex'
    noise_degrees: float = 0.0
    bias_degrees: float = 0.0
    quantum_degrees: float = 0.0
    missing: bool = False
    stuck: bool = False
    paused: bool = False
    duplicate_sequence: bool = False
    stale_sequence: bool = False
    fragment: bool = False
    coalesce: bool = False


class SensorServer:
    """One loopback client, bounded sends, welcome-gated snapshots; no editor commands."""
    def __init__(self, profile, port=39177):
        self.profile = profile
        self.port = port
        self.lock = threading.Lock()
        self.q = dict.fromkeys(profile.order, 0.0)
        self.faults = Faults()
        self.body35 = False
        self.session = str(uuid.uuid4())
        self.sequence = 0
        self.state = '未启动'
        self.error = ''
        self.sent = 0
        self.latest = None
        self._stuck_raw = None
        self._stuck_axis = None
        self._stale_sequence = None
        self._restart = threading.Event()
        self._stop = threading.Event()
        self._thread = None
        self._socket = None
        self._record = None

    def start(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        listener.bind(('127.0.0.1', self.port))
        listener.listen(1)
        listener.settimeout(.2)
        self._socket = listener
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name='PoseDollSensorServer', daemon=False)
        self._thread.start()

    def close(self):
        self._stop.set()
        if self._thread:
            self._thread.join(3)
            if self._thread.is_alive():
                raise RuntimeError('Sensor server did not stop within timeout')
        if self._socket:
            self._socket.close()
        self.stop_recording()

    def restart(self):
        with self.lock:
            self.session = str(uuid.uuid4())
            self.sequence = 0
            self._stuck_raw = None
            self._stale_sequence = None
        self._restart.set()

    def set_pose(self, angles):
        if set(angles) != set(self.profile.order):
            raise ValueError('Incomplete pose')
        with self.lock:
            self.q = dict(angles)

    def start_recording(self, path):
        with self.lock:
            if self._record:
                raise ValueError('Recording already active')
            self._record = open(path, 'x', encoding='utf-8')
            self._record.write(json.dumps({'type':'recording_header','hello':self.profile.hello(self.session,self.body35)},ensure_ascii=False)+'\n')

    def stop_recording(self):
        with self.lock:
            if self._record:
                self._record.close()
                self._record = None

    def snapshot(self):
        with self.lock:
            raw, states = self.profile.encode_angles(self.q, self.body35)
            f = self.faults
            index = self.profile.index[f.axis]
            if raw[index] is not None:
                value = raw[index] + math.radians(f.bias_degrees + random.gauss(0, f.noise_degrees))
                quantum = math.radians(f.quantum_degrees)
                if quantum > 0:
                    value = round(value / quantum) * quantum
                value %= self.profile.cal[f.axis]['raw_period_rad']
                if f.stuck:
                    if self._stuck_raw is None or self._stuck_axis != f.axis:
                        self._stuck_raw, self._stuck_axis = value, f.axis
                    value = self._stuck_raw
                else:
                    self._stuck_raw = None
                raw[index] = value
                if f.missing:
                    raw[index], states[index] = None, 'missing'
            if not f.duplicate_sequence or self.sequence == 0:
                self.sequence += 1
            emitted_sequence = self.sequence
            if f.stale_sequence:
                if self._stale_sequence is None:
                    self._stale_sequence = max(0,self.sequence-2)
                emitted_sequence = self._stale_sequence
            else:
                self._stale_sequence = None
            hello = self.profile.hello(self.session, self.body35)
            message = {key:hello[key] for key in IDENTITY_FIELDS}
            message.update(type='sample',sequence=str(emitted_sequence),sender_monotonic_us=str(time.monotonic_ns()//1000),raw_angles_rad=raw,axis_status=states)
            self.latest = message
            if self._record:
                self._record.write(json.dumps(message,ensure_ascii=False,allow_nan=False)+'\n')
            return message

    def _run(self):
        while not self._stop.is_set():
            self.state = '监听 127.0.0.1:%d' % self.port
            try:
                client, _ = self._socket.accept()
            except socket.timeout:
                continue
            except OSError as exc:
                self.error = str(exc)
                break
            with client:
                try:
                    self._restart.clear()
                    client.settimeout(.25)
                    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    with self.lock:
                        hello = self.profile.hello(self.session,self.body35)
                    client.sendall(encode(hello))
                    self.state = '等待 UE 握手'
                    decoder = FrameDecoder()
                    deadline = time.monotonic()+3
                    accepted = False
                    while not self._stop.is_set() and time.monotonic()<deadline:
                        try:
                            data = client.recv(4096)
                        except socket.timeout:
                            continue
                        if not data:
                            raise ConnectionError('连接已关闭')
                        for reply in decoder.feed(data):
                            if reply.get('type') == 'reject':
                                raise ValueError('UE 拒绝: '+str(reply.get('code')))
                            if reply.get('type') != 'welcome' or reply.get('session_id') != hello['session_id'] or reply.get('protocol') != hello['protocol'] or reply.get('accepted') is not True:
                                raise ValueError('握手响应无效')
                            accepted = True
                        if accepted:
                            break
                    if not accepted:
                        raise TimeoutError('握手超时')
                    self.state, self.error = '已连接 · 60 Hz', ''
                    tick = time.monotonic()
                    while not self._stop.is_set() and not self._restart.is_set():
                        batch_size = 2 if self.faults.coalesce else 1
                        if not self.faults.paused:
                            packet = b''.join(encode(self.snapshot()) for _ in range(batch_size))
                            if self.faults.fragment:
                                client.sendall(packet[:2])
                                client.sendall(packet[2:17])
                                client.sendall(packet[17:])
                            else:
                                client.sendall(packet)
                            self.sent += batch_size
                        tick += batch_size/60
                        now = time.monotonic()
                        if tick < now:
                            tick = now  # Drop missed periods instead of replaying a backlog.
                        self._stop.wait(max(0,tick-now))
                except (OSError,ValueError,ConnectionError) as exc:
                    self.error = str(exc)
        self.state = '已停止'
