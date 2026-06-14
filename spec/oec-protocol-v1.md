# OEconnect Wire Protocol — v1.1

**Status:** Frozen for v1.x. Layout changes require a major version bump.
**Authoritative for:** frame layout, ringbuffer layout, ZMQ socket conventions, command/ack codes.
**CI enforcement:** changes to this file without a `version_major` or `version_minor` bump in §1 are rejected.

## 1. Versions

- Wire spec: 1.1
- Wire frame `version_major`: 1
- Wire frame `version_minor`: 1

(Bump the version above before merging any change to §3 (frame layout) or §4 (stream IDs).)

---

## 2. Frame

### 2.1 Frame header (32 B, packed, little-endian)

```c
struct oec_frame_header {
    uint32_t magic;             // 'O','E','C','1' = 0x3143454F
    uint8_t  version_major;     // bump on layout-breaking change
    uint8_t  version_minor;     // bump on additive change
    uint16_t stream_id;         // see §3
    uint32_t payload_len;       // bytes immediately after header
    uint64_t sample_index;      // FPGA sample counter, authoritative
    uint64_t host_qpc_ticks;    // QueryPerformanceCounter at frame creation
    uint16_t flags;             // bit 0 = CONTINUATION, bit 1 = LOST_DATA
    uint16_t crc16;             // optional; zero if disabled
};
```

### 2.2 Command IDs (`CMD.cmd_id`)

| Code  | Name              | Body                                                  | Class |
|-------|-------------------|-------------------------------------------------------|-------|
| 0x0001 | `START_RECORD`   | `{utf8_len_u16, dir[], utf8_len_u16, prefix[]}`       | slow  |
| 0x0002 | `STOP_RECORD`    | —                                                     | slow  |
| 0x0003 | `SET_TTL`        | `{line_u8, edge_u8}`                                  | fast  |
| 0x0004 | `PULSE_TTL`      | `{line_u8, edge_u8, width_us_u32}`                    | fast  |
| 0x0005 | `START_ACQ`      | —                                                     | slow  |
| 0x0006 | `STOP_ACQ`       | —                                                     | slow  |
| 0x0007 | `GET_STATE`      | —                                                     | fast  |

### 2.3 Status codes (`ACK.status`)

`OK=0, PENDING=1, COMPLETED=2, BUSY=3, NOT_SUPPORTED=4, BAD_ARG=5, TIMEOUT=6, INTERNAL=7`

### 2.4 Block sizing

OE plugin block defaults to **32 samples** per `process()` callback ≈ 1.07 ms
at 30 kHz. One `RAW_BLOCK` frame per callback. At 256 ch: 32 × 256 × 2 B =
16 KB payload + 32 B header. Comfortably under one 64 KB ringbuf slot.

### 2.5 Dual-clock sync

Every frame's header carries `(sample_index, host_qpc_ticks)`. Once per second
the plugin also emits a `SYNC` frame carrying `qpc_freq_hz` and
`fpga_sample_rate_hz`. The Bonsai side keeps a 60-point ring of `(sample,
qpc)` pairs and runs sliding-window weighted least-squares regression to
maintain `qpc = a · sample + b`. Drift correction surfaces in user code as
`SampleToHostTime(sample_index) → DateTimeOffset`.

Fit divergence (residual RMS > 5 µs) resets the window and triggers a
`LOST_DATA`-style telemetry counter, but no `OnError` — drift glitches are
expected during USB hiccups.

### 2.6 Frame versioning behaviour

`version_major` mismatch ⇒ consumer drops the frame and raises
`ERROR(code=PROTOCOL_VERSION_MISMATCH)`. `version_minor` mismatch ⇒ consumer
accepts and treats unknown stream IDs / flags as opaque.

The authoritative source is `spec/oec-protocol-v1.md` (this document). Any
frame layout change requires a PR to that file plus a version bump; CI
rejects frame-layout diffs that don't bump the version. See §7 for the
project-wide versioning policy.

## 3. Stream IDs

### 3.1 Stream ID table

