# Bonsai.OEconnect

NuGet package providing Bonsai-rx operators for OpenEphys streaming and
control.

## Build

```powershell
dotnet build Bonsai.OEconnect.sln -c Release
dotnet test Bonsai.OEconnect.sln -c Release
dotnet pack src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release -o nupkg/
```

## Operators

| Category   | Operator             | Purpose                                                |
|------------|----------------------|--------------------------------------------------------|
| Source     | `OpenEphysSession`   | Liveness / status                                       |
| Source     | `RawSamples`         | Continuous broadband                                    |
| Source     | `FilteredSamples`    | Filtered continuous                                     |
| Source     | `Spikes`             | Spike events                                            |
| Source     | `TtlEvents`          | TTL edges                                               |
| Source     | `SyncPoints`         | Raw (sample, qpc) pairs                                 |
| Transform  | `ToMat`              | `RawBlock` → Bonsai.Dsp `Mat`                          |
| Transform  | `SampleToHostTime`   | Adds drift-corrected `DateTimeOffset`                   |
| Sink       | `StartRecording`     | Drive OE Record Node                                    |
| Sink       | `StopRecording`      | —                                                       |
| Sink       | `SetTtl`             | Assert a TTL output line                                |
| Sink       | `PulseTtl`           | Pulse a TTL output line                                 |

See `examples/` for ready-to-run workflows.
