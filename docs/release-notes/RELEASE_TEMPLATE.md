## OEconnect ${{ github.ref_name }}

### What's in this release
- **OE plugin** — `OEconnect-{linux,windows,macos}.zip` containing the
  per-OS plugin bundle. Drop into the OE GUI `plugins/` folder.
- **Bonsai package** — `Bonsai.OEconnect.<version>.nupkg`. Install via the
  Bonsai package manager.

### Verification before installing in a live rig
- Run the manual latency procedure in
  [`docs/perf/closed_loop_latency.md`](../docs/perf/closed_loop_latency.md).
- Confirm OE Record Node downstream of OEconnect captures the expected
  backup files.

### Protocol
- Wire protocol version: see [`spec/oec-protocol-v1.md`](../spec/oec-protocol-v1.md).
- Compatible producer/consumer protocol versions are negotiated via the
  HELLO handshake (§11). v1.0 peers without HELLO are still supported.

### Supported upstream versions
- See [`docs/compat-policy.md`](../docs/compat-policy.md) for the
  authoritative table.

(Automatic changelog from commits below.)