| ID    | Name             | Payload                                                                      |
|-------|------------------|------------------------------------------------------------------------------|
| 0x01  | `RAW_BLOCK`      | `block_subheader` (8 B) + `int16[n_samples × n_channels]`, chan-major (planar) |
| 0x02  | `FILTERED_BLOCK` | same shape as `RAW_BLOCK`; filter info conveyed via header flags + most recent SYNC |
| 0x03  | `SPIKE`          | `{electrode_u16, unit_u16, threshold_f32, waveform_int16[N]}`                |
| 0x04  | `TTL_EVENT`      | `{line_u8, edge_u8, board_id_u8, _pad_u8}`                                   |
| 0x10  | `SYNC`           | `{qpc_freq_hz_u64, fpga_sample_rate_hz_f64, stream_meta[n]}` — periodic heartbeat (see §3.2) |
| 0x20  | `CMD`            | `{cmd_id_u16, cookie_u32, body…}` (Bonsai → OE)                              |
| 0x21  | `ACK`            | `{cookie_u32, status_u16, body…}` (OE → Bonsai)                              |
| 0x22  | `ERROR`          | `{code_u16, utf8_len_u16, utf8_msg[…]}`                                      |

### 3.2 `block_subheader` (RAW_BLOCK / FILTERED_BLOCK)

Self-describes the block so the consumer never has to "remember" channel
geometry across frames. 8 bytes, packed:

```c
struct oec_block_subheader {
    uint16_t n_channels;
    uint16_t n_samples;
    uint8_t  dtype;             // 0 = int16, 1 = float32, 2 = int32 (reserved)
    uint8_t  source_id;         // identifies which OE source node produced it
    uint16_t reserved;
};
```

`SYNC` frames additionally carry an array of
`{source_id_u8, n_channels_u16, sample_rate_hz_f64}` triples — one per
active source — so a late-joining subscriber can reconstruct the layout
without waiting for the next RAW frame.

## 4. Shared-memory region

### 4.1 Region

```
region name:   Local\oeconnect.<oe_pid>.shm                (Windows global ns)
               /oeconnect.<oe_pid>.shm                     (POSIX shm_open)

layout (cache-line = 64 B):
+0x0000  region_header        (4 KiB)
+0x1000  data_producer_idx    (64 B, atomic u64)
+0x1040  data_consumer_idx    (64 B, atomic u64)
+0x1080  data_ring            (slot_count × slot_size)
+......  cmd_producer_idx     (64 B)
+......  cmd_consumer_idx     (64 B)
+......  cmd_ring             (cmd_slot_count × cmd_slot_size)
+......  ack_producer_idx     (64 B)
+......  ack_consumer_idx     (64 B)
+......  ack_ring             (ack_slot_count × ack_slot_size)
```

### 4.2 `region_header`

```c
struct oec_region_header {
    uint32_t magic;                      // 'O','E','C','R'
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t slot_size;                  // default 65536  (64 KiB)
    uint32_t slot_count;                 // default 256
    uint32_t cmd_slot_size;              // default 4096
    uint32_t cmd_slot_count;             // default 64
    uint32_t ack_slot_size;              // default 4096
    uint32_t ack_slot_count;             // default 64
    uint64_t producer_heartbeat_ns;      // bumped each SYNC tick
    uint64_t fpga_sample_rate_hz_x1000;
    uint64_t reserved[…];                // pad to 4 KiB
};
```

### 4.3 SPSC algorithm (Vyukov-style)

- Producer loads `consumer_idx` with **acquire**, checks
  `producer_idx − consumer_idx < slot_count`. Full ⇒ advance both indices
  (drop oldest) and set `BIT_LOST_DATA` on next published frame. Writes
  payload into `ring[producer_idx mod slot_count]`. Stores
  `producer_idx + 1` with **release**.
- Consumer mirrors.
- No locks, no syscalls on the hot path. Producer and consumer indices live
  on independent cache lines to avoid false sharing.

Fixed slot size keeps the algorithm trivial. Frames larger than `slot_size`
set `BIT_CONTINUATION` and span up to 4 contiguous slots; over that ⇒
`ERROR(FRAME_TOO_LARGE)`.

