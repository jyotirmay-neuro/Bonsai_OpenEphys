# `spec/` — wire-protocol contracts

Authoritative source for the OEconnect wire format. Three artifacts (libshared, OE plugin, Bonsai package) all conform to this spec. Any change here must:

1. Bump `version_major` (layout-breaking) or `version_minor` (additive) in §1 of `oec-protocol-v1.md`.
2. Update `OEC_PROTOCOL_VERSION_MAJOR` / `_MINOR` in `libshared/oeconnect/include/oeconnect/version.h`.
3. Update `OEconnect.Protocol.VersionMajor` / `.VersionMinor` in the Bonsai package.

CI (`.github/workflows/spec.yml`) enforces these.
