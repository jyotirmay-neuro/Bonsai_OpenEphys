# Implementation status

What actually works today, verified against the source. Keep this file honest —
every other document links here rather than restating capability claims.

Last verified: 2026-07-08.

## Legend

- **Working** — implemented and exercised by tests or manual use.
- **Partial** — works, with a caveat that changes how you should use it.
- **Stub** — the code path exists and returns success, but has no effect.

---

## Data plane (OE → Bonsai)

| Capability | Status | Notes |
|---|---|---|
| `RAW_BLOCK` publish | Working | One block per acquisition callback. |
| Sample scaling | Working | Converted from OE's microvolt floats to `int16` ADC counts via each channel's `bitVolts`. Consumers multiply by `bitVolts` to recover microvolts. |
| Shared-memory transport | Working | Lock-free SPSC ring, wait-free producer, sub-millisecond. |
| ZMQ transport | Working | PUB/SUB data + REQ/REP commands. Loopback by default; non-loopback requires CURVE on **both** ends. |
| `SYNC` heartbeat | Working | 1 Hz on the data ring, carries clock frequency + sample rate. |
| Drift correction | Working | 60-point least-squares fit; `SampleToHostTime` anchors it to UTC. |
| Auto-discovery | Working | Sidecar JSON + shared-region heartbeat liveness check. One sidecar per OEconnect node (`<pid>-<node_id>.json`); with several nodes in a chain, give each Bonsai source an explicit `Endpoint`. |
| Frame bounds validation | Working | Untrusted frames are length-checked before any copy. |
| `BIT_LOST_DATA` flag (§4.3) | Working | Armed by any drop (ring-full eviction or oversized frame) and stamped on the next published frame, exactly once. Bonsai's `SessionStatus.DropCount` counts gaps; the OE editor shows total frames lost. Ring-full evictions were previously uncounted entirely — `oec_ringbuf_acquire(drop_oldest=1)` returns a slot rather than NULL, so the new `oec_ringbuf_evictions()` counter is what makes them observable. |
| Frame magic/version check on receive (§2.6, §8.4) | Working | Both sides call `oec_frame_validate()` before touching any field. The plugin refuses a command whose major differs and answers with `ERROR(PROTOCOL_VERSION_MISMATCH)`; the consumer drops the frame, counts it in `InvalidFrameCount`, and raises a fatal `OnError` on a major mismatch. |
| `BIT_CONTINUATION` multi-slot frames (§4.3) | **Missing** | Frames larger than one slot should span up to 4 slots, else `ERROR(FRAME_TOO_LARGE)`. Instead they are dropped and counted. |
| `ERROR` frame handling | Working | Producer emits `{code_u16, utf8_len_u16, utf8_msg[]}` per §3.1 (the previous hand-rolled emission omitted the length field). Consumer parses it into `SessionStatus.LastErrorCode` / `.LastErrorMessage`. Codes are now enumerated in spec §3.1. |
| `SYNC` `stream_meta[]` triples (§3.1) | **Missing** | `SYNC` carries `qpc_freq_hz` + `fpga_sample_rate_hz` only; the per-source `{source_id, n_channels, sample_rate}` array is not emitted. |
| CRC-16 (§2.1, optional) | **Missing** | `oec_crc16()` exists and is unit-tested but is never computed or verified on the hot path. `crc16` is always 0, which the spec permits. |
| `FILTERED_BLOCK` publish | Working | The node publishes its input **once**, under the stream id chosen by the *Stream label* parameter (`Raw` or `Filtered`). It does not filter — the label must describe where you placed the node. For both streams, branch the chain and run two OEconnect nodes. |
| `SPIKE` publish | Working | Republished from OE's event bus via `checkForEvents(true)` + `handleSpike()`. Requires an upstream Spike Detector/sorter — OEconnect does not detect spikes. Waveforms scaled to int16 ADC counts by each spike channel's `bitVolts`. |
| `TTL_EVENT` publish | Working | Republished from OE's event bus via `checkForEvents()` + `handleTTLEvent()`. Covers board digital inputs and upstream event generators. |