### 4.4 Wakeups (optional)

| OS      | Mechanism                                        |
|---------|--------------------------------------------------|
| Windows | named EVENT `Local\oeconnect.<pid>.data_evt`     |
| Linux   | `eventfd`                                        |
| macOS   | `kqueue`                                         |

Producer signals every K frames (default K = 1; raise to batch under load).
Consumer chooses: blocking wait (power-efficient) or adaptive spin
(100 µs busy-loop, then block) — default is adaptive.

### 4.5 Three rings, one producer per ring

| Ring        | Producer            | Consumer        | SPSC | Notes                                                              |
|-------------|---------------------|-----------------|------|--------------------------------------------------------------------|
| `data_ring` | audio thread        | Bonsai          | yes  | raw / filtered / spikes / TTL events / SYNC                        |
| `cmd_ring`  | Bonsai              | audio thread    | yes  | drained at top of `process()`                                       |
| `ack_ring`  | audio thread (only) | Bonsai          | yes  | populated from in-process `AckOutbox` MPSC queue (plugin-internal)  |

### 4.6 Discovery

Plugin writes a sidecar JSON to a well-known dir on startup, deletes it on
clean shutdown:

```
%TEMP%\oeconnect\sessions\<oe_pid>.json    (Windows)
/tmp/oeconnect/sessions/<oe_pid>.json      (POSIX)

{
  "pid": 18432,
  "shm_region": "Local\\oeconnect.18432.shm",
  "data_event":  "Local\\oeconnect.18432.data_evt",
  "cmd_event":   "Local\\oeconnect.18432.cmd_evt",
  "zmq_fallback_endpoint": "tcp://127.0.0.1:5557",
  "zmq_cmd_endpoint":      "tcp://127.0.0.1:5558",
  "spec_version": "1.0",
  "started_unix_ns": 1717900000000000000
}
```

Bonsai source ops with empty `Endpoint` scan this directory, pick the newest
live session (`producer_heartbeat_ns` fresh within 5 s), prefer shmem when
same-OS, fall to ZMQ otherwise.

### 4.7 Sizing defaults (configurable from editor)

| Knob             | Default  | Rationale                                                  |
|------------------|----------|------------------------------------------------------------|
| `slot_size`      | 64 KiB   | Holds 32 samp × 1024 ch × int16 + header                    |
| `slot_count`     | 256      | 16 MiB → ~1 s buffer at 60 MB/s worst case                  |
| `wakeup_batch_K` | 1        | One wakeup per frame; raise if scheduler thrash             |
| `cmd_slot_count` | 64       | Sparse — start/stop/TTL                                     |
| `ack_slot_count` | 64       | Matches command volume                                      |

## 5. ZMQ transport

Same frame, different pipe. Soft-RT tier: 1–5 ms typical, 5–20 ms tail under
Windows scheduler. Activated when shmem is unavailable (cross-OS or LAN) or
explicitly selected in the editor.

### 5.1 Libraries

| Side     | Library                                       |
|----------|-----------------------------------------------|
| C++ plugin | `libzmq` linked statically + `cppzmq` headers |
| .NET Bonsai package | NetMQ (pure-managed; no native dep dragged into NuGet) |

### 5.2 Sockets

| Endpoint              | Pattern    | Direction          | Default port |
|-----------------------|------------|--------------------|--------------|
| `tcp://*:5557`        | PUB / SUB  | data (OE → Bonsai) | 5557         |
| `tcp://*:5558`        | REP / REQ  | commands           | 5558         |

`ipc://` and `inproc://` use the same scheme; endpoint string becomes
`ipc:///tmp/oeconnect/<pid>.data` (POSIX) or
`ipc://%TEMP%\oeconnect\<pid>.data` (Windows, mapped to a named pipe).

### 5.3 Topic filter — 2-byte ZMQ multipart prefix

Each data message is sent as two ZMQ frames:

```
[frame 0]  uint16 stream_id (LE)             ← topic; sub filter matches this
[frame 1]  oec_frame_header + payload        ← exact same bytes as shmem path
```

