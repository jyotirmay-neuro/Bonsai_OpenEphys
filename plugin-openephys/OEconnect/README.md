# OEconnect — OpenEphys GUI plugin

A JUCE-based OE GUI **sink** processor (a `GenericProcessor` subclass) that
publishes acquisition data to Bonsai over shared memory or ZeroMQ and executes
commands coming back (start/stop recording, set/pulse TTL).

See [`docs/status.md`](../../docs/status.md) for what is functional. In short:
data publishing, recording control and clock sync work; **TTL output does not
reach hardware yet** (the board adapters are stubs), and `SPIKE` / `TTL_EVENT`
frames are not emitted.

## Build

Two stages: the GUI is built once to produce `open-ephys.lib` and generate
`JuceHeader.h`, then the plugin is built against it. `build-oe-plugin.ps1`
automates both.

```powershell
git submodule update --init --recursive
pwsh ./build-oe-plugin.ps1 -Config Release
```

Output: `dist-oe-plugin/OEconnect.dll` (also copied into the GUI build's
`plugins/` folder when present).

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

1. Copy `dist-oe-plugin/OEconnect.dll` into the OE GUI `plugins` directory:
   - Windows: `C:\ProgramData\Open Ephys\plugins\`
     (for a portable/dev build, the `plugins\` folder beside `open-ephys.exe`)
2. Restart the OE GUI. **OEconnect** appears under the *Sinks* category.
3. Drag it onto the signal chain downstream of an acquisition source:

   ```
   [Acquisition Source] → [OEconnect]
   ```

   To stream a filtered signal, insert a filter upstream — the plugin forwards
   whatever reaches it, it does not filter:

   ```
   [Acquisition Source] → [Bandpass Filter] → [OEconnect] → [Record Node]
   ```

> The plugin must match the ABI of the GUI it was built against. Load it into
> the GUI built from this repo's `external/plugin-GUI` submodule, not an
> arbitrary release.

## Settings

Every control is an OE `Parameter`: it shows its description as a tooltip, is
saved and restored with the signal chain, and (where changing it mid-run would
be unsafe) is locked while acquisition runs.

| Control | Meaning |
|---|---|
| **Transport** | `Auto` / `SharedMem` (sub-ms, same machine) / `Zmq` (1–5 ms, cross-machine) |
| **Stream raw** | Publish the broadband block each callback. On by default. |
| **Stream filtered** | Publish a second copy tagged `FILTERED_BLOCK`. Off by default; only meaningful with an upstream filter. |
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