## Control plane (Bonsai → OE)

| Capability | Status | Notes |
|---|---|---|
| `START_RECORD` / `STOP_RECORD` | Working | Dispatched to a worker thread; the acquisition thread never blocks. |
| Command bounds validation | Working | Every field length-checked against the frame before use. |
| ACK / cookie correlation | Working | Acks drained on both transports and matched by cookie. |
| `PULSE_TTL` auto-clear scheduling | Working | Falling edge scheduled by FPGA sample index, serviced each callback. |
| `START_RECORD` directory + prefix | Working | Directory applied to every Record Node. `Prefix` maps to OE's recording-directory *prepend text* (OE has no per-file prefix). Empty fields leave the GUI's settings alone. |
| `SetTtl` / `PulseTtl` → TTL event | Working | Published on OE's event bus via `addTTLChannel()` + `setTTLState()`. Board-agnostic. |
| TTL echo into the OE recording | Working | The same event is captured by any downstream Record Node, satisfying the "complete record" guarantee in [architecture-rules.md](architecture-rules.md). |
| `SetTtl` / `PulseTtl` → physical line | Working, **requires a downstream output plugin** | The GUI has no cross-board API for driving a digital output directly (`setTTLOutputBit` does not exist). Place **Acq Board Output**, **Arduino Output**, or **Pulse Pal** downstream of OEconnect; it converts the event into a line. This is the same mechanism Crossing Detector and Ripple Detector use. |
| Concurrent commands → `ACK(BUSY)` (§5.5) | Working | The plugin admits one slow command (record/acq) at a time; a second arriving mid-flight gets `ACK(BUSY)` rather than being queued behind the first. The slot is held until the dispatcher *returns*, not merely until dequeue. Bonsai serialises `PostCmd` under a lock — the cmd ring is single-producer and NetMQ sockets are not thread-safe, yet several operators post from different Rx threads. |
| Drift divergence reset (§2.5) | Working | After each fit the residual RMS is converted to microseconds using the SYNC-reported clock frequency; above 5 µs the window is reset and `SessionStatus.DriftResetCount` increments. Deliberately not an `OnError` — drift glitches are expected. |
| ZMQ CURVE, both ends (§5.7) | Working | Plugin refuses an unauthenticated non-loopback bind; `ZmqClient` mirrors it, requiring `OEC_ZMQ_CURVE_SERVER_PUBLIC` for any non-loopback endpoint and failing closed otherwise. Client keypair from `OEC_ZMQ_CURVE_SECRET`, else ephemeral. |
| ZMQ `RCVHWM`, REQ heartbeat, disconnect `OnError` (§5.4/§5.6) | Working | SUB sets `ReceiveHighWatermark = 4096`; REQ sets a 500 ms heartbeat with a 2 s timeout. A `NetMQMonitor` disconnect, or an unanswered command, raises `ConnectionLost`, which the Session turns into `OnError` on the sources — once, so a flapping peer cannot spam it. |
| Multi-`DataStream` sources | **Partial** | `process()` flattens every channel of every DataStream into one block using `buffer.getNumSamples()`. A source with several streams at different rates (e.g. Neuropixels AP + LFP) is mis-shaped. Should emit one block per stream, keyed by `source_id`. |
| Direct board trigger | Working, opt-in | With the *Direct board trigger* parameter on, a **pulse** additionally broadcasts `ACQBOARD TRIGGER <line> <ms>`, so the acquisition board fires it without waiting on the downstream output plugin. Pulses only — the grammar cannot latch a line, so `SetTtl` always goes via the event bus. Boards that don't implement `handleBroadcastMessage()` ignore it. |

## Configuration

