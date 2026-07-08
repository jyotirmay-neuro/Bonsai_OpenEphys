# Configuring OEconnect

How to set up the OpenEphys plugin and the Bonsai nodes, what every option means,
and which combinations are valid.

Read this alongside [status.md](status.md), which records exactly which parts of
the bridge are functional today. Several controls described here are wired and
working; a few connect to code paths that are still stubs, and those are called
out explicitly.

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

### Stream raw

Publish the unprocessed broadband block on every acquisition callback
(`RAW_BLOCK`). This is what Bonsai's `RawSamples` node receives. **On by default** —
turn it off only if you exclusively want the filtered stream.

### Stream filtered

Publish a second copy of the incoming block tagged `FILTERED_BLOCK`, received by
Bonsai's `FilteredSamples`. **Off by default.**

> **The plugin does not filter anything.** It forwards whatever the upstream OE
> chain already produced. To get a genuinely filtered stream, put a **Bandpass
> Filter** node *before* OEconnect in the OE signal chain. With no upstream
> filter, this option merely duplicates the raw stream at double the bandwidth.

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

Read-only, refreshed twice a second: the active transport, the detected board
adapter, and the cumulative dropped-frame count. **Dropped frames climbing means
Bonsai is not draining fast enough** — simplify the downstream workflow, or accept
the loss (the plugin never blocks acquisition to wait for a consumer).

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

---

## 4. Bonsai nodes

Every node has an `Endpoint` property with identical semantics:

| `Endpoint` | Behaviour |
|------------|-----------|
| *(empty — default)* | **Auto-discovery.** Scans `%TEMP%\oeconnect\sessions\` for the newest session and verifies its heartbeat is fresh. Use this unless you run several OE instances. |
| `shm://Local\oeconnect.<pid>.shm` | Attach to a specific shared-memory region. Same machine only. |
| `tcp://host:5557\|tcp://host:5558` | Connect over ZMQ. Note the `\|` separating data and command endpoints — **both are required**. |

All nodes sharing an endpoint share **one underlying connection** (reference
counted), so adding a second source node costs nothing.

### Sources (data flowing OE → Bonsai)

| Node | Emits | Status |
|------|-------|--------|
| `RawSamples` | One `RawBlock` per callback (32 samples ≈ 1.07 ms at 30 kHz) | **Working** |
| `FilteredSamples` | `RawBlock` from the `FILTERED_BLOCK` stream | Working, but see *Stream filtered* above |
| `SyncPoints` | One `SyncPoint` per second (sample index ↔ host clock) | **Working** |
| `OpenEphysSession` | One `SessionStatus` per second (liveness, frame/drop counts) | **Working** |
| `Spikes` | `SpikeEvent` | **Not functional** — the plugin never emits `SPIKE` frames |
| `TtlEvents` | `TtlEvent` | **Not functional** — the plugin never emits `TTL_EVENT` frames |

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
| `SetTtl` | Latches a TTL line high or low | Command round-trips, **but no board adapter drives hardware yet** |
| `PulseTtl` | Drives a line high, auto-clears after `WidthMicroseconds` | Same caveat as `SetTtl` |

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
acquisition is actually running and that **Stream raw** is enabled.

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
- `FilteredSamples` with **Stream filtered** off — subscribes fine, emits nothing.

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
