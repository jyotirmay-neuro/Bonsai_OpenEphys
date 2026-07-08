# Configuring OEconnect

How to set up the OpenEphys plugin and the Bonsai nodes, what every option means,
and which combinations are valid.

Read this alongside [status.md](status.md), which records exactly which parts of
the bridge are functional today. Every control described here is wired; where a
feature depends on how you arrange the *OE signal chain* (spike detection, TTL
output), that requirement is called out explicitly.

---

## 1. The two halves

| Half | What it is | Where it runs |
|------|------------|---------------|
| `OEconnect.dll` | An Open Ephys GUI **sink** processor. Publishes sample blocks and executes commands. | The OE GUI process |
| `Bonsai.OEconnect` | A Bonsai package of source/sink operators. Subscribes to blocks and issues commands. | The Bonsai process |

Data always flows **OE → Bonsai**. Commands always flow **Bonsai → OE**. Bonsai
never touches acquisition-board firmware directly — every hardware action crosses
the plugin, so the OE recording stays a complete record of everything that happened
(see [architecture-rules.md](architecture-rules.md)).

---

## 2. OpenEphys plugin settings

Drop the **OEconnect** sink into the signal chain, downstream of your acquisition
source. Every control below is an OE *Parameter*: it shows its description as a
tooltip on hover, and it is saved and restored with the signal chain.

Controls marked **locked during acquisition** are greyed out while acquisition is
running — stop acquisition to change them.

### Transport — *locked during acquisition*

How samples reach Bonsai.

| Value | Meaning | Latency | Constraint |
|-------|---------|---------|------------|
| `Auto` *(default)* | Shared memory if it can be created, otherwise fall back to ZMQ | — | — |
| `SharedMem` | Lock-free shared-memory ring buffer | **sub-millisecond** | Bonsai must run on the **same machine** |
| `Zmq` | TCP sockets (PUB/SUB for data, REQ/REP for commands) | 1–5 ms typical, 5–20 ms tail | Works across machines |

`Auto` and `SharedMem` behave identically today: both try shared memory first and
fall back to ZMQ if the region cannot be created.

> If you need the sub-millisecond guarantee, you need `SharedMem`. ZMQ is a
> soft-real-time tier and cannot meet a 1 ms budget.

### Stream continuous

Publish this node's incoming continuous block on every acquisition callback.
**On by default.** Turn it off to use OEconnect purely as an event/command bridge.

### Stream label — *locked during acquisition*

Which stream id to stamp on that block: `Raw` feeds Bonsai's `RawSamples`,
`Filtered` feeds `FilteredSamples`.

> **The plugin does not filter, and it cannot see a raw copy.** It receives exactly
> one buffer: whatever the upstream OE chain handed it. So this setting does not
> *select* a signal — it *labels* the one signal this node sees. Put OEconnect
> after a Bandpass Filter and label it `Filtered`; put it straight after the source
> and label it `Raw`. Mislabelling silently hands Bonsai filtered data through
> `RawSamples`.

#### Getting raw **and** filtered at the same time

Branch the OE chain and run two OEconnect nodes:

```
[Source] ─┬─────────────────────────► [OEconnect: Raw]
          └─► [Bandpass Filter] ────► [OEconnect: Filtered]
```

Each node owns its own shared-memory region (scoped by OE node id), so they do not
collide. Auto-discovery cannot tell them apart, so give each Bonsai source an
**explicit `Endpoint`** — read the two `shm_region` values out of
`%TEMP%\oeconnect\sessions\<pid>-<node_id>.json`. Each block's `source_id` in the
subheader also carries the emitting node's id.

### Stream spikes

