# Bonsai.OEconnect

NuGet package providing Bonsai-rx operators for OpenEphys streaming and control.

Talks to the OEconnect OE GUI plugin over shared memory (same machine,
sub-millisecond) or ZeroMQ (cross-machine, 1–5 ms). See
[`docs/configuration.md`](../../docs/configuration.md) for options and
[`docs/status.md`](../../docs/status.md) for what is functional.

## Build

The package embeds the native `oeconnect.dll`, so build `libshared` first —
`Bonsai.OEconnect.csproj` picks it up from
`libshared/oeconnect/build/Release/oeconnect.dll`.

```powershell
cmake -S ../../libshared/oeconnect -B ../../libshared/oeconnect/build
cmake --build ../../libshared/oeconnect/build --config Release

dotnet build src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release
dotnet test  tests/Bonsai.OEconnect.Tests -c Release
dotnet pack  src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release -o ../../dist/nupkg
```

Targets `net472` (the framework Bonsai runs on) and `net6.0`.

> `PackageTags` **must** contain `Bonsai` with a capital B. Bonsai matches that
> tag case-sensitively to decide whether to register the assembly in
> `Bonsai.config`. With a lowercase tag the package installs and the DLL
> resolves, but **no operators appear in the toolbox**.

## Install

See [`docs/install.md`](../../docs/install.md). Briefly: add `dist/nupkg` as a
package source in **Tools ▸ Manage Packages**, install `Bonsai.OEconnect`,
restart Bonsai.

## Operators

All operators expose an `Endpoint` property. Leave it **empty** for
auto-discovery of the running OE session; all nodes sharing an endpoint share a
single connection.

| Category | Operator | Purpose | Status |
|---|---|---|---|
| Source | `RawSamples` | Continuous broadband blocks | **Working** |
| Source | `FilteredSamples` | The `FILTERED_BLOCK` stream | **Working** — from an OEconnect node whose *Stream label* is `Filtered` |
| Source | `SyncPoints` | 1 Hz sample-index ↔ host-clock pairs | **Working** |
| Source | `OpenEphysSession` | 1 Hz liveness / frame / drop counts | **Working** |
| Source | `Spikes` | Spike events | **Working** — needs an upstream Spike Detector in the OE chain |
| Source | `TtlEvents` | TTL edges | **Working** — board digital inputs + upstream event generators |
| Transform | `ToMat` | `RawBlock` → OpenCV `Mat` (rows = channels) | **Working** |
| Transform | `SampleToHostTime` | Drift-corrected `DateTimeOffset` per block | **Working** |
| Sink | `StartRecording` | Start every OE Record Node | **Working** |
| Sink | `StopRecording` | Stop recording; acquisition continues | **Working** |
| Sink | `SetTtl` | Latch a TTL line high/low | **Working** — needs a downstream output plugin |
| Sink | `PulseTtl` | Pulse a line, auto-clear after a width | **Working** — output plugin, or *Direct board trigger* |

## Seeing your data

`RawSamples` emits a `RawBlock`, which Bonsai will not plot directly. Convert it:

```
RawSamples  →  ToMat  →  (right-click ToMat → Visualizer)
```

`ToMat` yields an OpenCV `Mat` with **rows = channels, cols = samples**, which the
Bonsai.Dsp matrix/waveform visualizers render as live traces.

**Units:** samples are `int16` **ADC counts**, not microvolts. Multiply by the
channel's `bitVolts` (shown in the OE GUI) to convert.

**Buffer lifetime:** `RawBlock.Samples` is only valid inside the `OnNext` call.
Use `Clone()` or `Samples.ToArray()` to retain it. `ToMat` already copies.

## Health check

Add an `OpenEphysSession` node and watch:

- `IsConnected` — true
- `FrameCount` — rising steadily (≈940 /s at 30 kHz with 32-sample blocks)
- `DropCount` — zero

`FrameCount` stuck at 0 means the plugin is not publishing: confirm OE
acquisition is running and **Stream continuous** is enabled in the plugin editor.

> `DropCount` only counts frames whose `BIT_LOST_DATA` flag is set, and the plugin
> never sets it — so drops are currently **invisible from Bonsai**. Read the
> dropped-frame count off the OE editor's status line instead. In particular, a
> block over ~1023 channels (at a 32-sample block) exceeds one 64 KiB ring slot and
> every frame is dropped, while Bonsai just sees silence.

See `examples/` for ready-to-run workflows.
