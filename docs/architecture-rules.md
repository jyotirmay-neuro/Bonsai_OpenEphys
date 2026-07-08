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

> **Current compliance: satisfied.** Every Bonsai-issued TTL is published on OE's
> event bus (`addTTLChannel` + `setTTLState`) before it can reach hardware, so a
> downstream Record Node captures each edge. Hardware output is produced by a
> downstream output plugin (Acq Board Output, Arduino Output, Pulse Pal) consuming
> that same event — there is deliberately no board-specific code in the bridge.
>
> The one *optional* shortcut, "Direct board trigger", broadcasts
> `ACQBOARD TRIGGER <line> <ms>` so the board fires a pulse itself. It is still
> compliant: the event is emitted **first and unconditionally**, so the recording
> stays complete. Any future direct-drive path must preserve that ordering.

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
