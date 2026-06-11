# OEconnect — Bonsai ↔ OpenEphys bidirectional bridge

**Status:** Draft design — approved by user 2026-06-10.
**Spec version:** 1.0
**Repo:** greenfield (this directory).

---

## 1. Purpose

Provide a low-latency, bidirectional bridge between the OpenEphys GUI (C++/JUCE
acquisition software) and the Bonsai-rx visual programming environment (.NET /
Reactive Extensions). The bridge eliminates a long-standing pain point in
systems-neuroscience pipelines where these two widely-used tools cannot share
signals or controls without ad-hoc TCP scripts, file polling, or external sync
hardware.

Concrete capabilities exposed once OEconnect is installed:

1. Bonsai workflows can subscribe to OpenEphys data streams: raw broadband,
   filtered continuous, spike events, and TTL/digital events.
2. Bonsai workflows can command OpenEphys: start/stop acquisition, start/stop
   recording (filename + directory), and assert TTL output lines on the
   acquisition board.
3. OpenEphys continues to write its own recording files independently as a
   sanity-check backup; Bonsai-issued events appear on OE's own event bus so
   the OE recording remains a complete record.
4. Closed-loop round trip (event in → Bonsai logic → TTL out) hits **p99.9 <
   1 ms** for ≤ 256 ch @ 30 kHz on a single Windows box.

## 2. Scope and non-goals

### In scope (v1.0)

- Same-Windows-box deployment via lock-free shared-memory ringbuffers
  (hard-RT tier).
- Same-machine cross-OS and LAN deployment via ZeroMQ (soft-RT tier).
- Acquisition boards: Open Ephys Acquisition Board (Rhythm/Intan RHD), ONIX,
  Neuropixels via OE, and a `FileReader` adapter for offline / CI use.
- Bonsai .NET package targeting Bonsai 2.x (net472) and Bonsai 3.x (net6.0).
- OE GUI plugin built against the OpenEphys plugin-build template.

### Explicitly out of scope (v1.0)

- Hard real-time OS certification (RT_PREEMPT, VxWorks etc.).
- Encryption of stream payloads — only ZMQ CURVE authentication is offered.
- Writing OE-format recording files — that remains OE's Record Node.
- Spike sorting on the Bonsai side — Bonsai consumes spikes OE produces.
- Bonsai talking directly to acquisition-board firmware. **All hardware I/O
  routes through the OEconnect plugin.** This is a hard project rule, not a
  preference. See §10.

## 3. High-level architecture

Three independently built artifacts share one wire-protocol contract.

```
oeconnect/
├── spec/                              ← protocol contract (Markdown + IDL)
│   └── oec-protocol-v1.md
├── plugin-openephys/OEconnect/        ← C++/JUCE plugin for OE GUI
├── package-bonsai/Bonsai.OEconnect/   ← .NET / Bonsai package (NuGet)
├── libshared/oeconnect/               ← C ABI native lib used by both ends
├── examples/                          ← OE signal-chain XML + .bonsai workflows
└── ci/                                ← GitHub Actions workflows
```

`libshared/oeconnect/` builds as `liboeconnect.{dll,so,dylib}`. It implements
the ringbuffer layout, frame parser, and drift-fit math exactly once, in C
with a stable C ABI. The C++ plugin links it natively; the .NET Bonsai
package consumes it via `DllImport`. This makes the wire format
unforgeable-by-drift: there is no second implementation that can diverge.

Process and transport topology:

```
 OE GUI process                            Bonsai process
 ┌─────────────────────────────────┐       ┌─────────────────────────┐
 │ OEconnectProcessor (JUCE)       │  shm  │ Bonsai.OEconnect        │
 │   audio thread → data_ring      │◄═════►│   sources / sinks       │
 │   audio thread ← cmd_ring       │ rings │   (NuGet, dual-target)  │
 │   audio thread → ack_ring       │       │                         │
 │   shipper thread → ZMQ socket   │  ZMQ  │   NetMQ subscriber      │
 │                                 │ipc/tcp│                         │
 └────────┬────────────────────────┘       └─────────────────────────┘
          │
   OE Source Node ─► FPGA TTL / I/O
```

