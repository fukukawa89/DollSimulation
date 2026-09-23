# PDS1/1 static protocol

This version is separate from PD41 and the existing live TCP schema. Local TCP port 39178; 39177 remains legacy Live. It is a software prototype, not a claim that O3 hardware firmware emits PDS1.

Framing: 4-byte unsigned big-endian JSON byte length, maximum 65536. JSON envelope has exactly type:"pds1", payload (a JSON string, UTF-8 <=32768 bytes), and crc32 (eight lowercase hexadecimal characters). CRC32/ISO-HDLC covers the exact UTF-8 bytes of the unparsed payload, compatible with zlib.crc32 and UE FCrc::MemCrc32. It is error detection, not authentication.

Payload common fields: protocol:"PDS1/1", device_id, gateway_boot, profile_sha256, calibration_sha256.
- hello adds type:"hello", axis_order (44 exact profile axis IDs), source_boots (N1...N6, optionally followed by D3,D4), source_kind:"simulated"|"hardware".
- After successful hello, UE sends the existing framed welcome object with type:"welcome", protocol:"PDS1/1", session_id equal to gateway_boot, accepted:true.
- UE request/cancel/ack adds type and capture_id, no extra payload fields.
- Source accepted reply adds type:"accepted", capture_id, request_start_us, source_boots.
- Source scan has those accepted fields, type:"scan", plus scan_id, start_us, end_us, duration_us, raw_angles_rad, axis_status.

All uint64 fields are canonical unsigned decimal strings, no leading zeros except "0"; duration_us is an integer 1...4294967295. Times refer to one source monotonic clock, not UTC. The 100000 us acceptance limit does not change the wire integer width. Start/end bracket fresh measurement; end-start must equal duration.

First three raw entries are null, status fixed, in pelvis.yaw/pitch/roll order. Remaining 41 entries must be finite calibrated-period raw angles and status valid to accept a snapshot. Missing or faulty scans invalidate the transaction. The final complete scan is preserved; no averaging across axis caches.

Each connection fixes device, gateway boot, ordered source boots, profile and calibration hashes. A changed source requires reconnect/rejoin and a new capture. Within a capture, scan IDs begin at 1 and increase exactly by one. Duplicate/stale/overlapping scans are rejected. The source does not stream when idle. ACK/cancel stops active acquisition; requesting an already pending ID is idempotent. Session request history is bounded.

A capture is confirmed after a common observation >=500000 us, >=6 scans, <=0.30 degree peak-to-peak and <=0.20 degree/s least-squares drift on every measured axis. Absolute joint-limit branch is recomputed from each scan without historical winding guesses. Unknown multi-turn branch is rejected. Observation history <=32 scans, incoming messages <=32, outgoing commands <=8. Observation gaps >150000 us restart the window. Wall deadline is 3 seconds, including main-thread queue delay.

To avoid assuming clock synchronization, UE bounds final age by:
(UE_now - UE_request_send) - (source_start - source_request_start).
This is conservative because request transport time is included. Reject an interval whose source end lies >5ms beyond elapsed UE time. Device clock rate accuracy and trustworthy source timestamp placement remain hardware obligations.

PDR4, the candidate satellite byte codec, is distinct. Its request/response sizes are 68/80 bytes. Host C and Python crosschecks do not implement the gateway CAN coordinator, join challenge or STM32 drivers. Boot/reset/retry/power behavior must be qualified before hardware deployment.
