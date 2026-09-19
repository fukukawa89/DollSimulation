from __future__ import annotations

import hashlib
import json
import math
import struct
from pathlib import Path

import numpy as np

PROTOCOL = "posedoll.sensor/1"
MAX_FRAME = 65536
MAX_BUFFER = 2 * (MAX_FRAME + 4)
IDENTITY_FIELDS = ("protocol", "device_id", "session_id", "profile_id", "profile_sha256",
                   "calibration_id", "calibration_sha256")
SAMPLE_FIELDS = set(IDENTITY_FIELDS) | {"type", "sequence", "sender_monotonic_us",
                                       "raw_angles_rad", "axis_status"}
HELLO_FIELDS = set(IDENTITY_FIELDS) | {"type", "axis_order", "capability_id",
                                      "nominal_sample_hz", "source_kind"}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON field: " + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("Non-finite JSON: " + value)
    value = json.loads(data, object_pairs_hook=pairs, parse_constant=invalid)
    def check(v, depth=0):
        if depth > 16:
            raise ValueError("JSON nesting budget exceeded")
        if isinstance(v, dict):
            for x in v.values():
                check(x, depth + 1)
        elif isinstance(v, list):
            for x in v:
                check(x, depth + 1)
        elif isinstance(v, float) and not math.isfinite(v):
            raise ValueError("Non-finite number")
    check(value)
    return value


def encode(message):
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if not 0 < len(payload) <= MAX_FRAME:
        raise ValueError("Frame size outside 1..65536")
    return struct.pack(">I", len(payload)) + payload


class FrameDecoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        if len(data) + len(self.buffer) > MAX_BUFFER:
            raise ValueError("Receive buffer budget exceeded")
        self.buffer.extend(data)
        result = []
        while len(self.buffer) >= 4:
            size, = struct.unpack_from(">I", self.buffer)
            if not 0 < size <= MAX_FRAME:
                raise ValueError("Frame size outside 1..65536")
            if len(self.buffer) < size + 4:
                break
            message = strict_json(bytes(self.buffer[4:4 + size]).decode("utf-8", errors="strict"))
            del self.buffer[:4 + size]
            if not isinstance(message, dict):
                raise ValueError("JSON root must be an object")
            result.append(message)
            if len(result) > 256:
                raise ValueError("Message budget exceeded")
        return result


def uint64(text):
    if not isinstance(text, str) or not text.isascii() or not text.isdecimal():
        raise ValueError("uint64 must be an ASCII decimal string")
    if len(text) > 20 or (len(text) > 1 and text[0] == "0"):
        raise ValueError("Non-canonical uint64")
    value = int(text)
    if value > 2**64 - 1:
        raise ValueError("uint64 overflow")
    return value


def quaternion_matrix(q):
    q = np.asarray(q, dtype=float)
    if q.shape != (4,) or not np.isfinite(q).all() or abs(np.linalg.norm(q) - 1) > 1e-7:
        raise ValueError("Quaternion must be finite and normalized")
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def rigid(spec):
    result = np.eye(4)
    result[:3, :3] = quaternion_matrix(spec["rotation_xyzw"])
    translation = np.asarray(spec["translation_m"], dtype=float)
    if translation.shape != (3,) or not np.isfinite(translation).all():
        raise ValueError("Invalid translation")
    result[:3, 3] = translation
    return result


def rotate(axis, angle):
    result = np.eye(4)
    result[:3, :3] = quaternion_matrix([*(np.asarray(axis) * math.sin(angle / 2)), math.cos(angle / 2)])
    return result