## 4. Wire protocol

### 4.1 Frame header (32 B, packed, little-endian)

```c
struct oec_frame_header {
    uint32_t magic;             // 'O','E','C','1' = 0x3143454F
    uint8_t  version_major;     // bump on layout-breaking change
    uint8_t  version_minor;     // bump on additive change
    uint16_t stream_id;         // see §4.2
    uint32_t payload_len;       // bytes immediately after header
    uint64_t sample_index;      // FPGA sample counter, authoritative
    uint64_t host_qpc_ticks;    // QueryPerformanceCounter at frame creation
    uint16_t flags;             // bit 0 = CONTINUATION, bit 1 = LOST_DATA
    uint16_t crc16;             // optional; zero if disabled
};
```

### 4.2 Stream IDs

| ID    | Name             | Payload                                                                      |
|-------|------------------|------------------------------------------------------------------------------|
| 0x01  | `RAW_BLOCK`      | `block_subheader` (8 B) + `int16[n_samples × n_channels]`, chan-major (planar) |
| 0x02  | `FILTERED_BLOCK` | same shape as `RAW_BLOCK`; filter info conveyed via header flags + most recent SYNC |
| 0x03  | `SPIKE`          | `{electrode_u16, unit_u16, threshold_f32, waveform_int16[N]}`                |
| 0x04  | `TTL_EVENT`      | `{line_u8, edge_u8, board_id_u8, _pad_u8}`                                   |
| 0x10  | `SYNC`           | `{qpc_freq_hz_u64, fpga_sample_rate_hz_f64, stream_meta[n]}` — periodic heartbeat (see §4.2.1) |
| 0x20  | `CMD`            | `{cmd_id_u16, cookie_u32, body…}` (Bonsai → OE)                              |
| 0x21  | `ACK`            | `{cookie_u32, status_u16, body…}` (OE → Bonsai)                              |
| 0x22  | `ERROR`          | `{code_u16, utf8_len_u16, utf8_msg[…]}`                                      |

#### 4.2.1 `block_subheader` (RAW_BLOCK / FILTERED_BLOCK)

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

### 4.3 Command IDs (`CMD.cmd_id`)

| Code  | Name              | Body                                                  | Class |
|-------|-------------------|-------------------------------------------------------|-------|
| 0x0001 | `START_RECORD`   | `{utf8_len_u16, dir[], utf8_len_u16, prefix[]}`       | slow  |
| 0x0002 | `STOP_RECORD`    | —                                                     | slow  |
| 0x0003 | `SET_TTL`        | `{line_u8, edge_u8}`                                  | fast  |
| 0x0004 | `PULSE_TTL`      | `{line_u8, edge_u8, width_us_u32}`                    | fast  |
| 0x0005 | `START_ACQ`      | —                                                     | slow  |
| 0x0006 | `STOP_ACQ`       | —                                                     | slow  |
| 0x0007 | `GET_STATE`      | —                                                     | fast  |

### 4.4 Status codes (`ACK.status`)

`OK=0, PENDING=1, COMPLETED=2, BUSY=3, NOT_SUPPORTED=4, BAD_ARG=5, TIMEOUT=6, INTERNAL=7`

### 4.5 Block sizing

OE plugin block defaults to **32 samples** per `process()` callback ≈ 1.07 ms
at 30 kHz. One `RAW_BLOCK` frame per callback. At 256 ch: 32 × 256 × 2 B =
16 KB payload + 32 B header. Comfortably under one 64 KB ringbuf slot.

### 4.6 Dual-clock sync

Every frame's header carries `(sample_index, host_qpc_ticks)`. Once per second
the plugin also emits a `SYNC` frame carrying `qpc_freq_hz` and
`fpga_sample_rate_hz`. The Bonsai side keeps a 60-point ring of `(sample,
qpc)` pairs and runs sliding-window weighted least-squares regression to
maintain `qpc = a · sample + b`. Drift correction surfaces in user code as
`SampleToHostTime(sample_index) → DateTimeOffset`.

