# OEconnect compatibility policy

This document is the authoritative reference for every versioned surface in
the OEconnect bridge: what it is, who owns it, how it is versioned, and which
lever keeps old peers working when it changes.

## Versioned surfaces

| Surface | Owner file | Versioning scheme | Backward-compat lever |
|---|---|---|---|
| Wire protocol | `spec/oec-protocol-v1.md`, `libshared/oeconnect/include/oeconnect/frame.h` | `OEC_PROTOCOL_VERSION_{MAJOR,MINOR}` in `version.h` | HELLO handshake (§11) negotiates the highest mutually-supported version; v1.0 peers without HELLO still parse via discard arm |
| C ABI | `libshared/oeconnect/include/oeconnect/*.h` | `OEC_LIB_VERSION_{MAJOR,MINOR,PATCH}` in `version.h` | additive trailing struct fields guarded by `reserved[]`; new exported functions only on MINOR |
| Bonsai public API | `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/PublicAPI.{Shipped,Unshipped}.txt` | NuGet semver (`PackageId` `Bonsai.OEconnect`) | `[Obsolete]` shims kept for one minor; removed only on major |
| OE plugin-GUI dep | `plugin-openephys/OEconnect/external/plugin-GUI` (submodule) | pinned submodule SHA; min supported GUI tag | nightly canary against tip-of-main (`external-compat.yml`) |
| Bonsai.Core dep | `Bonsai.OEconnect.csproj` `<PackageReference>` | NuGet floor `2.8.x`+ | nightly canary floats `2.*` |
| Board firmware / SDKs | `plugin-openephys/OEconnect/Source/Boards/*Adapter.h` | per-adapter `kMin*` constant | runtime minimum-support gate refuses to start + emits `OEC_STREAM_ERROR` |

## Semver rules

### Wire protocol
- **MAJOR** — different frame layout or stream-id semantics. Requires the
  HELLO major to bump in lock-step; non-negotiating peers are incompatible.
- **MINOR** — additive only (new stream id, a previously-reserved field now
  consumed, a new ACK status). Old peers MUST still parse the stream via the
  discard arm.

### C ABI
- **MAJOR** — struct layout change or exported-function signature change.
- **MINOR** — a new exported function, or a new struct trailing field carved
  out of an existing `reserved[]` block (no offset shift for old fields).
- **PATCH** — implementation-only fixes; no surface change.

### Bonsai package
- Standard NuGet semver.
- **MAJOR** — allowed to remove `[Obsolete]` types.
- **MINOR** — additive only.

## Minimum supported versions

Updated every release. Current floor:

| Dependency | Minimum | Notes |
|---|---|---|
| OE plugin-GUI | `v0.5.x`+ | tested SHAs tracked in the submodule pin |
| Bonsai.Core | `2.8.x`+ | also Bonsai.Dsp `2.8.x`+ |
| native liboeconnect protocol | `v1.0`+ | v1.1 adds the HELLO handshake additively |

## Deprecation walkthrough

1. Mark the old member `[Obsolete("Use X. Removed in v2.0.")]`.
2. Append its canonical declaration to `PublicAPI.Unshipped.txt` (.NET only).
3. Ship one minor release carrying both the old and the new member.
4. Remove on the next major; move the line out of `PublicAPI.Shipped.txt` in
   the same PR that does the removal.
