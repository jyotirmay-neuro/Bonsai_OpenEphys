# OEconnect

Bidirectional, low-latency bridge between the Open Ephys GUI and Bonsai-rx.

- **Hard-RT tier:** Lock-free shared-memory ringbuffers on a single Windows box. Closed-loop p99.9 < 1 ms for ≤ 256 ch @ 30 kHz.
- **Soft-RT tier:** ZeroMQ over loopback or LAN for cross-OS and cross-machine deployments.
- **Backup-safe:** OE keeps its own Record Node files in parallel; Bonsai-issued TTL events land on OE's event bus.

See [`docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md`](docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md) for the full design and wire protocol.

## Repository layout

```
spec/                              ← wire-protocol spec (authoritative)
libshared/oeconnect/               ← C ABI shared library
plugin-openephys/OEconnect/        ← C++/JUCE plugin for OE GUI
package-bonsai/Bonsai.OEconnect/   ← .NET Bonsai package
examples/                          ← OE signal chains + Bonsai workflows
docs/                              ← design, plan, perf procedures
ci/                                ← GitHub Actions workflows
```

## Build

See per-subsystem READMEs:
- [libshared/oeconnect/README.md](libshared/oeconnect/README.md)
- [plugin-openephys/OEconnect/README.md](plugin-openephys/OEconnect/README.md)
- [package-bonsai/Bonsai.OEconnect/README.md](package-bonsai/Bonsai.OEconnect/README.md)