Fit divergence (residual RMS > 5 µs) resets the window and triggers a
`LOST_DATA`-style telemetry counter, but no `OnError` — drift glitches are
expected during USB hiccups.

### 4.7 Versioning

`version_major` mismatch ⇒ consumer drops the frame and raises
`ERROR(code=PROTOCOL_VERSION_MISMATCH)`. `version_minor` mismatch ⇒ consumer
accepts and treats unknown stream IDs / flags as opaque.

The authoritative source is `spec/oec-protocol-v1.md`. Any frame layout
change requires a PR to that file plus a version bump; CI rejects frame-layout
diffs that don't bump the version.

## 5. Transport tier 1 — shared-memory ringbuffers

### 5.1 Region

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

### 5.2 `region_header`

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

### 5.3 SPSC algorithm (Vyukov-style)

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

### 5.4 Wakeups (optional)

| OS      | Mechanism                                        |
|---------|--------------------------------------------------|
| Windows | named EVENT `Local\oeconnect.<pid>.data_evt`     |
| Linux   | `eventfd`                                        |
| macOS   | `kqueue`                                         |

Producer signals every K frames (default K = 1; raise to batch under load).
Consumer chooses: blocking wait (power-efficient) or adaptive spin
(100 µs busy-loop, then block) — default is adaptive.

### 5.5 Three rings, one producer per ring

| Ring        | Producer            | Consumer        | SPSC | Notes                                                              |
|-------------|---------------------|-----------------|------|--------------------------------------------------------------------|
| `data_ring` | audio thread        | Bonsai          | yes  | raw / filtered / spikes / TTL events / SYNC                        |
| `cmd_ring`  | Bonsai              | audio thread    | yes  | drained at top of `process()`                                       |
| `ack_ring`  | audio thread (only) | Bonsai          | yes  | populated from in-process `AckOutbox` MPSC queue (§7.4)            |

### 5.6 Discovery

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

### 5.7 Sizing defaults (configurable from editor)

| Knob             | Default  | Rationale                                                  |
|------------------|----------|------------------------------------------------------------|
| `slot_size`      | 64 KiB   | Holds 32 samp × 1024 ch × int16 + header                    |
| `slot_count`     | 256      | 16 MiB → ~1 s buffer at 60 MB/s worst case                  |
| `wakeup_batch_K` | 1        | One wakeup per frame; raise if scheduler thrash             |
| `cmd_slot_count` | 64       | Sparse — start/stop/TTL                                     |
| `ack_slot_count` | 64       | Matches command volume                                      |

## 6. Transport tier 2 — ZeroMQ fallback

Same frame, different pipe. Soft-RT tier: 1–5 ms typical, 5–20 ms tail under
Windows scheduler. Activated when shmem is unavailable (cross-OS or LAN) or
explicitly selected in the editor.

### 6.1 Libraries

| Side     | Library                                       |
|----------|-----------------------------------------------|
| C++ plugin | `libzmq` linked statically + `cppzmq` headers |
| .NET Bonsai package | NetMQ (pure-managed; no native dep dragged into NuGet) |

### 6.2 Sockets

| Endpoint              | Pattern    | Direction          | Default port |
|-----------------------|------------|--------------------|--------------|
| `tcp://*:5557`        | PUB / SUB  | data (OE → Bonsai) | 5557         |
| `tcp://*:5558`        | REP / REQ  | commands           | 5558         |

`ipc://` and `inproc://` use the same scheme; endpoint string becomes
`ipc:///tmp/oeconnect/<pid>.data` (POSIX) or
`ipc://%TEMP%\oeconnect\<pid>.data` (Windows, mapped to a named pipe).

### 6.3 Topic filter — 2-byte ZMQ multipart prefix

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

### 6.4 Backpressure

`ZMQ_SNDHWM = 1024` on PUB. Drops oldest on overflow (cannot block the OE
audio thread). Plugin tracks drop count and surfaces it on the next `SYNC`
frame + sets `BIT_LOST_DATA` on the next user frame.

