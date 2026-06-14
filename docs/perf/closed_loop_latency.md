# Closed-loop latency verification (manual)

This is the headline-number procedure for OEconnect. It is **manual** —
automated CI cannot drive real hardware.

## Equipment

- Open Ephys Acquisition Board (Rhythm FPGA) running firmware ≥ 3.0.
- Oscilloscope with ≥ 100 MS/s and a histogram math function (e.g.
  Tektronix MDO3000 series, Picoscope 5000, equivalent).
- Function generator capable of TTL output @ 100 Hz with < 10 µs jitter.
- One BNC patch from function generator → OE board TTL-in line 0.
- One BNC patch from OE board TTL-out line 2 → scope channel 2.
- One BNC patch from function generator trigger out → scope channel 1.

## Software setup

1. Build all three subsystems in Release.
2. Install `OEconnect.bundle` into the OE GUI plugin folder.
3. Build the Bonsai workflow:
   ```
   TtlEvents → Where(it.Line == 0 && it.Edge == 1) → PulseTtl(Line = 2, WidthMicroseconds = 500)
   ```
4. OE signal chain:
   ```
   [Rhythm FPGA] → [OEconnect (transport = SharedMem)] → [Record Node]
   ```
5. Set OE block size to 32 samples (matches the protocol default; smaller
   blocks shave latency at the cost of CPU).
6. Confirm OEconnect editor shows: *Mode: SharedMem, Board: Rhythm FPGA, drops: 0*.

## Run

1. Start OE acquisition. Start recording.
2. Start the Bonsai workflow.
3. Set function generator: TTL pulse, 100 Hz, 50 % duty, 3.3 V.
4. Configure scope:
   - Channel 1 (trigger): rising edge.
   - Channel 2 (output): measure time-to-rising-edge from trigger.
   - Math channel: histogram of Δt over 60 000 acquisitions (10 minutes @ 100 Hz).
   - Time/div: 200 µs.
5. Acquire for the full 10 minutes.

## Pass criteria

- **SharedMem mode**: p99.9 of in→out latency < 1 ms. Median < 200 µs.
- **ZMQ-loopback mode** (re-run with editor *Transport: Zmq*,
  *Bind: 127.0.0.1*): p99 < 5 ms. Median < 1 ms.

Record histogram screenshot, raw CSV, and the OE recording session ID in
the [release notes](../release-notes/) for the version under test.

## What to do if it fails

- p99.9 just over 1 ms (≈ 1.1–1.5 ms): try `block_size = 16` and rerun.
- Heavy long-tail spikes (occasional > 5 ms): check Windows Power Plan is
  *High performance*; disable USB selective suspend; pin OE GUI process to
  one CPU package; disable Hyper-V virtualisation if not needed.
- Steady offset above target: profile with ETW; look at `process()`
  callback duration in OE.
- Anything anomalous: capture `oec_plugin_synth` traces and attach to a new
  GitHub issue under the `perf` label.
