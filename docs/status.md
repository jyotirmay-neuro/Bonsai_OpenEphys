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
| ZMQ transport | Working | PUB/SUB data + REQ/REP commands. Loopback by default; non-loopback requires CURVE. |
| `SYNC` heartbeat | Working | 1 Hz on the data ring, carries clock frequency + sample rate. |
| Drift correction | Working | 60-point least-squares fit; `SampleToHostTime` anchors it to UTC. |
| Auto-discovery | Working | Sidecar JSON + shared-region heartbeat liveness check. One sidecar per OEconnect node (`<pid>-<node_id>.json`); with several nodes in a chain, give each Bonsai source an explicit `Endpoint`. |
| Frame bounds validation | Working | Untrusted frames are length-checked before any copy. |
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
| Direct board trigger | Working, opt-in | With the *Direct board trigger* parameter on, a **pulse** additionally broadcasts `ACQBOARD TRIGGER <line> <ms>`, so the acquisition board fires it without waiting on the downstream output plugin. Pulses only — the grammar cannot latch a line, so `SetTtl` always goes via the event bus. Boards that don't implement `handleBroadcastMessage()` ignore it. |

## Configuration

| Capability | Status | Notes |
|---|---|---|
| OE editor parameters | Working | Declared as OE `Parameter`s: tooltips from their descriptions, saved/restored with the signal chain, locked during acquisition where changing them mid-run would be unsafe. |
| Transport / stream / ZMQ bind + ports | Working | Wired to the running configuration. |
| Block size, slot size, slot count | **Not configurable** | Block size is dictated by OE's acquisition callback. Ring geometry is compile-time (64 KiB × 256 slots), despite what spec §4.7 implies. |

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
3. **Ring geometry is not editor-configurable** even though spec §4.7 says it is.
   A slot is 64 KiB, so a block needing `40 + n_channels x n_samples x 2` bytes
   above that cannot be published — at the usual 32-sample block that caps you at
   1023 channels. Such frames are now *counted* as drops rather than vanishing
   silently, but the geometry still cannot be raised.

### Resolved

- ~~Board adapters are stubs~~ — replaced by `EventBusTtlAdapter`. There was never
  a `setTTLOutputBit` API to call; the correct mechanism is a TTL event plus a
  downstream output plugin, which works for every board with no board-specific code.
- ~~TTL echo not recorded~~ — the event bus carries it to Record Nodes.
- ~~Two OEconnect nodes collide on one shm region~~ — the region and sidecar are now
  scoped by OE node id, so each node owns its own single-producer rings.
- ~~`RAW_BLOCK` could carry filtered data~~ — one node now publishes its input once,
  under an explicit *Stream label*.
- ~~Oversized blocks vanished silently~~ — now counted via `ITransport::noteDropped()`.
- ~~No `SPIKE` / `TTL_EVENT` emission~~ — both republished from OE's event bus.
  Also fixed a consumer bug: the spike parser sized the waveform from the ring
  *slot* size rather than the frame's `payload_len`.

## What is verified by tests

| Suite | Count | Covers |
|---|---|---|
| `libshared` (C) | 34 | Ring buffer SPSC + eviction, shared memory (incl. per-node region naming), frame codec, drift fit, sidecar |
| Plugin (C++) | 16 | Ack outbox, both transports, hot-path block emission, command drain, TTL_EVENT + SPIKE emission, stream labelling, oversize-drop accounting |
| Bonsai (C#) | 10 | Interop, HELLO negotiation matrix, command builder, end-to-end shared-memory round trip against a synthetic producer |

The end-to-end round trip drives a real synthetic producer over shared memory and
asserts that `RawSamples` delivers blocks, so the data path is covered from the
ring buffer up through the Bonsai operator.
