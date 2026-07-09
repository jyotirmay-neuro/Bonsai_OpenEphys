#!/usr/bin/env bash
# Build the loadable OEconnect OE GUI plugin on Linux / macOS, in two stages.
# POSIX counterpart of build-oe-plugin.ps1; keep the two in step.
#
#   Stage 1: configure + build the Open Ephys GUI `open-ephys` target from the
#            submodule, which generates JuceHeader.h and the GUI headers.
#   Stage 2: build the OEconnect shared library against those headers.
#
# Unlike Windows, the POSIX plugin does not link open-ephys at build time: it
# resolves JUCE symbols from the host process at load (see the LINUX/APPLE
# branches in oe-gui-plugin/CMakeLists.txt). Stage 1 is still needed for the
# generated headers.
#
# Usage:
#   ./build-oe-plugin.sh [--config Release] [--gui-dir /path/to/plugin-GUI]
#
# Point --gui-dir at another plugin-GUI checkout to build for a different plugin
# API version; output is keyed on that version so several can coexist.
set -euo pipefail

CONFIG="Release"
GUI_DIR=""
while [ $# -gt 0 ]; do
  case "$1" in
    --config)  CONFIG="$2"; shift 2 ;;
    --gui-dir) GUI_DIR="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI="${GUI_DIR:-$ROOT/external/plugin-GUI}"
GUI="$(cd "$GUI" && pwd)"
GUI_BUILD="$GUI/Build"

[ -f "$GUI/CMakeLists.txt" ] || {
  echo "plugin-GUI not found at '$GUI'. Run: git submodule update --init --recursive" >&2
  exit 1
}

# A plugin binary is bound to one plugin API version: PluginManager rejects any
# library whose apiVersion != PLUGIN_API_VER. Key the output on that version.
API_HEADER="$GUI/Source/Processors/PluginManager/OpenEphysPlugin.h"
API_VER="$(grep -Eo '#define[[:space:]]+PLUGIN_API_VER[[:space:]]+[0-9]+' "$API_HEADER" | grep -Eo '[0-9]+$')"
[ -n "$API_VER" ] || { echo "could not read PLUGIN_API_VER from $API_HEADER" >&2; exit 1; }
echo "Open Ephys plugin API version: v$API_VER  (GUI: $GUI)"

PLUGIN_SRC="$ROOT/oe-gui-plugin"
PLUGIN_BUILD="$ROOT/build-oe-plugin-api$API_VER"
DIST="$ROOT/dist-oe-plugin/api-v$API_VER"

echo "== Stage 1: configure Open Ephys GUI (generate headers) =="
cmake -S "$GUI" -B "$GUI_BUILD" -DCMAKE_BUILD_TYPE="$CONFIG"

# The POSIX plugin resolves JUCE symbols from the host at load, so it needs only
# the generated headers, not the built GUI. If configure already produced
# JuceHeader.h we can skip the (30+ min) full GUI build; otherwise build the
# open-ephys target to force header generation.
JUCE_HEADER="$GUI/JuceLibraryCode/JuceHeader.h"
if [ -f "$JUCE_HEADER" ]; then
  echo "JuceHeader.h present after configure; skipping full GUI build."
else
  echo "JuceHeader.h not generated at configure; building open-ephys target."
  cmake --build "$GUI_BUILD" --config "$CONFIG" --target open-ephys --parallel
fi

# open-ephys.lib is MSVC-only; on POSIX the equivalent static archive is optional
# for linking, but locate it if present so the plugin configure can use it.
OELIB="$(find "$GUI_BUILD" \( -name 'libopen-ephys.a' -o -name 'open-ephys.lib' \) -print 2>/dev/null | head -n1 || true)"

echo "== Stage 2: build OEconnect plugin =="
# GUI_BASE_DIR must match the checkout that generated the headers, else we compile
# against one version and (on MSVC) link another.
CMAKE_ARGS=( -S "$PLUGIN_SRC" -B "$PLUGIN_BUILD" -DCMAKE_BUILD_TYPE="$CONFIG" "-DGUI_BASE_DIR=$GUI" )
[ -n "$OELIB" ] && CMAKE_ARGS+=( "-DOPEN_EPHYS_LIB=$OELIB" )
cmake "${CMAKE_ARGS[@]}"
cmake --build "$PLUGIN_BUILD" --config "$CONFIG" --parallel

# The shared library has PREFIX "" and OUTPUT_NAME OEconnect -> OEconnect.so /
# OEconnect.dylib (or a .bundle if the toolchain emits one).
PLUGIN_LIB="$(find "$PLUGIN_BUILD" \
    \( -name 'OEconnect.so' -o -name 'OEconnect.dylib' -o -name 'OEconnect.bundle' \) \
    -print 2>/dev/null | head -n1 || true)"
[ -n "$PLUGIN_LIB" ] || { echo "OEconnect plugin library not produced" >&2; exit 1; }

mkdir -p "$DIST"
cp -R "$PLUGIN_LIB" "$DIST/"

echo "OEconnect plugin built: $PLUGIN_LIB"
echo "Copied to: $DIST"
echo "Install: drop it into the plugins folder of a GUI whose plugin API is v$API_VER."
echo "         (a GUI with a different API version will refuse to load it)"