`ZMQ_RCVHWM = 4096` on SUB — generous so Bonsai workflows can stall briefly.

### 6.5 Control channel ordering

REQ/REP is strictly synchronous. One command in flight at a time per Bonsai
instance. ACK comes back via REP socket payload (NOT the data PUB stream),
keeping control traffic off the high-rate channel. Concurrent commands from
multiple subscribers are serialised by the plugin; the loser gets
`ACK(status=BUSY)`.

### 6.6 Heartbeat

`ZMQ_HEARTBEAT_IVL = 500 ms`, `ZMQ_HEARTBEAT_TIMEOUT = 2000 ms` on the
control socket. Disconnect triggers Bonsai source `OnError`.

### 6.7 Security (off by default)

ZMQ CURVE keypair on both ends. Editor exposes "Require auth" checkbox and a
public-key field. Off ⇒ open localhost dev. On ⇒ mandatory whenever binding
to anything but `127.0.0.1`.

## 7. OE plugin internals

### 7.1 Component layout

```
plugin-openephys/OEconnect/Source/
├── OEconnectProcessor.{h,cpp}        // GenericProcessor subclass
├── OEconnectEditor.{h,cpp}           // GenericEditor subclass (JUCE UI)
├── Transport/
│   ├── ITransport.h                  // pure virtual
│   ├── ShmemTransport.{h,cpp}        // wraps liboeconnect ringbufs
│   └── ZmqTransport.{h,cpp}          // wraps cppzmq sockets
├── Boards/
│   ├── IBoardAdapter.h
│   ├── RhdAcqBoardAdapter.{h,cpp}
│   ├── OnixAdapter.{h,cpp}
│   ├── NeuropixelsAdapter.{h,cpp}
│   └── FileReaderAdapter.{h,cpp}
├── Sync/
│   └── DriftEmitter.{h,cpp}          // emits SYNC frames every 1 s
└── Util/
    └── AckOutbox.h                   // lock-free MPSC
```

### 7.2 Signal-chain placement

`OEconnect` is a **passthrough processor** — samples flow unmodified
downstream. The recommended chain places a Record Node *after* it so OE
keeps its own backup recording independent of Bonsai:

```
[Acq Source] → [Bandpass Filter] → [OEconnect] → [Record Node]
                                       │
                                       └── shmem / ZMQ → Bonsai
```

### 7.3 `IBoardAdapter`

```cpp
class IBoardAdapter {
public:
    virtual ~IBoardAdapter() = default;
    virtual juce::String name() const = 0;
    virtual int numTtlOutLines() const = 0;

    // Wait-free, called on audio thread.
    // Returns the sample index at which the line will actually flip.
    virtual uint64_t setTtl(uint8_t line, bool high) = 0;

    // Called on the JUCE message thread, not the audio thread.
    virtual void onStartAcquisition(int blockSize, double sampleRate) {}
    virtual void onStopAcquisition() {}
};
```

Recognised upstream source nodes (v1.0):

| Source node              | Adapter                  | TTL out path                                  |
|--------------------------|--------------------------|-----------------------------------------------|
| `Rhythm FPGA`            | `RhdAcqBoardAdapter`     | `RhythmNode::setTTLBit()`                     |
| `Onix Source`            | `OnixAdapter`            | `oni_write_reg()` on DIO peripheral           |
| `Neuropixels-PXI`        | `NeuropixelsAdapter`     | delegates to paired Acq Board when present    |
| `File Reader`            | `FileReaderAdapter`      | appends to `ttl_out_log.csv`; no hw effect    |

Adapter is auto-selected at `updateSettings()` by walking up the signal
chain. None recognised ⇒ `FileReaderAdapter` + yellow banner in the
editor: *"No supported acquisition board upstream — TTL out disabled."*

### 7.4 Threading model

