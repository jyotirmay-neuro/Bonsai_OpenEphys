# Open Ephys version compatibility

## One binary per plugin API version. This is not negotiable.

The GUI's `PluginManager` hard-rejects any plugin whose API version differs from
its own:

```cpp
// plugin-GUI/Source/Processors/PluginManager/PluginManager.cpp
if (libInfo.apiVersion != PLUGIN_API_VER)
{
    ERROR_MSG ("Invalid Plugin API version");
    closeHandle (handle);
    return -1;
}
```

On Windows the plugin also links `open-ephys.lib` and imports JUCE symbols from
the host executable, so even a matching API number still requires an ABI-matched
GUI build.

**There is no such thing as one `OEconnect.dll` that loads into every GUI since
2020.** Distribution is a build matrix. `build-oe-plugin.ps1` reads
`PLUGIN_API_VER` out of the GUI you point it at and writes to
`dist-oe-plugin/api-v<N>/OEconnect.dll` so several can coexist.

CI does exactly this for both supported lines: every release build produces
`api-v10/` (from the submodule) **and** `api-v8/` (from a fresh plugin-GUI
v0.6.7 checkout), and the one per-OS release zip ships both folders. Users pick
the matching folder — see the variant table in
[install.md](install.md#which-variant-do-i-install).

## Supported targets

| GUI line | Plugin API | JUCE | Status |
|---|---|---|---|
| 1.0.x | v10 | 8 | **Built, tested, shipped.** The submodule default; `api-v10/` in every release zip. |
| 0.6.x | v8 | 6 | **Built and shipped.** CI compiles OEconnect against a real plugin-GUI v0.6.7 checkout through `Source/Compat/OECompat.h`; `api-v8/` in every release zip. |
| 0.5.x | older | — | **Not supported.** |
| 0.4.x | older | — | **Not supported.** |

### Why 0.5.x / 0.4.x are out

0.6.0 was a breaking rewrite that introduced the `DataStream` class — the notion
that a source can emit several asynchronous streams, each with a guaranteed
per-block sample count — and overhauled `Parameter` so plugins could declare
their UI. OEconnect is built on both. Versions before 0.6 have neither: they use
`process(AudioSampleBuffer&, MidiBuffer&)`, a different event-channel model, and
no parameter system at all. Supporting them is not a shim, it is a second
implementation of the processor, for a GUI line superseded in 2022.

If you genuinely need 0.4.x/0.5.x, say so — but expect a separate source tree
rather than an `#if`.

## Building for a specific GUI

```powershell
# Default: the submodule (GUI 1.0.x, API v10)
pwsh ./build-oe-plugin.ps1 -Config Release
# -> dist-oe-plugin/api-v10/OEconnect.dll

# A 0.6.x checkout elsewhere
pwsh ./build-oe-plugin.ps1 -Config Release -GuiDir C:\src\plugin-GUI-0.6.7
# -> dist-oe-plugin/api-v8/OEconnect.dll
```

Install the DLL whose `api-vN` matches the GUI you run. A mismatch produces
`Invalid Plugin API version` in the GUI's console and the plugin simply does not
appear.

## What the shim absorbs

`Source/Compat/OECompat.h` selects on `PLUGIN_API_VER`. Only genuine divergences
live there:

| Concern | v8 (0.6.x) | v10 (1.0.x) |
|---|---|---|
| Where parameters are declared | constructor | `registerParameters()` override |
| `addBooleanParameter` etc. | `(scope, name, description, …)` | `(scope, name, **displayName**, description, …)` |
| Categorical choices | `StringArray` | `Array<String>` |
| Processor-wide scope enum | `Parameter::GLOBAL_SCOPE` | `Parameter::PROCESSOR_SCOPE` |
| Boolean parameter editor | `addCheckBoxParameterEditor(name,x,y)` | `addToggleParameterEditor(scope,name,x,y)` |
| Combo / text parameter editors | no scope argument | scope argument |
| Label font | `Font(12.0f)` | `FontOptions(12.0f)` |
| Alert icon | `AlertWindow::WarningIcon` | `MessageBoxIconType::WarningIcon` |

Deliberately **not** in the shim, because they are identical in both:
`addTTLChannel`, `setTTLState`, `flipTTLState`, `broadcastMessage`,
`getContinuousChannel(int)`, `getSampleRate(int)`, `getNumDataStreams`,
`getParameter`, the `editor` member and `getEditor()`, and
`CoreServices::RecordNode::setRecordingDirectory` / `setRecordingStatus` /
`setAcquisitionStatus`.

That the TTL and recording APIs are stable across both lines is why the
event-bus TTL design ports unchanged.

## Board compatibility

Orthogonal to GUI version, and much simpler: **OEconnect contains no
board-specific code.**

TTL output is published as a TTL *event* on OE's event bus. A downstream output
plugin turns it into a physical line:

```
[any acquisition board] → [OEconnect] → [Acq Board Output | Arduino Output | Pulse Pal | …]
```

So OEconnect works with every board the GUI supports, including boards released
after this code was written, provided the board has an output companion plugin.
There is nothing to update per board. See
[configuration.md](configuration.md#getting-ttl-to-actual-hardware).

The one board-aware feature is the optional **Direct board trigger**, which
broadcasts the Open Ephys acquisition board's documented remote-control command
`ACQBOARD TRIGGER <line> <ms>`. Boards that do not implement
`handleBroadcastMessage()` ignore it, and the TTL event is emitted first
regardless, so enabling it is never harmful.
