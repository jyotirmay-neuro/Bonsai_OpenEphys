# libshared/oeconnect/runtime/build.ps1
# Packs the standalone native runtime nupkg after a Release build of
# liboeconnect (Windows: build/Release/oeconnect.dll; POSIX libs at build root).
param([string]$Version = "0.0.1")

Push-Location $PSScriptRoot
try {
    & nuget pack liboeconnect.runtime.nuspec -Version $Version -OutputDirectory .
} finally {
    Pop-Location
}
