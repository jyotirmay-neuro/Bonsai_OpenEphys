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