class DeviceProfile:
    def __init__(self, shared: Path):
        self.shared = Path(shared)
        directory = self.shared / "Profiles"
        pb = (directory / "virtual_humanoid_44_v1.json").read_bytes()
        cb = (directory / "virtual_zero_pi_v1.json").read_bytes()
        self.data, self.calibration = strict_json(pb), strict_json(cb)
        self.capability = strict_json((directory / "body35_capabilities.json").read_bytes())
        self.profile_hash = hashlib.sha256(pb).hexdigest()
        self.calibration_hash = hashlib.sha256(cb).hexdigest()
        self.order = self.data["axis_order"]
        self.axes = {axis["id"]: axis for axis in self.data["axes"]}
        self.cal = {axis["axis_id"]: axis for axis in self.calibration["axes"]}
        self.nodes = self.data["nodes"]
        self.index = {aid: index for index, aid in enumerate(self.order)}
        self.fixed = self.capability["fixed_axis_values_rad"]
        self.validate()
        self.pre = {n["id"]: rigid(n["parent_to_axis"]) for n in self.nodes}
        self.post = {n["id"]: rigid(n["axis_to_child"]) for n in self.nodes}

    def validate(self):
        order = set(self.order)
        if len(order) != 44 or len(self.order) != 44 or order != set(self.axes) or order != set(self.cal):
            raise ValueError("Expected 44 unique matching channels")
        seen, channels = set(), set()
        for node in self.nodes:
            if node["id"] in seen or (node["parent"] is not None and node["parent"] not in seen):
                raise ValueError("Duplicate node, missing parent, cycle or unordered tree")
            seen.add(node["id"])
            rigid(node["parent_to_axis"])
            rigid(node["axis_to_child"])
            if node["kind"] == "revolute":
                aid = node["axis_id"]
                if aid not in order or aid in channels or abs(np.linalg.norm(node["axis_local"]) - 1) > 1e-8:
                    raise ValueError("Invalid mechanical axis")
                channels.add(aid)
            elif node["kind"] != "fixed":
                raise ValueError("Unknown node kind")
        if channels != order:
            raise ValueError("Unbound channel")
        for aid in self.order:
            lo, hi = self.axes[aid]["limits_rad"]
            c = self.cal[aid]
            if not finite(lo) or not finite(hi) or lo >= hi:
                raise ValueError("Invalid limits")
            if c["sign"] not in (-1, 1) or not finite(c["joint_rad_per_sensor_rad"]) or c["joint_rad_per_sensor_rad"] <= 0:
                raise ValueError("Invalid calibration")
            if not finite(c["raw_period_rad"]) or c["raw_period_rad"] <= 0 or not finite(c["zero_raw_rad"]):
                raise ValueError("Invalid period or zero")
        if set(self.fixed) | set(self.capability["measured_axis_ids"]) != order or set(self.fixed) & set(self.capability["measured_axis_ids"]):
            raise ValueError("Incomplete capabilities")

    def fk(self, angles):
        if set(angles) != set(self.order) or not all(finite(v) for v in angles.values()):
            raise ValueError("FK requires a complete finite angle snapshot")
        result = {}
        for node in self.nodes:
            rotation = np.eye(4) if node["kind"] == "fixed" else rotate(node["axis_local"], angles[node["axis_id"]])
            parent = np.eye(4) if node["parent"] is None else result[node["parent"]]
            result[node["id"]] = parent @ self.pre[node["id"]] @ rotation @ self.post[node["id"]]
        return result

    def encode_angles(self, angles, body35=False):
        if set(angles) != set(self.order):
            raise ValueError("Incomplete pose")
        raw, status = [], []
        for aid in self.order:
            if body35 and aid in self.fixed:
                raw.append(None)
                status.append("fixed")
            else:
                c = self.cal[aid]
                value = angles[aid]
                if not finite(value):
                    raise ValueError("Invalid angle")
                raw.append((c["zero_raw_rad"] + value / (c["sign"] * c["joint_rad_per_sensor_rad"])) % c["raw_period_rad"])
                status.append("valid")
        return raw, status

    def hello(self, session, body35=False):
        return dict(protocol=PROTOCOL, type="hello", device_id="virtual-doll-001", session_id=session,
                    profile_id=self.data["profile_id"], profile_sha256=self.profile_hash,
                    calibration_id=self.calibration["calibration_id"], calibration_sha256=self.calibration_hash,
                    axis_order=self.order, capability_id=self.capability["capability_id"] if body35 else "full44",
                    nominal_sample_hz=60, source_kind="simulator")


