# `spec/` — wire-protocol contracts

[`oec-protocol-v1.md`](oec-protocol-v1.md) is the authoritative source for the
OEconnect wire format. Three artifacts conform to it:

| Artifact | Role |
|---|---|
| `libshared/oeconnect` | Reference implementation of the frame codec, rings and drift fit |
| `plugin-openephys/OEconnect` | Producer (data) / consumer (commands) |
| `package-bonsai/Bonsai.OEconnect` | Consumer (data) / producer (commands) |

The spec defines the frame header, stream ids, command and ack codes, the
shared-memory region layout, the ZMQ socket conventions, and the HELLO handshake.

## Changing the protocol

Any change to `oec_frame_header`, stream ids, command codes, or the shared-memory
region header must:

1. Bump `version_major` (layout-breaking) or `version_minor` (additive) in §1 of
   [`oec-protocol-v1.md`](oec-protocol-v1.md).
2. Update `OEC_PROTOCOL_VERSION_MAJOR` / `_MINOR` in
   [`libshared/oeconnect/include/oeconnect/version.h`](../libshared/oeconnect/include/oeconnect/version.h).
3. Update the mirrored layouts in the Bonsai package's interop layer —
   `OecFrameHeader`, `OecBlockSubheader`, `OecHelloBody`, `OecStreams`, `OecCmds`
   in
   [`Interop/NativeStructs.cs`](../package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Interop/NativeStructs.cs).

CI enforces this: `.github/workflows/spec.yml` rejects frame-layout diffs without
a version bump, and `spec-drift.yml` checks the three implementations stay in
step. Golden frame fixtures live under [`tests/golden/`](../tests/golden/).

## Version semantics

- **`version_major` mismatch** → the consumer drops the frame and raises
  `ERROR(PROTOCOL_VERSION_MISMATCH)`.
- **`version_minor` mismatch** → forward-compatible accept; unknown stream ids
  and flags are treated as opaque and routed to the default discard arm.

Current wire spec: **1.1** (adds the HELLO handshake, §8).

## Spec vs implementation

The spec states the intended contract, not necessarily what is wired up today.
Notably §4.7 presents the ring geometry as editor-configurable while the
implementation fixes it at compile time, and two stream ids the spec defines
(`SPIKE`, `TTL_EVENT`) are parsed by consumers but never emitted by the current
plugin.

[`docs/status.md`](../docs/status.md) records what is actually implemented. When
the two disagree, the spec states the intent and `status.md` states reality.