| Capability | Status | Notes |
|---|---|---|
| OE editor parameters | Working | Declared as OE `Parameter`s: tooltips from their descriptions, saved/restored with the signal chain, locked during acquisition where changing them mid-run would be unsafe. |
| Transport / stream / ZMQ bind + ports | Working | Wired to the running configuration. |
| Ring slot size / slot count | Working | Editor-configurable per spec §4.7 (`slot_size` 64 KiB–1 MiB, `slot_count` 64–1024). Categorical choices, so `slot_count` is always a power of two — `oec_region_size()` rejects anything else and the transport refuses to start. The consumer reads geometry from the region header. Command/ack rings stay at defaults (sparse traffic). |
| Block size | **Not configurable** | Dictated by OE's acquisition callback, not by us. |

## OE GUI compatibility

| Capability | Status | Notes |
|---|---|---|
| GUI 1.0.x (plugin API v10) | Working | Default target; the vendored submodule. |
| GUI 0.6.x (plugin API v8) | Working | Built and link-verified against a real v0.6.7 checkout via `Source/Compat/OECompat.h`. |
| GUI 0.5.x / 0.4.x | **Not supported** | Predate `DataStream` and the `Parameter` class. See [oe-version-compatibility.md](oe-version-compatibility.md). |
| Board support | Working, board-agnostic | No board-specific code. TTL is an event; a downstream output plugin drives hardware, so any board with an output companion works. |

One DLL per plugin API version — the GUI hard-rejects a mismatch. `build-oe-plugin.ps1`
emits `dist-oe-plugin/api-v<N>/OEconnect.dll`.

---

## Known gaps, in rough priority order

1. **`FILTERED_BLOCK` is a passthrough.** Either filter in the plugin or rename
   the stream to reflect that it mirrors the upstream chain.
2. **Pre-0.6 GUI lines (0.4.x / 0.5.x) are unsupported.** They predate
   `DataStream` and the `Parameter` class, on which this plugin is built.
   See [oe-version-compatibility.md](oe-version-compatibility.md).
3. **`wakeup_batch_K` (§4.4/§4.7) is not implemented.** The consumer uses adaptive
   spin rather than named events; the spec marks wakeups optional.

### Resolved

- ~~Board adapters are stubs~~ — replaced by `EventBusTtlAdapter`. There was never
  a `setTTLOutputBit` API to call; the correct mechanism is a TTL event plus a
  downstream output plugin, which works for every board with no board-specific code.
- ~~TTL echo not recorded~~ — the event bus carries it to Record Nodes.
- ~~Two OEconnect nodes collide on one shm region~~ — the region and sidecar are now
  scoped by OE node id, so each node owns its own single-producer rings.
- ~~`RAW_BLOCK` could carry filtered data~~ — one node now publishes its input once,
  under an explicit *Stream label*.
- ~~Oversized blocks vanished silently~~ — now counted via `ITransport::noteDropped()`,
  and `slot_size` can be raised to admit them (§4.7).
- ~~No `SPIKE` / `TTL_EVENT` emission~~ — both republished from OE's event bus.
  Also fixed a consumer bug: the spike parser sized the waveform from the ring
  *slot* size rather than the frame's `payload_len`.

## What is verified by tests

| Suite | Count | Covers |
|---|---|---|
| `libshared` (C) | 35 | Ring buffer SPSC + eviction accounting, shared memory (incl. per-node region naming), frame codec, drift fit, sidecar |
| Plugin (C++) | 25 | Ack outbox, both transports, hot-path block emission, command drain, TTL_EVENT + SPIKE emission, stream labelling, oversize + eviction drop accounting, LOST_DATA flag arming, command frame validation, slow-command BUSY gating (incl. slot held until the dispatcher returns) |
| Bonsai (C#) | 33 | Interop, HELLO negotiation matrix, command builder, CURVE client policy, frame validation, ERROR frame parsing, drift residual/reset semantics, non-default ring geometry, end-to-end shared-memory round trip |

The end-to-end round trip drives a real synthetic producer over shared memory and
asserts that `RawSamples` delivers blocks, so the data path is covered from the
ring buffer up through the Bonsai operator.
