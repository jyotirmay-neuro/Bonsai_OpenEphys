# Architectural rules

These are non-negotiable. Any PR that violates them is rejected at review.

## 1. Bonsai never talks directly to the acquisition board

All hardware I/O — every TTL line set, every digital input read, every
acquisition state change — must traverse the OEconnect OE plugin. The
acquisition board's firmware is reached only via the OE Source Node API. A
"fast direct path" that lets Bonsai drive the FPGA without OE in the loop is
**never** an acceptable optimisation, regardless of latency wins.

**Rationale:** OE's recording remains a complete, independent sanity-check
copy of every closed-loop output the bridge ever issued. Bypassing OE
splits the event-bus record and silently invalidates the backup.

> **Current compliance: partial.** The routing rule holds — every command does
> cross the plugin, and no direct path exists. But the plugin's `on_ttl_emit`
> hook is still a stub: it never calls `addEvent()`, so Bonsai-issued TTLs are
> *not* written to OE's event bus, and the "complete copy" guarantee above does
> not hold in practice yet. The board adapters are likewise no-ops, so no TTL
> reaches hardware at all. Both gaps are tracked in [status.md](status.md) and
> must be closed before this rule's rationale is actually satisfied.

## 2. The audio thread is the only writer of shmem rings

Three shmem rings (`data_ring`, `cmd_ring`, `ack_ring`) all have the OE
audio thread as their sole producer. Helper threads (sync timer, slow-cmd
worker) feed `AckOutbox` (lock-free MPSC) which the audio thread drains.

**Rationale:** Preserves SPSC invariants on the rings → trivially correct
lock-free code. Any multi-writer scheme demands MPSC algorithms with worse
cache behaviour and harder verification.

## 3. Frame-layout changes require a spec version bump

Any change to `oec_frame_header`, stream IDs, command codes, or the
shmem region header requires bumping `version_major` (breaking) or
`version_minor` (additive) in `spec/oec-protocol-v1.md` §1. CI enforces.

**Rationale:** Three independent artifacts (libshared, plugin, package)
must agree on the wire. The spec doc is the single source of truth.