class SensorDecoder:
    """Atomic continuous unwrap, limits and time-based filtering; never substitutes missing axes."""
    def __init__(self, profile, filter_seconds=0.0, max_speed=40.0):
        self.profile = profile
        self.tau = filter_seconds
        self.max_speed = max_speed
        self.reset()

    def reset(self):
        self.hello = None
        self.previous_raw = {}
        self.unwrapped = {}
        self.filtered = {}
        self.sequence = -1
        self.timestamp = None

    def handshake(self, hello):
        p = self.profile
        if set(hello) != HELLO_FIELDS or hello.get("type") != "hello":
            raise ValueError("HelloFields")
        if hello["protocol"] != PROTOCOL:
            raise ValueError("ProtocolMismatch")
        if hello["profile_id"] != p.data["profile_id"]:
            raise ValueError("ProfileUnknown")
        if hello["profile_sha256"] != p.profile_hash:
            raise ValueError("ProfileHashMismatch")
        if hello["calibration_id"] != p.calibration["calibration_id"] or hello["calibration_sha256"] != p.calibration_hash:
            raise ValueError("CalibrationMismatch")
        if hello["axis_order"] != p.order:
            raise ValueError("AxisOrderMismatch")
        if hello["capability_id"] not in ("full44", p.capability["capability_id"]):
            raise ValueError("CapabilitiesUnsupported")
        if not all(isinstance(hello[k], str) and 0 < len(hello[k]) <= 128 for k in ("device_id", "session_id", "source_kind")):
            raise ValueError("Invalid identity")
        if not finite(hello["nominal_sample_hz"]) or not 1 <= hello["nominal_sample_hz"] <= 240:
            raise ValueError("Invalid sample rate")
        self.reset()
        self.hello = dict(hello)

    def sample(self, sample):
        if self.hello is None or set(sample) != SAMPLE_FIELDS or sample["type"] != "sample":
            raise ValueError("SampleFields or handshake required")
        if any(sample[k] != self.hello[k] for k in IDENTITY_FIELDS):
            raise ValueError("Session identity changed")
        seq, timestamp = uint64(sample["sequence"]), uint64(sample["sender_monotonic_us"])
        if seq <= self.sequence or (self.timestamp is not None and timestamp <= self.timestamp):
            raise ValueError("Non-monotonic sample")
        p = self.profile
        raw, status = sample["raw_angles_rad"], sample["axis_status"]
        if len(raw) != 44 or len(status) != 44:
            raise ValueError("ChannelCount")
        dt = None if self.timestamp is None else (timestamp - self.timestamp) / 1e6
        next_raw, next_unwrapped, angles = {}, {}, {}
        for aid, value, state in zip(p.order, raw, status):
            if state == "fixed":
                if self.hello["capability_id"] == "full44" or aid not in p.fixed or value is not None:
                    raise ValueError("Undeclared fixed axis: " + aid)
                angles[aid] = p.fixed[aid]
                continue
            if state != "valid" or not finite(value):
                raise ValueError("Missing/invalid axis: " + aid)
            if self.hello["capability_id"] != "full44" and aid in p.fixed:
                raise ValueError("Fixed axis must be fixed: " + aid)
            c = p.cal[aid]
            period, gain = c["raw_period_rad"], c["sign"] * c["joint_rad_per_sensor_rad"]
            if not 0 <= value < period:
                raise ValueError("Raw range: " + aid)
            lo, hi = p.axes[aid]["limits_rad"]
            if aid not in self.previous_raw:
                base = value - c["zero_raw_rad"]
                bounds = sorted((lo / gain, hi / gain))
                first = math.ceil((bounds[0] - base - 1e-9) / period)
                last = math.floor((bounds[1] - base + 1e-9) / period)
                if first != last:
                    raise ValueError("Calibration branch ambiguous/out of limits: " + aid)
                unwrapped = base + first * period
            else:
                delta = (value - self.previous_raw[aid] + period / 2) % period - period / 2
                if dt is None or abs(delta * gain) > self.max_speed * dt + 1e-6:
                    raise ValueError("Angular speed: " + aid)
                unwrapped = self.unwrapped[aid] + delta
            q = unwrapped * gain
            if q < lo - 1e-8 or q > hi + 1e-8:
                raise ValueError("Joint limit: " + aid)
            next_raw[aid], next_unwrapped[aid] = value, unwrapped
            alpha = 1.0 if self.tau <= 0 or dt is None else -math.expm1(-dt / self.tau)
            angles[aid] = self.filtered.get(aid, q) + alpha * (q - self.filtered.get(aid, q))
        self.previous_raw, self.unwrapped, self.filtered = next_raw, next_unwrapped, angles
        self.sequence, self.timestamp = seq, timestamp
        return dict(angles)


def default_shared():
    return Path(__file__).resolve().parents[4] / "Shared"