| Thread           | Owns                                                                  | Hot path? |
|------------------|-----------------------------------------------------------------------|-----------|
| JUCE audio       | `data_ring` (write), `cmd_ring` (read), `ack_ring` (write)            | yes — wait-free, no alloc, no log |
| Sync             | pushes to `AckOutbox`                                                 | no — 1 Hz timer |
| Slow-cmd worker  | pushes to `AckOutbox`; runs start/stop-record, config changes         | no |
| Shipper          | reads `data_ring` + `ack_ring`, pushes to ZMQ PUB; reads from ZMQ REP, pushes into `cmd_ring`; matches ACK cookies back to REP replies | no — only present when active transport ≠ SharedMem |

Invariant: the audio thread is the only writer for any of the three shmem
rings. Slow-cmd worker and sync thread funnel through `AckOutbox` (lock-free
MPSC); the audio thread drains it each `process()` tick and copies entries
into `ack_ring`. The shipper thread exists so the audio thread never touches
a ZMQ socket — `libzmq`'s userland queues + EINTR retries cannot stall
acquisition.

**ZMQ command path detail:** in ZMQ mode the shipper thread also runs the
REP loop. On receiving a `CMD` frame it (a) records `{cookie → caller
identity}`, (b) pushes the frame into `cmd_ring` exactly as a shmem Bonsai
peer would, then (c) waits for the matching `ACK(cookie)` to appear in
`ack_ring`, dequeues it, and sends it back over REP. This lets the audio
thread treat shmem and ZMQ command sources identically — both arrive via
`cmd_ring`.

### 7.5 Command classification

| Class | Examples                              | Handling                                                                                                                         |
|-------|---------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| Fast  | `SET_TTL`, `PULSE_TTL`, `GET_STATE`   | Execute inline via board adapter. Emit `ACK(OK, sample_index)` into `data_ring` same tick. Latency ≤ one block.                  |
| Slow  | `START_RECORD`, `STOP_RECORD`, `START_ACQ`, `STOP_ACQ` | Audio thread hands off to slow-cmd worker, immediately emits `ACK(PENDING, cookie)`. Worker later pushes `ACK(COMPLETED, cookie)`. |

### 7.6 Hot-path pseudocode

```cpp
void OEconnectProcessor::process(AudioBuffer<float>& buffer) {
    drainCmdRing();                           // fast cmds inline; slow ⇒ worker
    drainAckOutbox();                         // ACKs into ack_ring

    for (auto streamId : enabledStreams) {
        auto* slot = dataRing.acquireSlot();
        if (!slot) {                          // ring full ⇒ drop oldest
            dataRing.dropOldest();
            slot = dataRing.acquireSlot();
            nextFrameFlags |= BIT_LOST_DATA;
        }
        writeFrame(slot, streamId, buffer);
        dataRing.publish(slot);
    }
}
```

### 7.7 Editor UI (compact JUCE)

```
┌── OEconnect ───────────────────────────┐
│ Transport: [Auto ▼]   ZMQ port: [5557] │
│ Bind: [127.0.0.1]    [Require auth ☐]  │
│ Streams:  [✓] Raw [✓] Filt [✓] Spk [✓] TTL │
│ Block: [32 samp ▼]   Slots: [256]      │
│                                        │
│ ● 1 consumer  drops: 0   lag: 0.4 ms   │
│ Mode: SharedMem   Board: Rhythm FPGA   │
└────────────────────────────────────────┘
```

## 8. Bonsai package surface

Package: **`Bonsai.OEconnect`** (NuGet). Dual-target net472 (Bonsai 2.x) +
net6.0 (Bonsai 3.x). Native `liboeconnect.{dll,so,dylib}` packed under
`runtimes/<rid>/native/`. XML namespace prefix in workflows: **`oec:`**.

### 8.1 Operators

