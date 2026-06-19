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
  [string]$Arch = "x64"
)
# NB: do NOT set $ErrorActionPreference='Stop'. cmake/MSBuild write progress and
# deprecation warnings to stderr; under 'Stop' (especially with 2>&1) PowerShell
# turns those into terminating errors. We gate on $LASTEXITCODE after each native
# call and throw explicitly instead.
$root = $PSScriptRoot
$gui  = Join-Path $root "external/plugin-GUI"
$guiBuild = Join-Path $gui "Build"
$pluginSrc = Join-Path $root "oe-gui-plugin"
$pluginBuild = Join-Path $root "build-oe-plugin"
$dist = Join-Path $root "dist-oe-plugin"

if (-not (Test-Path (Join-Path $gui "CMakeLists.txt"))) {
  throw "plugin-GUI submodule not initialized. Run: git submodule update --init --recursive"
}

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
cmake -S $pluginSrc -B $pluginBuild -A $Arch "-DOPEN_EPHYS_LIB=$($oelib.FullName)"
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
Write-Host "Install: drop OEconnect.dll into the OE GUI 'plugins' folder, restart the GUI." -ForegroundColor Green
