<#
.SYNOPSIS
  Build the loadable OEconnect OE GUI plugin (Windows) in two stages.

.DESCRIPTION
  Stage 1: build the Open Ephys GUI (`open-ephys` target) from the submodule,
           which generates JuceHeader.h and produces open-ephys.lib.
  Stage 2: build OEconnect.dll against the GUI headers + open-ephys.lib.

  The resulting OEconnect.dll is copied next to this script under
  dist-oe-plugin/ and (if found) into the GUI build's plugins/ folder.

.EXAMPLE
  pwsh ./build-oe-plugin.ps1 -Config Release
#>
param(
  [string]$Config = "Release",
  [string]$Arch = "x64",
  # Point at a different plugin-GUI checkout to build for another API version.
  # Defaults to the submodule (GUI 1.0.x, plugin API v10).
  [string]$GuiDir = ""
)
# NB: do NOT set $ErrorActionPreference='Stop'. cmake/MSBuild write progress and
# deprecation warnings to stderr; under 'Stop' (especially with 2>&1) PowerShell
# turns those into terminating errors. We gate on $LASTEXITCODE after each native
# call and throw explicitly instead.
$root = $PSScriptRoot
$gui  = if ($GuiDir) { (Resolve-Path $GuiDir).Path } else { Join-Path $root "external/plugin-GUI" }
$guiBuild = Join-Path $gui "Build"

if (-not (Test-Path (Join-Path $gui "CMakeLists.txt"))) {
  throw "plugin-GUI not found at '$gui'. For the submodule run: git submodule update --init --recursive"
}

# A plugin binary is bound to one plugin API version: PluginManager rejects any
# library whose apiVersion != PLUGIN_API_VER. Key the build and dist directories
# on that version so several GUI lines can be built side by side.
$apiHeader = Join-Path $gui "Source/Processors/PluginManager/OpenEphysPlugin.h"
$apiVer = (Select-String -Path $apiHeader -Pattern '#define\s+PLUGIN_API_VER\s+(\d+)').Matches[0].Groups[1].Value
if (-not $apiVer) { throw "could not read PLUGIN_API_VER from $apiHeader" }
Write-Host "Open Ephys plugin API version: v$apiVer  (GUI: $gui)" -ForegroundColor Cyan

$pluginSrc   = Join-Path $root "oe-gui-plugin"
$pluginBuild = Join-Path $root "build-oe-plugin-api$apiVer"
$dist        = Join-Path $root "dist-oe-plugin/api-v$apiVer"

Write-Host "== Stage 1: build Open Ephys GUI (open-ephys target) ==" -ForegroundColor Cyan
cmake -S $gui -B $guiBuild -A $Arch
if ($LASTEXITCODE) { throw "GUI configure failed" }
cmake --build $guiBuild --config $Config --target open-ephys --parallel
if ($LASTEXITCODE) { throw "GUI build failed" }

$oelib = Get-ChildItem -Path $guiBuild -Recurse -Filter open-ephys.lib -ErrorAction SilentlyContinue |
         Select-Object -First 1
if (-not $oelib) { throw "open-ephys.lib not found under $guiBuild after the GUI build" }
Write-Host "open-ephys.lib: $($oelib.FullName)"

Write-Host "== Stage 2: build OEconnect plugin ==" -ForegroundColor Cyan
# GUI_BASE_DIR must point at the SAME checkout that produced open-ephys.lib --
# otherwise we compile against one version's headers and link another's library,
# which surfaces as a wall of unresolved externals with subtly wrong signatures.
cmake -S $pluginSrc -B $pluginBuild -A $Arch `
      "-DGUI_BASE_DIR=$gui" `
      "-DOPEN_EPHYS_LIB=$($oelib.FullName)"
if ($LASTEXITCODE) { throw "plugin configure failed" }
cmake --build $pluginBuild --config $Config --parallel
if ($LASTEXITCODE) { throw "plugin build failed" }

$dll = Get-ChildItem -Path $pluginBuild -Recurse -Filter OEconnect.dll -ErrorAction SilentlyContinue |
       Select-Object -First 1
if (-not $dll) { throw "OEconnect.dll not produced" }

New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item $dll.FullName $dist -Force
$guiPlugins = Join-Path (Split-Path $oelib.FullName) "plugins"
if (Test-Path $guiPlugins) { Copy-Item $dll.FullName $guiPlugins -Force }

Write-Host "OEconnect plugin built: $($dll.FullName)" -ForegroundColor Green
Write-Host "Copied to: $dist" -ForegroundColor Green
Write-Host "Install: drop OEconnect.dll into the plugins folder of a GUI whose plugin API is v$apiVer." -ForegroundColor Green
Write-Host "         (a GUI with a different API version will refuse to load it)" -ForegroundColor Green