Bonsai source ops only care about specific streams (e.g. only `TTL_EVENT`)
subscribe with `socket.Subscribe(BitConverter.GetBytes((ushort)0x0004))`.
Filter happens in-broker on `tcp://`, in-kernel on `ipc://`.

**REQ/REP traffic uses a single ZMQ message** = the bare
`oec_frame_header + payload` bytes. No topic prefix. The control socket is
point-to-point so filtering is meaningless.

### 5.4 Backpressure

`ZMQ_SNDHWM = 1024` on PUB. Drops oldest on overflow (cannot block the OE
audio thread). Plugin tracks drop count and surfaces it on the next `SYNC`
frame + sets `BIT_LOST_DATA` on the next user frame.

`ZMQ_RCVHWM = 4096` on SUB — generous so Bonsai workflows can stall briefly.

### 5.5 Control channel ordering

REQ/REP is strictly synchronous. One command in flight at a time per Bonsai
instance. ACK comes back via REP socket payload (NOT the data PUB stream),
keeping control traffic off the high-rate channel. Concurrent commands from
multiple subscribers are serialised by the plugin; the loser gets
`ACK(status=BUSY)`.

### 5.6 Heartbeat

`ZMQ_HEARTBEAT_IVL = 500 ms`, `ZMQ_HEARTBEAT_TIMEOUT = 2000 ms` on the
control socket. Disconnect triggers Bonsai source `OnError`.

### 5.7 Security (off by default)

ZMQ CURVE keypair on both ends. Editor exposes "Require auth" checkbox and a
public-key field. Off ⇒ open localhost dev. On ⇒ mandatory whenever binding
to anything but `127.0.0.1`.

## 6. Hard architectural rules

1. **Bonsai must never talk directly to acquisition-board firmware.** Every
   hardware command and every hardware-line read crosses the OEconnect
   plugin. This preserves a single source of truth for the event stream: the
   OE recording remains a complete sanity-check copy of every TTL the bridge
   ever issued. A "fast direct path" from Bonsai to the FPGA is **never** an
   acceptable optimisation, even if it would shave latency.
2. **The audio thread is the only writer of the three shmem rings.** Any
   helper thread emitting toward Bonsai funnels through `AckOutbox`.
3. **Frame layout changes require a spec version bump.** CI enforces.

## 7. Versioning

SemVer aligned with `spec/oec-protocol-v1.md` major/minor. Plugin, NuGet,
and `liboeconnect` all carry the same version. Frame `version_major`
mismatch ⇒ frame dropped + `ERROR(PROTOCOL_VERSION_MISMATCH)`.
`version_minor` mismatch ⇒ forward-compatible accept.

## 8. HELLO handshake

Added in protocol v1.1. Lets producer and consumer announce their
protocol/version up-front so future minor bumps can be negotiated without
breaking older clients.

### 8.1 Stream id

`OEC_STREAM_HELLO = 0x0030`.

### 8.2 Body layout (16 B, packed, little-endian)

```c
struct oec_hello_body {
    uint16_t protocol_major;    // 1
    uint16_t protocol_minor;    // 1 in this release
    uint32_t plugin_version;    // (major<<16) | (minor<<8) | patch
    uint32_t lib_version;       // same packing
    uint32_t reserved;          // zero on emit; ignored on receive
};
```

### 8.3 Timing

Producer SHOULD emit one HELLO on the data ring at `startAcquisition`,
before any `RAW_BLOCK` frame. Consumer SHOULD treat absence of HELLO
within 2 s of session start as "remote is v1.0".

### 8.4 Negotiation rules

- same major + same minor → silent.
- same major + remote minor > local minor → info-log; treat reserved
  fields as zero.
- same major + remote minor < local minor → info-log; do not emit
  minor-only frames the older side wouldn't understand.
- different major → consumer raises a fatal error and stops; producer
  logs and refuses commands.

### 8.5 Backward-compat clause

Protocol v1.0 senders that never emit HELLO remain conformant. Receivers
that don't recognise `OEC_STREAM_HELLO` MUST route it through their default
discard arm (not error).