| Operator           | Category    | Input              | Output                       | Purpose                                            |
|--------------------|-------------|--------------------|------------------------------|----------------------------------------------------|
| `OpenEphysSession` | Source      | —                  | `IObservable<SessionStatus>` | Liveness, transport, drop count, board name        |
| `RawSamples`       | Source      | —                  | `IObservable<RawBlock>`      | Continuous broadband per `process()` block         |
| `FilteredSamples`  | Source      | —                  | `IObservable<RawBlock>`      | Filtered continuous (LFP / spike band)             |
| `Spikes`           | Source      | —                  | `IObservable<SpikeEvent>`    | Threshold or sorter output                         |
| `TtlEvents`        | Source      | —                  | `IObservable<TtlEvent>`      | TTL edges from OE event bus                        |
| `SyncPoints`       | Source      | —                  | `IObservable<SyncPoint>`     | Raw (sample, qpc) pairs                            |
| `ToMat`            | Transform   | `RawBlock`         | `Mat` (Bonsai.Dsp)           | Interop with existing DSP operators                 |
| `SampleToHostTime` | Transform   | any frame          | same + `DateTimeOffset`      | Applies drift-corrected clock                      |
| `StartRecording`   | Sink        | trigger            | trigger (passthrough)        | Args: `Directory`, `Prefix`                        |
| `StopRecording`    | Sink        | trigger            | trigger                      | —                                                  |
| `SetTtl`           | Sink        | trigger or `TtlCommand` | trigger                 | Args: `Line`, `Edge`                               |
| `PulseTtl`         | Sink        | trigger            | trigger                      | Args: `Line`, `WidthMicroseconds`                  |

### 8.2 Data types (`Bonsai.OEconnect.Data`)

```csharp
public readonly struct RawBlock
{
    public ulong SampleIndex { get; }              // FPGA-authoritative
    public ulong HostQpcTicks { get; }
    public ushort StreamId { get; }
    public int NumChannels { get; }
    public int NumSamples { get; }
    public ReadOnlyMemory<short> Samples { get; }  // chan-major; pooled buffer
}

public readonly struct TtlEvent
{
    public ulong SampleIndex;
    public ulong HostQpcTicks;
    public byte Line;
    public byte Edge;        // 0 = falling, 1 = rising
    public byte BoardId;
}

public readonly struct SpikeEvent
{
    public ulong SampleIndex;
    public ulong HostQpcTicks;
    public ushort ElectrodeId;
    public ushort UnitId;
    public float Threshold;
    public ReadOnlyMemory<short> Waveform;   // pooled
}

public readonly struct SyncPoint
{
    public ulong SampleIndex;
    public ulong HostQpcTicks;
    public double FpgaSampleRateHz;
}

public sealed class SessionStatus
{
    public bool   IsConnected { get; init; }
    public string Transport   { get; init; }       // "SharedMem" | "Zmq"
    public string BoardName   { get; init; }
    public long   FrameCount  { get; init; }
    public long   DropCount   { get; init; }
    public double EstimatedLagMs { get; init; }
}
```

### 8.3 Memory model

`Samples` and `Waveform` are slices over buffers rented from
`ArrayPool<short>.Shared`. **The buffer is valid only inside the `OnNext`
invocation.** Users who need to retain past one tick must clone explicitly
(`.ToArray()` or `RawBlock.Clone()`). Documented on every operator
description string.

### 8.4 `SessionRegistry` singleton (`internal`)

- Keys connections by endpoint string (`""` = auto-discovery).
- Reference-counted: multiple operators on the same endpoint share one
  ringbuf reader and one drift-fit instance.
- Last unsubscribe disposes session, returns buffers to pool, unmaps shmem
  or closes ZMQ sockets.
- Background reader thread per session marshals into per-stream subjects.

### 8.5 Endpoint property convention

Every source op carries an `Endpoint` string property:

| Value                              | Meaning                                       |
|------------------------------------|-----------------------------------------------|
| `""` (empty)                       | Auto-discovery (newest live sidecar; shmem preferred) |
| `"tcp://host:port"`                | Explicit ZMQ                                  |
| `"shm://oeconnect.18432.shm"`      | Explicit shmem region (advanced)              |

### 8.6 Errors

Source observable fires `OnError(OpenEphysConnectionException)` on
disconnect, carrying transport + endpoint + last drop count.
Auto-reconnect is **off** by default. Users add `.Repeat()` explicitly if
desired. Closed-loop experiments must not silently mask a dead acquisition.

### 8.7 Example workflows shipped in `examples/`

- `closed_loop_spike_triggered_stim.bonsai`
- `record_with_ttl_marker.bonsai`
- `lfp_band_visualization.bonsai`
- `multi_subscriber_data_split.bonsai`

