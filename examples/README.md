# OEconnect example workflows

Each `.bonsai` file opens in the Bonsai-rx editor once `Bonsai.OEconnect` is
installed (see [`docs/install.md`](../docs/install.md)).

| Workflow | Demonstrates | Runs today? |
|---|---|---|
| `lfp_band_visualization.bonsai` | `RawSamples → ToMat`, visualize and write to disk | **Yes** |
| `multi_subscriber_data_split.bonsai` | Several operators sharing one auto-discovered session | **Yes** |
| `record_with_ttl_marker.bonsai` | Start OE recording from Bonsai; emit TTL markers every 5 s | **Yes** — markers land on OE's event bus; add an output plugin for a physical line |
| `closed_loop_spike_triggered_stim.bonsai` | Threshold a unit, pulse TTL line 2 | **Yes** — put a Spike Detector upstream of OEconnect in the OE chain, and an output plugin downstream |

> Remaining caveats are tracked in [`docs/status.md`](../docs/status.md). Two
> things to get right in the OE signal chain: reaching a *physical* TTL line needs
> an output plugin (Acq Board Output / Arduino Output / Pulse Pal) **downstream**
> of OEconnect, and the `Spikes` node needs a Spike Detector **upstream** of it —
> OEconnect forwards OE's event bus, it does not detect spikes itself.

## Starting point

If you just want to confirm the bridge is live, the smallest useful workflow is:

```
RawSamples  →  ToMat  →  (right-click ToMat → Visualizer)
```

Leave `Endpoint` empty; Bonsai auto-discovers the running OE session. Add an
`OpenEphysSession` node to watch `FrameCount` climb and `DropCount` stay at zero.

## OE signal chain

Workflows needing OE-side setup ship a matching `*.openephys.xml`, loaded via
*File → Load Signal Chain* in the OE GUI.

The recommended chain places OEconnect after any filtering you want mirrored to
Bonsai, and keeps a Record Node downstream:

```
[Acq Source] → [Bandpass] → [OEconnect] → [Record Node]
```

OEconnect is a sink and does not modify the samples passing through it. The
Record Node is the **independent backup recording**: if Bonsai or the bridge
crashes mid-experiment, the OE recording remains a complete copy.

> Note: OEconnect does **not** filter. Its "Stream filtered" option republishes
> whatever the upstream chain already produced, which is why the Bandpass sits
> *before* it.
