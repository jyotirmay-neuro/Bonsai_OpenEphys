# OEconnect example workflows

Each `.bonsai` file in this directory opens in the Bonsai-rx editor (after
installing the `Bonsai.OEconnect` NuGet via the package manager).

| Workflow                                   | What it demonstrates                                     |
|--------------------------------------------|----------------------------------------------------------|
| `closed_loop_spike_triggered_stim.bonsai`  | Sub-millisecond loop: filter a unit, pulse TTL line 2.   |
| `record_with_ttl_marker.bonsai`            | Start OE recording from Bonsai; emit TTL markers every 5 s. |
| `lfp_band_visualization.bonsai`            | Convert OEconnect blocks to Bonsai.Dsp `Mat`, write to disk. |
| `multi_subscriber_data_split.bonsai`       | Multiple operators share one OE session (auto-discovery). |

## OE signal chain

For workflows that need OE-side configuration, the matching
`*.openephys.xml` file is loaded in the OE GUI via *File → Load Signal Chain*.

The recommended chain places the OEconnect processor as a passthrough:

```
[Acq Source] → [Bandpass] → [OEconnect] → [Record Node]
```

The Record Node downstream of OEconnect is the **independent backup
recording**: even if Bonsai or the bridge crashes mid-experiment, the OE
recording remains a complete copy.
