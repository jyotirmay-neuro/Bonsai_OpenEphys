# OEconnect — OpenEphys GUI plugin

JUCE-based OE GUI plugin (a `GenericProcessor` subclass) that publishes
acquisition data to Bonsai over shared memory or ZeroMQ and accepts commands
back (start/stop record, set TTL).

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Install the resulting `OEconnect.bundle` into your OE GUI plugin folder.

## Dependencies

- OE plugin-build template (submodule under `external/plugin-GUI/`).
- `liboeconnect` (sibling `libshared/oeconnect`, linked statically).
- `libzmq` + `cppzmq` (vendored via CMake FetchContent).

## Install (after `cmake --build`)

1. Locate `build/OEconnect.bundle` (Linux/macOS) or `build/Release/OEconnect.dll`
   (Windows).
2. Copy it into your OE GUI `plugins` directory:
   - Windows: `%APPDATA%\Open Ephys\plugins\`
   - macOS:   `~/Library/Application Support/Open Ephys/plugins/`
   - Linux:   `~/.config/Open Ephys/plugins/`
3. Restart OE GUI. `OEconnect` appears under the *Sinks* category.
4. Drag onto the signal chain *after* a Bandpass Filter (or any other
   downstream consumer) and *before* a Record Node:
   `[Acq Source] -> [Bandpass] -> [OEconnect] -> [Record Node]`.
