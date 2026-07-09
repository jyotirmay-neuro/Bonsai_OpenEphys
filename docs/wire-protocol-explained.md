# The OEconnect wire protocol, explained

This is the **teaching** companion to the protocol. It explains *why* the wire
format looks the way it does, walks a block of data from the acquisition thread
to a Bonsai visualizer byte by byte, and gives you the mental model you need to
debug the bridge or write a new consumer.

If you just want the frozen field offsets and codes, those live in the source of
truth — `OEC_PROTOCOL_VERSION_{MAJOR,MINOR}` and the packed structs in
`libshared/oeconnect/include/oeconnect/` (`frame.h`, `ringbuf.h`, `shm.h`,
`sidecar.h`, `hello.h`). CI hashes those headers and rejects any layout drift
that doesn't bump the version. This document paraphrases them; where the two ever
disagree, **the headers win.**

---

## 1. The one-paragraph model

Open Ephys produces data; Bonsai consumes it. Commands go the other way. That
asymmetry is baked into everything below:

```
        data  (blocks, spikes, TTL events, sync, errors)
   OE  ───────────────────────────────────────────────▶  Bonsai
       ◀───────────────────────────────────────────────
        commands (start/stop recording, TTL, acquisition)
```

There are **two pipes** carrying the *identical* frame bytes, chosen by where
Bonsai runs:

| Pipe | When | Latency | Mechanism |
|---|---|---|---|
| **Shared memory** | OE and Bonsai on the same machine | sub-millisecond | lock-free ring buffers in a mapped region |
| **ZeroMQ** | different machines, or forced | 1–5 ms typical | PUB/SUB for data, REQ/REP for commands |

The frame is the same on both. Learn the frame once and you understand both
transports.

---

## 2. The frame: a 32-byte header, then payload

Every message — a block of neural data, a spike, a command, an acknowledgement —
is a **frame**: a fixed 32-byte header followed by a variable payload. The header
is packed and little-endian:

```c
struct oec_frame_header {
    uint32_t magic;             // 'O','E','C','1'  → 0x3143454F
    uint8_t  version_major;     // layout-breaking changes bump this
    uint8_t  version_minor;     // additive changes bump this
    uint16_t stream_id;         // what kind of frame this is (§3)
    uint32_t payload_len;       // bytes that follow the header
    uint64_t sample_index;      // FPGA sample counter — the authoritative clock
    uint64_t host_qpc_ticks;    // host performance-counter reading at creation
    uint16_t flags;             // bit0 = CONTINUATION, bit1 = LOST_DATA
    uint16_t crc16;             // optional integrity check; 0 if disabled
};
```

Why each field earns its place:

- **`magic`** is a sanity gate. If the first four bytes aren't `OEC1`, the
  receiver is misaligned or reading garbage — it raises `ERROR(BAD_MAGIC)` rather
  than trusting the rest. Cheap insurance against a desynchronized ring.
- **`version_major` / `version_minor`** encode the compatibility contract
  (§7). A consumer that sees a *major* it doesn't know drops the frame; a newer
  *minor* it doesn't fully understand it accepts and treats the unknown bits as
  opaque. This is what lets the protocol grow without a flag day.
- **`stream_id`** is the discriminator. One number tells the consumer whether the
  payload is a raw block, a spike, a command, or an ack. Everything downstream
  branches on it.
- **`sample_index` + `host_qpc_ticks`** are the heart of time sync (§5). *Every*
  frame carries both clocks so any frame is a synchronization point, not just the
  periodic `SYNC`.
- **`flags`** carry two one-bit truths that must travel *with* the data:
  `LOST_DATA` (a gap happened before this frame) and `CONTINUATION` (this frame
  spans more than one ring slot).

### Worked example: a 256-channel raw block

At 30 kHz, OE hands the plugin **32 samples per callback** (~1.07 ms of signal).
For 256 channels of `int16`:

```
payload = 8 (block subheader) + 32 × 256 × 2  = 16 392 bytes
frame   = 32 (header) + 16 392                = 16 424 bytes
```

Comfortably inside one 64 KiB ring slot, one frame per callback, ~940 frames/s.
Hold onto these numbers — they explain the ring sizing in §6.

---

## 3. Stream IDs: what a frame *is*

`stream_id` is the type tag. The full table lives in `frame.h`; the ones you'll
actually meet:

