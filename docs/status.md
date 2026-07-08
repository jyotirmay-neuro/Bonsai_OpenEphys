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
| Auto-discovery | Working | Sidecar JSON + shared-region heartbeat liveness check. |
| Frame bounds validation | Working | Untrusted frames are length-checked before any copy. |
| `FILTERED_BLOCK` publish | **Partial** | The plugin performs **no filtering**. It re-publishes the incoming block under a different stream id. Put a Bandpass Filter upstream of OEconnect for this to mean anything. Off by default. |
| `SPIKE` publish | **Missing** | The plugin never emits `SPIKE` frames. Bonsai's `Spikes` node subscribes but never fires. |
| `TTL_EVENT` publish | **Missing** | The plugin never emits `TTL_EVENT` frames. Bonsai's `TtlEvents` node subscribes but never fires. |

## Control plane (Bonsai → OE)

| Capability | Status | Notes |
|---|---|---|
| `START_RECORD` / `STOP_RECORD` | Working | Dispatched to a worker thread; the acquisition thread never blocks. |
| Command bounds validation | Working | Every field length-checked against the frame before use. |
| ACK / cookie correlation | Working | Acks drained on both transports and matched by cookie. |
| `PULSE_TTL` auto-clear scheduling | Working | Falling edge scheduled by FPGA sample index, serviced each callback. |
| `START_RECORD` file-name prefix | **Partial** | Transmitted over the wire, but the plugin does not apply it. The OE GUI's naming settings win. |
| `SetTtl` / `PulseTtl` hardware effect | **Stub** | Every board adapter (`RhdAcqBoardAdapter`, `OnixAdapter`, `NeuropixelsAdapter`) is a `TODO` that no-ops and returns a synthetic sample counter. Commands round-trip and are acknowledged, but **no physical line moves**. |
| TTL echo into the OE recording | **Stub** | `on_ttl_emit` never calls `addEvent()`, so Bonsai-issued TTLs are not captured in the OE recording. This currently breaks the "OE recording is a complete record" guarantee in [architecture-rules.md](architecture-rules.md). |

## Configuration

| Capability | Status | Notes |
|---|---|---|
| OE editor parameters | Working | Declared as OE `Parameter`s: tooltips from their descriptions, saved/restored with the signal chain, locked during acquisition where changing them mid-run would be unsafe. |
| Transport / stream / ZMQ bind + ports | Working | Wired to the running configuration. |
| Block size, slot size, slot count | **Not configurable** | Block size is dictated by OE's acquisition callback. Ring geometry is compile-time (64 KiB × 256 slots), despite what spec §4.7 implies. |

---

## Known gaps, in rough priority order

1. **Board adapters are stubs.** No TTL output reaches hardware. This is the
   single biggest gap: closed-loop stimulation does not work end to end. Each
   adapter needs wiring to its board's real SDK call
   (`setTTLOutputBit` for Rhythm, `oni_write_reg` for ONIX).
2. **TTL echo not recorded.** `on_ttl_emit` must call `addEvent()` so the OE
   recording captures every edge the bridge issues.
3. **No `SPIKE` / `TTL_EVENT` emission.** Both Bonsai nodes exist and parse
   correctly; the producer side is missing.
4. **`FILTERED_BLOCK` is a passthrough.** Either filter in the plugin or rename
   the stream to reflect that it mirrors the upstream chain.
5. **Ring geometry is not editor-configurable** even though the spec says it is.

## What is verified by tests

| Suite | Count | Covers |
|---|---|---|
| `libshared` (C) | 33 | Ring buffer SPSC + eviction, shared memory, frame codec, drift fit, sidecar |
| Plugin (C++) | 12 | Ack outbox, both transports, hot-path block emission, command drain |
| Bonsai (C#) | 10 | Interop, HELLO negotiation matrix, command builder, end-to-end shared-memory round trip against a synthetic producer |

The end-to-end round trip drives a real synthetic producer over shared memory and
asserts that `RawSamples` delivers blocks, so the data path is covered from the
ring buffer up through the Bonsai operator.