## 9. Failure model, backup recording, sync markers

### 9.1 Failure matrix

| Failure                            | Detection                                  | Recovery                                                                              | User signal                                  |
|------------------------------------|--------------------------------------------|---------------------------------------------------------------------------------------|----------------------------------------------|
| Bonsai workflow crashes            | `consumer_idx` stalls; reader heartbeat gone | Plugin keeps publishing; drops oldest. OE Record Node keeps recording.                | Editor banner: "consumer disconnected"        |
| Plugin process crashes             | Bonsai sees `producer_heartbeat_ns` stale > 3 s | Source op fires `OnError`. No auto-reconnect.                                       | Bonsai workflow halts                         |
| OE GUI crashes                     | Sidecar JSON gone                          | None — full restart                                                                   | Both ends die together                        |
| ZMQ socket disconnect              | `ZMQ_HEARTBEAT_TIMEOUT = 2 s`              | Same as plugin crash                                                                  | OnError on sources                            |
| Ringbuf full (slow consumer)       | `producer_idx − consumer_idx == slot_count`  | Drop oldest, set `BIT_LOST_DATA`, increment `DropCount`                              | `SessionStatus.DropCount` rises               |
| Drift fit diverges                 | Residual RMS > 5 µs                        | Reset window, recollect 60 sync points                                                | `EstimatedLagMs` → NaN briefly                |
| Slow cmd hangs (e.g. bad path)     | Worker thread timeout (5 s default)        | Emit `ACK(status=TIMEOUT, cookie=X)`                                                  | Bonsai sink op throws                         |
| No recognised board upstream       | `updateSettings()` scan misses             | Falls to `FileReaderAdapter`; `SetTtl` returns `ACK(NOT_SUPPORTED)`                  | Yellow banner in editor                       |

### 9.2 Independent backup recording

```
                    ┌── OE Record Node ──► <session>/openephys/  (continuous .dat + .events + .nwb)
[Acq] → [Filt] → [OEconnect] ─┤
                    └── shmem/ZMQ ──► Bonsai sinks ──► <session>/bonsai/ (Bonsai-side writers)
```

Two independent writer chains. If Bonsai's writers fail (disk full, process
crash), the OE recording remains a complete record.

### 9.3 Cross-stream sync markers

When Bonsai issues `SetTtl(line, edge)` through OEconnect, the plugin's
audio thread does two things atomically with respect to the next block:

1. Calls `boardAdapter.setTtl(...)` — hardware flips at sample `S`.
2. Calls `addEvent()` on its `GenericProcessor` base, emitting the same TTL
   event onto OE's own event bus at sample `S`.

Result: OE's Record Node captures it in its `.events` file, Bonsai sees the
echo on its `TtlEvents` stream, and offline alignment between Bonsai-written
files and OE-written files uses these markers as anchors.

### 9.4 Best-practice session-control workflow

`StartRecording` sink op sends `START_RECORD` over the cmd channel; plugin
forwards to OE's `RecordEngineManager` via
`CoreServices::setRecordingDirectory()` + `CoreServices::setRecordingStatus(true)`.
One Bonsai action triggers both writers, both rooted under the same session
folder by convention:
`<root>/<UTC-timestamp>/openephys/` and `<root>/<UTC-timestamp>/bonsai/`.

## 10. Hard architectural rules

1. **Bonsai must never talk directly to acquisition-board firmware.** Every
   hardware command and every hardware-line read crosses the OEconnect
   plugin. This preserves a single source of truth for the event stream: the
   OE recording remains a complete sanity-check copy of every TTL the bridge
   ever issued. A "fast direct path" from Bonsai to the FPGA is **never** an
   acceptable optimisation, even if it would shave latency.
2. **The audio thread is the only writer of the three shmem rings.** Any
   helper thread emitting toward Bonsai funnels through `AckOutbox`.
3. **Frame layout changes require a spec version bump.** CI enforces.

## 11. Testing strategy