| ID | Name | Payload, in plain terms |
|---|---|---|
| `0x01` | `RAW_BLOCK` | A block subheader + `int16` samples, **channel-major** (all of channel 0's samples, then channel 1's…). |
| `0x02` | `FILTERED_BLOCK` | Same shape as raw; it's the output of a filter node placed upstream in the OE chain. |
| `0x03` | `SPIKE` | One detected spike: electrode, unit, threshold, waveform. |
| `0x04` | `TTL_EVENT` | One digital edge: line, rising/falling, board. |
| `0x10` | `SYNC` | The once-a-second heartbeat carrying clock rates and per-stream geometry. |
| `0x20` | `CMD` | A command, Bonsai → OE. |
| `0x21` | `ACK` | The reply to a command, OE → Bonsai. |
| `0x22` | `ERROR` | A code + human-readable message. |
| `0x30` | `HELLO` | Version announcement at session start (§8). |

Two design choices worth understanding:

**Channel-major (planar), not interleaved.** A raw block stores
`[ch0 s0, ch0 s1, … ch0 s31, ch1 s0, …]`. Neuroscience consumers almost always
want one channel's time series contiguously — for filtering, plotting, spike
detection. Planar layout means `ToMat` in Bonsai maps the block to an OpenCV
`Mat` with rows = channels and **zero reshuffling**.

**The block is self-describing.** Each raw/filtered frame begins with an 8-byte
subheader so a consumer never has to *remember* geometry across frames:

```c
struct oec_block_subheader {
    uint16_t n_channels;
    uint16_t n_samples;
    uint8_t  dtype;       // 0=int16, 1=float32, 2=int32 (reserved)
    uint8_t  source_id;   // which OE DataStream produced this
    uint16_t reserved;
};
```

`source_id` matters the moment a rig has more than one stream. A Neuropixels
probe emits an AP band at 30 kHz *and* an LFP band at 2.5 kHz — different rates,
different channel counts, different sample clocks. Each is a separate
**DataStream** with its own `source_id`, and their blocks **interleave** on the
one data ring. The consumer demultiplexes by `source_id`, and the periodic
`SYNC` frame carries a small table of
`{source_id, n_channels, sample_rate_hz}` triples so a subscriber that joined
late can reconstruct every stream's layout without waiting for the next block of
each.

---

## 4. Shared memory: the sub-millisecond path

When OE and Bonsai share a machine, the frame never touches a socket. Both
processes map the same region of memory and hand frames across it through
**lock-free single-producer/single-consumer (SPSC) ring buffers**.

### 4.1 One region, three rings

```
region name:  Local\oeconnect.<pid>.<node_id>.shm   (Windows)
              /oeconnect.<pid>.<node_id>.shm         (POSIX)
```

The name is scoped by **both** process id and OE node id — because one GUI
process can host several OEconnect processors (say, one publishing raw data and
another a filtered branch), and each must own its own rings. Keying on pid alone
was an early bug: the second processor attached to the first's region and became
a phantom second producer.

Inside, three rings, each strictly one writer and one reader:

| Ring | Producer | Consumer | Carries |
|---|---|---|---|
| `data_ring` | OE audio thread | Bonsai | raw / filtered / spikes / TTL / sync |
| `cmd_ring` | Bonsai | OE audio thread | commands, drained atop each `process()` |
| `ack_ring` | OE audio thread | Bonsai | acknowledgements |

Note the hard rule: **the audio thread is the only writer of all three rings.**
Even acknowledgements that originate on a helper thread are funneled through an
in-process `AckOutbox` queue so that, from the ring's perspective, there is
exactly one producer. SPSC correctness depends on it.

### 4.2 How the SPSC ring actually works

A ring is a fixed array of `slot_count` slots plus two counters — a producer
index and a consumer index — living on **separate cache lines** so the two
threads never fight over the same line (false sharing would silently tax the hot
path).

Producing a frame:

1. Load the consumer index with **acquire** ordering.
2. If `producer_idx − consumer_idx == slot_count`, the ring is **full**. Rather
   than block the audio thread (never acceptable), drop the oldest by advancing
   both indices, and remember to set `LOST_DATA` on the next frame published.
3. Write the frame into `ring[producer_idx mod slot_count]`.
4. Publish by storing `producer_idx + 1` with **release** ordering.

The consumer mirrors it: read its index, check for available slots, read the
slot, then advance its index with release. The acquire/release pair is what makes
the payload write *happen-before* the index bump the other thread observes — no
lock, no syscall, no kernel transition on the hot path. That is where the
sub-millisecond number comes from.