Republish spike events crossing this node as `SPIKE` frames (Bonsai's `Spikes`).
**On by default.**

> **OEconnect does not detect spikes.** It forwards what OE's event bus already
> carries, so you need a **Spike Detector** (or sorter) *upstream* of OEconnect:
> `[Source] → [Spike Detector] → [OEconnect]`. Without one this node never fires.
> Waveforms arrive as int16 ADC counts, channel-major.

### Stream TTL events

Republish TTL edges crossing this node as `TTL_EVENT` frames (Bonsai's
`TtlEvents`). Covers board digital inputs and any upstream event generator.
**On by default.**

Edges that OEconnect *itself* issues (`SetTtl` / `PulseTtl`) go downstream and into
the recording, but are not echoed back through this node.

### Ring slot size / Ring slot count — *locked during acquisition*

Shared-memory ring geometry (spec §4.7). Ignored when Transport is `Zmq`.

| Control | Default | Meaning |
|---|---|---|
| **Ring slot size** | 64 KiB | Largest frame this node can publish. Raise it for wide probes: a block needs `40 + n_channels × n_samples × 2` bytes, so 64 KiB caps you at 1023 channels at a 32-sample block. |
| **Ring slot count** | 256 | How many frames the ring holds — how long Bonsai may stall before the oldest unread frame is overwritten. ≈270 ms at a 32-sample block. |

Both are offered as fixed choices rather than free text: `slot_count` must be a
power of two, and RAM cost is `slot_size × slot_count` (the default is 16 MiB).
Depth costs memory, never latency — the producer never waits for a consumer.

### ZMQ bind address — *locked during acquisition*

Which network interface the ZMQ sockets bind to. **Ignored when Transport is
`SharedMem`.**

| Value | Effect |
|-------|--------|
| `127.0.0.1` *(default)* | Loopback only. Data and commands never leave this machine. No authentication required. |
| `0.0.0.0` or any routable address | Reachable from the network. **Refused unless CURVE authentication is configured** (below). |

### ZMQ data port / ZMQ command port — *locked during acquisition*

Defaults `5557` (data, PUB socket) and `5558` (commands, REP socket). They must
differ. Bonsai's endpoint string lists both: `tcp://host:5557|tcp://host:5558`.

### Status line

Read-only, refreshed twice a second: the active transport, the detected board, and
the cumulative dropped-frame count.

Dropped frames climb for two different reasons:

- **Bonsai is not draining fast enough** — the ring filled and the oldest slot was
  evicted. Simplify the downstream workflow, or accept the loss (the plugin never
  blocks acquisition to wait for a consumer).
- **The block does not fit one ring slot.** A frame needs
  `40 + n_channels x n_samples x 2` bytes, so the default 64 KiB slot tops out at
  **1023 channels** at a 32-sample block. Exceed it and *every* frame is dropped —
  raise **Ring slot size** (below).

Both kinds arm the `BIT_LOST_DATA` header flag on the next frame the plugin
publishes, so Bonsai's `SessionStatus.DropCount` counts the **gaps**, while the OE
editor shows the total **frames** lost. A block that never fits a slot publishes
nothing at all, so there is no next frame to carry the flag: Bonsai sees silence
and only the OE editor's readout climbs.

---

## 3. Network security

Exposing raw neural data and a *command channel* — which can start/stop recording
and fire TTL lines — to a network without authentication is not something the
plugin will do silently.

- **Loopback (`127.0.0.1`)** — no authentication needed. This is the default.
- **Any other bind address** — the plugin **refuses to start** unless the
  environment variable `OEC_ZMQ_CURVE_SECRET` holds a 40-character Z85 CURVE
  secret key. When set, both sockets run as CURVE servers and clients must present
  the matching public key.

Set it on the **OE machine**, before launching the GUI:

```powershell
$env:OEC_ZMQ_CURVE_SECRET = "<40-char Z85 secret key>"
```

There is no "just let me through" switch. A non-loopback bind without a key fails
closed.

### On the Bonsai machine

Bonsai applies the mirror-image policy: connecting to a **non-loopback** endpoint
requires the plugin's **public** key, or it throws rather than attempt a handshake
the plugin would reject.

```powershell
# Required for any non-loopback endpoint: the plugin's 40-char Z85 PUBLIC key
$env:OEC_ZMQ_CURVE_SERVER_PUBLIC = "<40-char Z85 public key>"

# Optional: this client's own secret key. Omit and an ephemeral keypair is
# generated — CURVE still authenticates the server and encrypts the channel.
$env:OEC_ZMQ_CURVE_SECRET = "<40-char Z85 secret key>"
```

Keys live in environment variables, never in a saved workflow. Loopback endpoints
need neither variable, matching the plugin's exemption.

---

## 4. Bonsai nodes

Every node has an `Endpoint` property with identical semantics:

| `Endpoint` | Behaviour |
|------------|-----------|
| *(empty — default)* | **Auto-discovery.** Scans `%TEMP%\oeconnect\sessions\` for the newest session and verifies its heartbeat is fresh. Ambiguous if several OEconnect nodes run in one chain. |
| `shm://Local\oeconnect.<pid>.<node_id>.shm` | Attach to a specific shared-memory region. Same machine only. Required when several OEconnect nodes run in one chain. |
| `tcp://host:5557\|tcp://host:5558` | Connect over ZMQ. Note the `\|` separating data and command endpoints — **both are required**. |

All nodes sharing an endpoint share **one underlying connection** (reference
counted), so adding a second source node costs nothing.

### Sources (data flowing OE → Bonsai)

| Node | Emits | Status |
|------|-------|--------|
| `RawSamples` | One `RawBlock` **per DataStream** per callback | **Working** |
| `FilteredSamples` | `RawBlock` from the `FILTERED_BLOCK` stream | Working — from an OEconnect node labelled `Filtered` (see *Stream label*) |
| `SyncPoints` | One `SyncPoint` per second (sample index ↔ host clock) | **Working** |
| `OpenEphysSession` | One `SessionStatus` per second (liveness, frame/drop counts) | **Working** |
| `Spikes` | `SpikeEvent` | **Working** — needs a Spike Detector *upstream* of OEconnect |
| `TtlEvents` | `TtlEvent` | **Working** — board digital inputs + upstream event generators |

> **Multi-stream sources.** A Neuropixels probe presents AP (30 kHz) and LFP
> (2.5 kHz) as separate OE DataStreams. OEconnect publishes one block per stream, so
> `RawSamples` interleaves blocks with different `SourceId`, channel counts and
> sample clocks. Filter on `SourceId` to isolate one stream; look it up in
> `SyncPoint.Streams` (or `Session.Streams`) to get its channel count and rate.
> Single-stream sources always emit `SourceId = 0` and need no special handling.

### Transforms

| Node | Purpose |
|------|---------|
| `ToMat` | `RawBlock` → OpenCV `Mat` (rows = channels, cols = samples). **Attach visualizers here.** |
| `SampleToHostTime` | Pairs a block with drift-corrected wall-clock time |

### Sinks (commands flowing Bonsai → OE)

| Node | Effect | Status |
|------|--------|--------|
| `StartRecording` | Starts every Record Node in the OE chain | **Working** |
| `StopRecording` | Stops recording; acquisition continues | **Working** |
| `SetTtl` | Latches a TTL line high or low | **Working** — needs a downstream output plugin (below) |
| `PulseTtl` | Drives a line high, auto-clears after `WidthMicroseconds` | **Working** — same, or use *Direct board trigger* |

### Getting TTL to actual hardware

The OE GUI has **no cross-board API** for a plugin to set a digital output line
directly. The supported mechanism is a TTL *event*, which a downstream **output
plugin** converts into a physical line:

```
[Acquisition Board] → [OEconnect] → [Acq Board Output]
                          │                  │
                    emits TTL event    event → physical line
```

OEconnect always emits the event (that is also what puts it in the OE recording).
Put one of these downstream and point it at the line OEconnect drives:

| Output plugin | Drives |
|---|---|
| **Acq Board Output** | Open Ephys / Intan acquisition board digital outs |
| **Arduino Output** | An Arduino pin (lower latency) |
| **Pulse Pal** | Pulse Pal channels (lowest latency) |

This is the same path Crossing Detector and Ripple Detector use, so **any board
with an output companion plugin works, with no board-specific code**.

**Direct board trigger** (plugin parameter, off by default) additionally broadcasts
`ACQBOARD TRIGGER <line> <ms>` so the acquisition board fires the pulse itself,
skipping the output plugin's response time. Caveats:

- **Pulses only.** The command grammar carries a duration and cannot latch a line,
  so `SetTtl` always goes via the event bus.
- Only delivered **while acquisition is active**.
- Boards that don't implement `handleBroadcastMessage()` ignore it — harmless.
- The event is still emitted first, so the recording stays complete.

---

## 5. Seeing your data

`RawSamples` emits a `RawBlock` struct, which Bonsai will not plot directly.
Convert it first:

```
RawSamples  →  ToMat  →  (right-click ToMat → Visualizer)
```

`ToMat` produces an OpenCV `Mat` with **rows = channels, cols = samples**, which
the Bonsai.Dsp matrix/waveform visualizers render as live traces.

**Units.** Samples are `int16` **ADC counts**, not microvolts. Multiply by the
channel's `bitVolts` (shown in the OE GUI) to convert. The waveform shape is
identical either way, so for a quick "is data flowing?" check the raw counts are
fine.

**Confirming the pipeline is healthy** — drop in an `OpenEphysSession` node and
watch:

- `IsConnected` true
- `FrameCount` rising steadily (≈940 /s at 30 kHz with 32-sample blocks)
- `DropCount` at zero

If `FrameCount` is stuck at 0, the plugin is not publishing — check that OE
acquisition is running and **Stream continuous** is enabled. If `DropCount` is
climbing while `FrameCount` stays at 0, your block exceeds one 64 KiB ring slot
(see *Status line*).

---

## 6. Compatibility matrix

| Want | Transport | Bind address | Auth | Notes |
|------|-----------|--------------|------|-------|
| Lowest latency, same machine | `SharedMem` | *(n/a)* | *(n/a)* | The only option under 1 ms |
| Bonsai on another machine | `Zmq` | routable | **CURVE required** | 1–5 ms typical |
| Quick local dev over TCP | `Zmq` | `127.0.0.1` | none | Loopback is exempt |
| Two OE instances, one Bonsai | either | — | — | Give each Bonsai node an explicit `Endpoint`; auto-discovery picks only the newest |

Invalid combinations:

- `SharedMem` with Bonsai on a **different machine** — the region is host-local.
  Use `Zmq`.
- Non-loopback bind **without** `OEC_ZMQ_CURVE_SECRET` — refused at startup.
- Same port for data and commands — the second bind fails.
- `FilteredSamples` with no OEconnect node labelled `Filtered` — subscribes fine, emits nothing.
- Two OEconnect nodes plus empty `Endpoint` — auto-discovery picks one arbitrarily; set explicit endpoints.
- `Spikes` with no Spike Detector **upstream** of OEconnect — never fires.
- `SetTtl`/`PulseTtl` with no output plugin **downstream** — event is recorded, no physical line moves.

---

## 7. Latency

| Path | Budget |
|------|--------|
| Plugin writes a block into shared memory | sub-microsecond, wait-free |
| Bonsai's reader picks it up | sub-millisecond (adaptive spin) |
| Bonsai command → plugin applies it | up to one acquisition callback (~1.07 ms at 30 kHz) |
| Same, over ZMQ | 1–5 ms typical, 5–20 ms tail |

The ~1.07 ms callback period is the floor for command application: the plugin
drains the command ring at the top of each `process()` tick. Nothing crosses that
boundary faster. Measurements live in
[perf/closed_loop_latency.md](perf/closed_loop_latency.md).

---

## 8. Troubleshooting

**No OEconnect nodes in the Bonsai toolbox.** The package's NuGet tags must
contain `Bonsai` (capital B) or Bonsai installs it without registering the
assembly. Check that `Bonsai.config` contains
`<AssemblyReference assemblyName="Bonsai.OEconnect" />`.

**OE GUI crashes when adding the node.** You are on a build predating the
`createEditor()` ownership fix. Rebuild the plugin.

**Bonsai throws "no live producer (stale heartbeat)".** The shared-memory region
exists but the plugin that created it died. Start OE acquisition, or delete the
stale sidecar in `%TEMP%\oeconnect\sessions\`.

**Signal looks like a clipped square wave.** You are on a build predating the
`bitVolts` scaling fix, which treated OE's microvolt floats as normalized audio.
Rebuild the plugin.

**`SampleToHostTime` returns 1/1/0001.** No clock fit yet — it needs a couple of
`SYNC` frames (~2 s after the session starts).
