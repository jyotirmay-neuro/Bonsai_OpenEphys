# OEconnect — OpenEphys GUI plugin

A JUCE-based OE GUI **sink** processor (a `GenericProcessor` subclass) that
publishes acquisition data to Bonsai over shared memory or ZeroMQ and executes
commands coming back (start/stop recording, set/pulse TTL).

See [`docs/status.md`](../../docs/status.md) for what is functional. In short:
data publishing, spike/TTL event republishing, recording control, clock sync and
TTL output all work.

Builds for OE GUI **1.0.x (plugin API v10)** and **0.6.x (v8)** — one DLL per API
version, see [`docs/oe-version-compatibility.md`](../../docs/oe-version-compatibility.md).

## TTL output

The OE GUI exposes **no cross-board API** for a plugin to set a digital output
line (`setTTLOutputBit` does not exist anywhere). The supported route is a TTL
*event* plus a downstream **output plugin**:

```
[Acquisition Board] → [OEconnect] → [Acq Board Output]
                          │                  │
                    emits TTL event    event → physical line
```

OEconnect calls `addTTLChannel()` in `updateSettings()` and `setTTLState()` when a
command arrives, exactly as Crossing Detector and Ripple Detector do. Any board
with an output companion plugin works, with **no board-specific code**. The same
event is captured by downstream Record Nodes, which is what makes the OE recording
a complete copy of every edge the bridge issued.

Optionally, **Direct board trigger** broadcasts `ACQBOARD TRIGGER <line> <ms>` so
the acquisition board fires a pulse itself. Pulses only (the grammar cannot latch),
active-acquisition only, ignored by boards that don't implement
`handleBroadcastMessage()`. The event is always emitted first.

## Build

Two stages: the GUI is built once to produce `open-ephys.lib` and generate
`JuceHeader.h`, then the plugin is built against it. `build-oe-plugin.ps1`
automates both.

```powershell
git submodule update --init --recursive
pwsh ./build-oe-plugin.ps1 -Config Release
```

Output: `dist-oe-plugin/api-v10/OEconnect.dll` (also copied into the GUI build's
`plugins/` folder when present). The script reads `PLUGIN_API_VER` from whichever
GUI you point it at and keys the output directory on it.

To build for GUI 0.6.x instead, pass a 0.6 checkout:

```powershell
pwsh ./build-oe-plugin.ps1 -Config Release -GuiDir C:\src\plugin-GUI-0.6.7
# -> dist-oe-plugin/api-v8/OEconnect.dll
```

The first run compiles the whole Open Ephys GUI and can take tens of minutes.
Subsequent runs reuse the cached `open-ephys.lib` and only rebuild the plugin.

To build just the unit tests (no GUI toolchain needed — this excludes the JUCE
processor and editor):

```powershell
cmake -S . -B build -DOEC_PLUGIN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Dependencies

- Open Ephys `plugin-GUI` (submodule under `external/plugin-GUI/`).
- `liboeconnect` (sibling `libshared/oeconnect`, linked **statically**).
- `libzmq` + `cppzmq` (vendored via CMake `FetchContent`, linked statically).

The resulting `OEconnect.dll` is self-contained: no `liboeconnect.dll` or
`libzmq.dll` needs to sit beside it.

## Install

1. Copy `dist-oe-plugin/api-v<N>/OEconnect.dll` — matching your GUI's plugin API
   version — into the OE GUI `plugins` directory:
   - Windows: `C:\ProgramData\Open Ephys\plugins\`
     (for a portable/dev build, the `plugins\` folder beside `open-ephys.exe`)
2. Restart the OE GUI. **OEconnect** appears under the *Sinks* category.
3. Drag it onto the signal chain downstream of an acquisition source:

   ```
   [Acquisition Source] → [OEconnect]
   ```

   To stream a filtered signal, insert a filter upstream and set **Stream label**
   to `Filtered` — the plugin forwards whatever reaches it, it does not filter:

   ```
   [Acquisition Source] → [Bandpass Filter] → [OEconnect] → [Record Node]
   ```

   For raw **and** filtered simultaneously, branch the chain and run two OEconnect
   nodes (one labelled `Raw`, one `Filtered`). Each gets its own shared-memory
   region, scoped by OE node id, so give each Bonsai source an explicit `Endpoint`.

> The GUI hard-rejects a plugin whose API version differs from its own, and the
> DLL imports JUCE symbols from the host executable. Load the `api-vN` build that
> matches your GUI. See
> [`docs/oe-version-compatibility.md`](../../docs/oe-version-compatibility.md).

## Settings

Every control is an OE `Parameter`: it shows its description as a tooltip, is
saved and restored with the signal chain, and (where changing it mid-run would
be unsafe) is locked while acquisition runs.

| Control | Meaning |
|---|---|
| **Transport** | `Auto` / `SharedMem` (sub-ms, same machine) / `Zmq` (1–5 ms, cross-machine) |
| **Stream continuous** | Publish this node's incoming block each callback. On by default. |
| **Stream label** | `Raw` or `Filtered` — which stream id to stamp on that block. The node does **not** filter; the label must describe where you placed it. |
| **Stream spikes** | Republish OE's spike events. On by default. Needs a Spike Detector **upstream**. |
| **Stream TTL events** | Republish OE's TTL edges (board digital inputs, upstream detectors). On by default. |
| **Direct board trigger** | Also broadcast `ACQBOARD TRIGGER` so the board fires pulses itself. Off by default. Pulses only. |
| **ZMQ bind address** | `127.0.0.1` by default. A routable address requires CURVE auth. |
| **ZMQ data / command port** | `5557` / `5558`. Must differ. |
| *(status line)* | Active transport, detected board, cumulative dropped frames. |

Full detail and the compatibility matrix:
[`docs/configuration.md`](../../docs/configuration.md).

### Network security

Binding ZMQ to anything other than loopback exposes both the neural data stream
and a command channel that can start recordings and fire TTL lines. The plugin
**refuses a non-loopback bind** unless `OEC_ZMQ_CURVE_SECRET` holds a 40-character
Z85 CURVE secret key. There is no override.

## Sample units

OE continuous buffers hold **microvolts**. The plugin divides by each channel's
`bitVolts` to recover the raw `int16` ADC counts the recorder stores, and ships
those. Consumers multiply by `bitVolts` to get microvolts back.

### Observation-only mode

If the plugin loads against a board SDK older than the minimum supported, it
refuses to start acquisition and emits one `ERROR` frame so Bonsai sees why. To
keep the bridge usable during a firmware-upgrade window, run the OEconnect node
with no TTL-out or `StartRecording` sinks driving it from Bonsai: Bonsai then
receives blocks but issues no commands.