`slot_count` must be a **power of two** so `mod` is a single bitwise `and`.

### 4.3 Frames bigger than a slot

Most frames fit one 64 KiB slot. When one doesn't, it sets `CONTINUATION` and
spans up to **four** consecutive slots; beyond that the producer raises
`ERROR(FRAME_TOO_LARGE)`. The first slot holds the header plus as much payload as
fits; the rest hold bare payload. The consumer computes the span from the header
(`ceil((32 + payload_len) / slot_size)`) and **must** confirm the *whole* span is
published before reassembling — reading a half-written span is the classic torn
read this check prevents.

Continuation is a shared-memory-only trick. Over ZMQ, one slot maps to one
message, so a frame must fit a single slot — size the ring with that in mind.

### 4.4 Waking the consumer

The consumer can spin, but spinning wastes power. Each OS offers a cheap wakeup —
a named event on Windows, `eventfd` on Linux, `kqueue` on macOS — that the
producer signals every *K* frames (default 1). The default consumer strategy is
**adaptive**: busy-spin for ~100 µs (catches the common case with no syscall),
then fall back to a blocking wait. Best of both: low latency when data is
flowing, low power when it isn't.

### 4.5 Finding the session: discovery

How does Bonsai know the region name without being told? On startup the plugin
drops a small **sidecar JSON** in a well-known directory
(`%TEMP%\oeconnect\sessions\` / `/tmp/oeconnect/sessions/`) naming its region,
events, ZMQ fallback endpoints, and a heartbeat timestamp; it deletes the file on
clean shutdown. A Bonsai source with an empty `Endpoint` scans that directory,
picks the newest session whose heartbeat is fresh within 5 s, and prefers shared
memory when same-OS, ZMQ otherwise.

The catch: with **several** OEconnect nodes in one chain, "newest live session"
is ambiguous. Give each Bonsai source an explicit `Endpoint` in that case.

---

## 5. Time sync: making two clocks agree to microseconds

The bridge has to answer "what host wall-clock time was this sample acquired?"
across two independent clocks: the FPGA's sample counter and the host's
performance counter. They tick at slightly different, drifting rates.

The approach is deliberately simple and robust. Every frame carries a
`(sample_index, host_qpc_ticks)` pair — a data point relating the two clocks.
Once per second a `SYNC` frame adds the scale factors (`qpc_freq_hz`,
`fpga_sample_rate_hz`). The Bonsai side keeps a **60-point sliding ring** of
those pairs and runs a weighted least-squares regression to fit a line:

```
qpc_ticks ≈ a · sample_index + b
```

`a` is the measured ratio of the two clock rates (drift and all); `b` is the
offset. Invert it and you can map any sample index to a host `DateTimeOffset` —
which is exactly what the `SampleToHostTime` operator exposes to user workflows.

Because it's a *fit over a window* rather than a single reading, it rides through
USB hiccups and jitter gracefully. If the fit degrades (residual RMS > 5 µs) the
window resets and a telemetry counter ticks — but it does **not** raise
`OnError`, because brief drift glitches are expected, not faults.

---

## 6. Sizing the rings, and the 1023-channel gotcha

Defaults, all editor-configurable:

| Knob | Default | Why |
|---|---|---|
| `slot_size` | 64 KiB | Holds a 32-sample block up to 1023 channels + headers |
| `slot_count` | 256 | 16 MiB ≈ 270 ms of buffering at 30 kHz — absorbs GC pauses |
| `wakeup_batch_K` | 1 | One wakeup per frame; raise if the scheduler thrashes |
| `cmd_slot_count` / `ack_slot_count` | 64 | Commands are sparse |

The number to remember: a frame needs `40 + n_channels × n_samples × 2` bytes,
so a 64 KiB slot tops out at **1023** channels at a 32-sample block — *not* 1024.
Cross that and the producer can't fit the frame in one slot: it drops the frame
and sets `LOST_DATA` on the next one it publishes. From Bonsai this looks like
silence with a rising drop count; from the OE editor's status line it's visible
directly. If you run very high channel counts, either raise `slot_size` or shrink
the block — this is the single most common "why is Bonsai seeing nothing" cause.

---

## 7. Versioning: how the protocol grows without breaking

Everything ships under one SemVer — plugin, NuGet package, and `liboeconnect`
carry the same version, aligned to the protocol major/minor.

The frame's two version bytes drive runtime behavior:

- **`version_major` mismatch** → the frame is dropped and the consumer raises
  `ERROR(PROTOCOL_VERSION_MISMATCH)`. Different majors genuinely can't
  interoperate.
- **`version_minor` mismatch** → forward-compatible **accept**. A newer minor may
  add stream IDs, error codes, or fields; older receivers treat what they don't
  recognize as opaque (log it, keep going). This is why the `ERROR.code` and
  stream-ID tables are explicitly "additive" — a receiver **must** tolerate an
  unknown code rather than fail.

CI enforces the contract from the other side: change a layout header without
bumping the version and the drift gate rejects the commit.

---

## 8. HELLO: negotiating up front (protocol v1.1)

Added in v1.1, `HELLO` (stream `0x30`) lets the two ends announce their
protocol and build versions *before* any data flows, so future minor bumps can be
negotiated instead of guessed:

```c
struct oec_hello_body {          // 16 B, packed, little-endian
    uint16_t protocol_major;     // 1
    uint16_t protocol_minor;     // 1 this release
    uint32_t plugin_version;     // (major<<16)|(minor<<8)|patch
    uint32_t lib_version;
    uint32_t reserved;
};
```

The producer emits one HELLO at `startAcquisition`, before the first block. The
negotiation is unsurprising: same major+minor is silent; a newer remote minor
gets an info-log and its unknown fields treated as zero; an older remote minor
means "don't emit minor-only frames it won't understand"; a different major is
fatal — consumer stops, producer refuses commands.

The crucial **backward-compat clause**: a v1.0 sender that never emits HELLO is
still fully conformant, and any receiver that doesn't recognize the HELLO stream
must route it through its normal discard arm — never error on it. That clause is
what lets v1.1 code talk to v1.0 code with no special casing.

---

## 9. ZeroMQ: the same frame over a socket

When shared memory isn't an option (Bonsai on another machine, or forced in the
editor), the *identical* frame bytes ride ZMQ. Soft-RT tier: 1–5 ms typical,
occasionally 5–20 ms under the Windows scheduler.

- **Data** uses **PUB/SUB** on `tcp://*:5557`. Each message is two ZMQ frames: a
  2-byte `stream_id` prefix (the SUB topic filter) followed by the exact frame
  bytes. A Bonsai op that only wants TTL events subscribes to just that topic and
  the broker/kernel filters the firehose for it.
- **Commands** use **REQ/REP** on `tcp://*:5558` — strictly synchronous, one
  command in flight at a time. The ACK returns on the *reply* socket, never on the
  data stream, keeping control traffic off the high-rate channel. Concurrent
  commands from multiple subscribers are serialized; the loser gets
  `ACK(status=BUSY)`.
- **Backpressure**: `SNDHWM=1024` on PUB drops oldest on overflow (it can never be
  allowed to block the audio thread) and flags `LOST_DATA` on the next frame;
  `RCVHWM=4096` on SUB is generous so a Bonsai workflow can stall briefly without
  loss.
- **Liveness**: a 500 ms heartbeat with a 2 s timeout on the control socket; a
  disconnect raises `OnError` in the Bonsai source.
- **Security**: ZMQ CURVE, **off by default** for localhost dev, but **mandatory**
  the instant you bind to anything other than `127.0.0.1`. A non-loopback bind
  without a key configured refuses to start rather than expose an open port.

---

## 10. The rules that never bend

Three constraints are non-negotiable, and understanding *why* keeps you from
"optimizing" the bridge into incorrectness:

1. **Bonsai never talks to acquisition-board firmware directly.** Every hardware
   command and every line read crosses the OEconnect plugin. This keeps the OE
   recording a complete, authoritative copy of every TTL the bridge ever issued —
   one source of truth. A "fast direct path" from Bonsai to the FPGA is never an
   acceptable latency optimization.
2. **The audio thread is the sole writer of the three rings.** Anything else
   emitting toward Bonsai funnels through `AckOutbox`. SPSC correctness rests on
   this.
3. **Frame-layout changes require a version bump**, enforced by CI.

---

## Where to go next

- **Configure it:** [configuration.md](configuration.md) — every editor option and
  its compatibility matrix.
- **Which build for your GUI:** [oe-version-compatibility.md](oe-version-compatibility.md).
- **What actually works today:** [status.md](status.md).
- **The frozen field offsets:** the packed structs in
  `libshared/oeconnect/include/oeconnect/*.h`, which are authoritative over this
  document.
