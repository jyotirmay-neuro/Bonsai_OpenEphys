# Installing OEconnect

Two artifacts, installed independently:

| Artifact | Goes into |
|---|---|
| OE GUI plugin (`OEconnect.dll` / `.so` / `.dylib`) | The Open Ephys GUI `plugins` folder |
| `Bonsai.OEconnect.<version>.nupkg` | Bonsai, via a local package source |

## Download prebuilt binaries

Grab the latest release — no build toolchain required. One download per OS; each
zip carries **both** supported GUI variants. These links always point at the
newest release:

| Platform | OE GUI plugin (all variants) |
|---|---|
| **Windows** x64 | [OEconnect-plugin-windows.zip](https://github.com/jyotirmay-neuro/Bonsai_OpenEphys/releases/latest/download/OEconnect-plugin-windows.zip) |
| **Linux** x64 | [OEconnect-plugin-linux.zip](https://github.com/jyotirmay-neuro/Bonsai_OpenEphys/releases/latest/download/OEconnect-plugin-linux.zip) |
| **macOS** | [OEconnect-plugin-macos.zip](https://github.com/jyotirmay-neuro/Bonsai_OpenEphys/releases/latest/download/OEconnect-plugin-macos.zip) |

**Bonsai package (all OS):**
[Bonsai.OEconnect.0.0.1.nupkg](https://github.com/jyotirmay-neuro/Bonsai_OpenEphys/releases/latest/download/Bonsai.OEconnect.0.0.1.nupkg)
· or browse the [latest release](https://github.com/jyotirmay-neuro/Bonsai_OpenEphys/releases/latest)

### Which variant do I install?

A plugin binary loads into exactly one GUI API version — the GUI's
`PluginManager` rejects any mismatch (`Invalid Plugin API version` in the
console, and the plugin silently does not appear). Each zip lays both variants
out side by side; pick the folder that matches the GUI you run:

| Your Open Ephys GUI | Plugin API | Use the file in the zip |
|---|---|---|
| **1.0.x** (current) | v10 | `api-v10/OEconnect.<ext>` |
| **0.6.x** | v8 | `api-v8/OEconnect.<ext>` |
| 0.5.x / 0.4.x and older | — | not supported — see [oe-version-compatibility.md](oe-version-compatibility.md) |

`<ext>` is `dll` on Windows, `so` on Linux, `dylib` on macOS. Not sure of your
GUI version? It is in the GUI's title bar and under **Help ▸ About**.

The `.nupkg` bundles the native `oeconnect` library for all three OS and is the
same regardless of GUI version. Full version rationale:
[oe-version-compatibility.md](oe-version-compatibility.md).

---

## 1. Open Ephys GUI plugin

`OEconnect.dll` is self-contained — `liboeconnect` and `libzmq` are statically
linked, so nothing needs to sit beside it. It exports `getLibInfo` /
`getPluginInfo` (OE plugin API v10) and registers a **sink** processor.

```powershell
Copy-Item "plugin-openephys\OEconnect\dist-oe-plugin\OEconnect.dll" `
          "C:\ProgramData\Open Ephys\plugins\" -Force
```

For a portable or from-source GUI, use the `plugins\` folder beside
`open-ephys.exe` instead.

Restart the GUI. **OEconnect** appears under the *Sinks* category. Drop it
downstream of an acquisition source:

```
[Acquisition Source] → [OEconnect]
```

> The DLL must match the ABI of the GUI it was compiled against (this repo's
> `external/plugin-GUI` submodule, plugin API v10). A different GUI binary may
> refuse to load it.

---

## 2. Bonsai package

The `.nupkg` contains the managed operators (`net472` + `net6.0`) and the native
`oeconnect.dll` under `runtimes/win-x64/native/`.

**Via a local package source (recommended):**

1. Bonsai → **Tools ▸ Manage Packages ▸ Settings** (gear icon).
2. Add a package source pointing at your output folder, e.g.
   `D:\Github_clone\Bonsai_OpenEphys\dist\nupkg`.
3. Switch the source dropdown to it and install **Bonsai.OEconnect**.
4. Restart Bonsai. The operators appear under the **OEconnect** toolbox group.

**Drop-in alternative:** a `.nupkg` is a zip. Extract
`lib\net472\Bonsai.OEconnect.dll` and `runtimes\win-x64\native\oeconnect.dll`
into your Bonsai install folder alongside the other `Bonsai.*.dll` files. Keep
the native DLL beside the managed one so the P/Invoke resolves.

### If no operators show up

Bonsai decides whether a package is a "Bonsai package" by matching the
`Bonsai` tag **case-sensitively**. Without it the package installs and the
assembly resolves, but the toolbox never scans it.

Confirm `%LOCALAPPDATA%\Bonsai\Bonsai.config` contains, inside
`<AssemblyReferences>`:

```xml
<AssemblyReference assemblyName="Bonsai.OEconnect" />
```

If it is missing, either reinstall a package whose `PackageTags` include
`Bonsai` (capital B), or add that line by hand — **with Bonsai closed**, or it
will overwrite the file on exit.

---

## 3. Verify end to end

1. Start acquisition in the OE GUI with the OEconnect sink in the chain.
2. In Bonsai, build:

   ```
   RawSamples  →  ToMat  →  (right-click ToMat → Visualizer)
   ```

   Leave `Endpoint` empty — Bonsai auto-discovers the running session via the
   sidecar in `%TEMP%\oeconnect\sessions\` and checks its heartbeat.

3. Add an `OpenEphysSession` node: `IsConnected` true, `FrameCount` rising
   (≈940 /s at 30 kHz), `DropCount` zero.

Troubleshooting lives in [configuration.md § 8](configuration.md#8-troubleshooting).

---

## 4. Cross-machine (ZMQ)

Shared memory is host-local. To run Bonsai on a different machine, set
**Transport** to `Zmq` in the plugin editor and give a routable **ZMQ bind
address**.

A non-loopback bind is **refused** unless the OE machine has a CURVE key set
before the GUI launches:

```powershell
$env:OEC_ZMQ_CURVE_SECRET = "<40-char Z85 secret key>"
```

Then point Bonsai's `Endpoint` at both sockets:

```
tcp://<oe-host>:5557|tcp://<oe-host>:5558
```

Note the `|` — the data and command endpoints are both required. Expect 1–5 ms
latency on this path, not the sub-millisecond of shared memory.