| Tier               | Subject                                                       | Tooling                              | Location                                  |
|--------------------|---------------------------------------------------------------|--------------------------------------|-------------------------------------------|
| Unit               | `liboeconnect`: frame parse, ringbuf SPSC, drift fit          | gtest                                | `libshared/oeconnect/tests/`              |
| Unit               | Bonsai operator semantics (mock `SessionRegistry`)            | xUnit                                | `package-bonsai/Bonsai.OEconnect/tests/`  |
| Integration        | Plugin ↔ libshared ↔ shmem roundtrip, no OE GUI               | gtest harness loads `OEconnectProcessor`, feeds synthetic `AudioBuffer` | `plugin-openephys/OEconnect/Tests/`       |
| Integration        | NetMQ ↔ plugin ZMQ, end-to-end frame echo                     | xUnit + spawned plugin fixture       | `package-bonsai/Bonsai.OEconnect/tests/Integration/` |
| Stress             | Lock-free ringbuf: producer/consumer race, 10⁹ frames         | Custom binary, ASAN + TSAN           | `libshared/oeconnect/tests/stress/`       |
| Throughput         | 1024 ch @ 30 kHz, 24 h soak, zero drops                       | BenchmarkDotNet + fixture            | `package-bonsai/.../tests/Perf/`          |
| Replay             | Recorded session feeds `FileReaderAdapter`, deterministic     | gtest fixture                        | `examples/test_sessions/`                 |
| Latency (manual)   | Closed-loop in→out on real hardware                           | scope + 100 Hz function gen          | `docs/perf/closed_loop_latency.md`        |

### 11.1 Latency verification procedure (headline number)

1. Setup: Acq Board, function generator on TTL-in line 0, scope on TTL-out
   line 2.
2. Bonsai workflow:
   `TtlEvents → Where(Line == 0 && Edge == 1) → PulseTtl(Line = 2, WidthMicroseconds = 500)`.
3. Inject pulses at 100 Hz for 10 minutes (60 000 trials).
4. Scope captures in→out histogram.
5. **Pass criterion:** p99.9 < 1 ms in SharedMem mode, p99 < 5 ms in
   ZMQ-loopback mode.

## 12. CI

`.github/workflows/`:

| File                    | Triggers              | Matrix                                                | Outputs                                       |
|-------------------------|-----------------------|-------------------------------------------------------|-----------------------------------------------|
| `libshared.yml`         | push / PR             | OS × {Debug, Release, ASAN, TSAN}                     | gtest report, `liboeconnect.*` artifact       |
| `plugin-openephys.yml`  | push / PR / tag       | Win + Linux + macOS, against OE plugin-SDK submodule  | `OEconnect.bundle` per OS                     |
| `package-bonsai.yml`    | push / PR / tag       | net472 + net6.0                                       | NuGet `Bonsai.OEconnect.<ver>.nupkg`          |
| `spec.yml`              | PR touching `spec/`   | —                                                     | Rejects if frame layout changed without version bump |

Tagged releases (`v1.0.0`) trigger uploads to GitHub Releases for the OE
bundle and to NuGet for the Bonsai package. `liboeconnect` ships bundled in
both, and as a standalone `liboeconnect.runtime` NuGet for third-party
consumers.

## 13. Versioning

SemVer aligned with `spec/oec-protocol-v1.md` major/minor. Plugin, NuGet,
and `liboeconnect` all carry the same version. Frame `version_major`
mismatch ⇒ frame dropped + `ERROR(PROTOCOL_VERSION_MISMATCH)`.
`version_minor` mismatch ⇒ forward-compatible accept.

## 14. Open items deferred to implementation plan

- Exact `RecordEngineManager` / `CoreServices` API surface used to drive the
  OE Record Node from a command — confirm against current OE-GUI source at
  implementation time.
- ONIX SDK version pinning — currently moves fast.
- Whether to publish `liboeconnect.runtime` as a standalone NuGet on first
  release or wait for community demand.
- Bonsai 2.x vs 3.x feature gating — net472 build may need to disable
  operators that depend on `System.Memory` types newer than what Bonsai 2.x
  ships.
