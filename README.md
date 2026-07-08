# OEconnect

Bidirectional, low-latency bridge between the Open Ephys GUI and Bonsai-rx.

- **Hard-RT tier:** lock-free shared-memory ring buffers on a single machine.
  Wait-free producer; the consumer wakes in well under a millisecond.
- **Soft-RT tier:** ZeroMQ over loopback or LAN for cross-machine deployments.
  1–5 ms typical.
- **Secure by default:** the ZMQ transport binds loopback only; any
  network-reachable bind requires CURVE authentication or refuses to start.

> **Read [`docs/status.md`](docs/status.md) before relying on any capability.**
> Continuous data streaming, recording control, clock sync and TTL output all work
> end to end. TTL reaches hardware via OE's event bus plus a downstream output
> plugin (board-agnostic). Spike and TTL-event *input* streams are not yet emitted
> by the plugin. Only GUI 1.0.x (plugin API v10) is currently built.

## Quick start

1. Build both halves — see [Build](#build).
2. Copy `OEconnect.dll` into the OE GUI `plugins` folder, restart the GUI, and
   drop the **OEconnect** sink into your signal chain, downstream of an
   acquisition source.
3. Install `Bonsai.OEconnect.nupkg` into Bonsai — see [`docs/install.md`](docs/install.md).
4. Start acquisition in OE. In Bonsai, build:

   ```
   RawSamples  →  ToMat  →  (right-click ToMat → Visualizer)
   ```

   Leave `Endpoint` empty and Bonsai auto-discovers the running session.

Option-by-option walkthrough, including which settings are compatible with which:
[`docs/configuration.md`](docs/configuration.md).

## How it works

Data always flows **OE → Bonsai**; commands always flow **Bonsai → OE**. Bonsai
never talks to acquisition-board firmware directly — every hardware action
crosses the plugin, so the OE recording stays the single source of truth. See
[`docs/architecture-rules.md`](docs/architecture-rules.md).

The wire format is frozen and authoritative in
[`spec/oec-protocol-v1.md`](spec/oec-protocol-v1.md). All three artifacts
(`liboeconnect`, the OE plugin, the Bonsai package) conform to it.

## Repository layout

```
spec/                              ← wire-protocol spec (authoritative)
libshared/oeconnect/               ← C ABI shared library (frames, rings, drift)
plugin-openephys/OEconnect/        ← C++/JUCE plugin for the OE GUI
package-bonsai/Bonsai.OEconnect/   ← .NET Bonsai package
examples/                          ← OE signal chains + Bonsai workflows
docs/                              ← status, configuration, design, perf
ci/                                ← CI helper scripts
```

## Build

Windows, Visual Studio 2022 build tools, CMake ≥ 3.20, .NET SDK 10.

**OE plugin** — two stages: build the GUI once, then the plugin against it.

```powershell
git submodule update --init --recursive
cd plugin-openephys/OEconnect
pwsh ./build-oe-plugin.ps1 -Config Release
# -> dist-oe-plugin/OEconnect.dll
```

**Bonsai package** — bundles the native `oeconnect.dll`, so build that first.

```powershell
cmake -S libshared/oeconnect -B libshared/oeconnect/build
cmake --build libshared/oeconnect/build --config Release
dotnet pack package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj `
        -c Release -o dist/nupkg
# -> dist/nupkg/Bonsai.OEconnect.<version>.nupkg
```

Per-subsystem detail:
[libshared](libshared/oeconnect/README.md) ·
[plugin](plugin-openephys/OEconnect/README.md) ·
[package](package-bonsai/Bonsai.OEconnect/README.md)

## Tests

```powershell
# C core (33 tests)
cmake -S libshared/oeconnect -B libshared/oeconnect/build -DOEC_BUILD_TESTS=ON
cmake --build libshared/oeconnect/build --config Release
ctest --test-dir libshared/oeconnect/build -C Release --output-on-failure

# Plugin (12 tests)
cmake -S plugin-openephys/OEconnect -B plugin-openephys/OEconnect/build -DOEC_PLUGIN_BUILD_TESTS=ON
cmake --build plugin-openephys/OEconnect/build --config Release
ctest --test-dir plugin-openephys/OEconnect/build -C Release --output-on-failure

# Bonsai package (10 tests, incl. an end-to-end shared-memory round trip)
dotnet test package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests -c Release
```

## Documentation

| Document | Purpose |
|---|---|
| [docs/status.md](docs/status.md) | **What actually works today.** Start here. |
| [docs/configuration.md](docs/configuration.md) | Every option, compatibility matrix, troubleshooting |
| [spec/oec-protocol-v1.md](spec/oec-protocol-v1.md) | Frozen wire protocol |
| [docs/architecture-rules.md](docs/architecture-rules.md) | Non-negotiable design constraints |
| [docs/compat-policy.md](docs/compat-policy.md) | Versioning and compatibility policy |
| [docs/perf/closed_loop_latency.md](docs/perf/closed_loop_latency.md) | Latency measurement procedure |
| [docs/install.md](docs/install.md) | Installing the built artifacts |
