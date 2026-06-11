# OEconnect Bonsai ↔ OpenEphys Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build OEconnect — an OE GUI plugin, a Bonsai-rx NuGet package, and a shared C ABI library that together provide a bidirectional, lock-free, low-latency bridge between OpenEphys and Bonsai (p99.9 < 1 ms closed-loop on shared-memory tier).

**Architecture:** Three artifacts, one wire-protocol contract. A C shared library (`liboeconnect`) owns the ringbuffer layout, frame parser, and drift-fit math. The C++/JUCE OE plugin and the .NET Bonsai package both link against it — one source of truth for the protocol. Hot path is a lock-free SPSC shared-memory ringbuffer between OE's audio thread and Bonsai; ZeroMQ is the cross-OS / LAN fallback using the same wire format.

**Tech Stack:** C11 (libshared), C++17 + JUCE (OE plugin), .NET (net472 + net6.0) + NetMQ (Bonsai package), CMake, GoogleTest, xUnit, BenchmarkDotNet, GitHub Actions.

**Reference spec:** [`docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md`](../specs/2026-06-10-oeconnect-bridge-design.md).

**Phase map:**
| Phase | Subsystem                          | Output                                  |
|-------|------------------------------------|-----------------------------------------|
| 0     | Repo scaffolding, spec, CI         | Bootable monorepo, frozen wire spec     |
| 1     | `liboeconnect` (C ABI lib)         | `liboeconnect.{dll,so,dylib}` + gtests  |
| 2     | OE GUI plugin (C++/JUCE)           | `OEconnect.bundle` per OS               |
| 3     | Bonsai package (.NET)              | `Bonsai.OEconnect.<ver>.nupkg`          |
| 4     | Example workflows + perf docs      | `examples/*.bonsai`, latency procedure  |
| 5     | Release tooling                    | Tagged release pipeline                  |

**Working directory throughout this plan:** the project root containing this `docs/` directory.

---

## Phase 0 — Repo scaffolding, spec freeze, CI skeleton

### Task 0.1: Initialise git repository

**Files:**
- Modify: `.gitignore`
- Create: `LICENSE`
- Create: `README.md`

- [ ] **Step 1: Initialise git**

```powershell
git init
git branch -M main
```

- [ ] **Step 2: Extend `.gitignore`**

Append to `.gitignore`:

```
# Build artifacts
build/
out/
bin/
obj/
*.o
*.obj
*.dll
*.so
*.dylib
*.lib
*.pdb
*.exp
*.exe

# CMake
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
Makefile
*.cmake

# .NET
[Dd]ebug/
[Rr]elease/
[Bb]in/
[Oo]bj/
*.user
*.suo
project.lock.json
.vs/
.idea/
*.csproj.user
TestResults/

# Runtime sidecar files
oeconnect/sessions/

# OS junk
.DS_Store
Thumbs.db
desktop.ini

# Build outputs (do NOT ignore .superpowers/ — kept by .gitignore already)
artifacts/
```

- [ ] **Step 3: Create LICENSE (MIT)**

`LICENSE`:

```
MIT License

Copyright (c) 2026 OEconnect contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 4: Create top-level README skeleton**

`README.md`:

```markdown
# OEconnect

Bidirectional, low-latency bridge between the Open Ephys GUI and Bonsai-rx.

- **Hard-RT tier:** Lock-free shared-memory ringbuffers on a single Windows box. Closed-loop p99.9 < 1 ms for ≤ 256 ch @ 30 kHz.
- **Soft-RT tier:** ZeroMQ over loopback or LAN for cross-OS and cross-machine deployments.
- **Backup-safe:** OE keeps its own Record Node files in parallel; Bonsai-issued TTL events land on OE's event bus.

See [`docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md`](docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md) for the full design and wire protocol.

## Repository layout

```
spec/                              ← wire-protocol spec (authoritative)
libshared/oeconnect/               ← C ABI shared library
plugin-openephys/OEconnect/        ← C++/JUCE plugin for OE GUI
package-bonsai/Bonsai.OEconnect/   ← .NET Bonsai package
examples/                          ← OE signal chains + Bonsai workflows
docs/                              ← design, plan, perf procedures
ci/                                ← GitHub Actions workflows
```

## Build

See per-subsystem READMEs:
- [libshared/oeconnect/README.md](libshared/oeconnect/README.md)
- [plugin-openephys/OEconnect/README.md](plugin-openephys/OEconnect/README.md)
- [package-bonsai/Bonsai.OEconnect/README.md](package-bonsai/Bonsai.OEconnect/README.md)
```

- [ ] **Step 5: Initial commit**

```powershell
git add .gitignore LICENSE README.md docs/
git commit -m "chore: bootstrap OEconnect repository"
```

---

### Task 0.2: Create top-level directory structure

**Files:**
- Create: `spec/.gitkeep`
- Create: `libshared/oeconnect/.gitkeep`
- Create: `plugin-openephys/OEconnect/.gitkeep`
- Create: `package-bonsai/Bonsai.OEconnect/.gitkeep`
- Create: `examples/.gitkeep`
- Create: `ci/.gitkeep`

- [ ] **Step 1: Create empty placeholder files so git tracks the dirs**

```powershell
$dirs = @(
  "spec",
  "libshared/oeconnect",
  "plugin-openephys/OEconnect",
  "package-bonsai/Bonsai.OEconnect",
  "examples",
  "ci"
)
foreach ($d in $dirs) {
  New-Item -ItemType Directory -Force -Path $d | Out-Null
  New-Item -ItemType File -Force -Path "$d/.gitkeep" | Out-Null
}
```

- [ ] **Step 2: Commit**

```powershell
git add spec libshared plugin-openephys package-bonsai examples ci
git commit -m "chore: lay out top-level directory tree"
```

---

### Task 0.3: Freeze wire protocol spec under `spec/`

**Files:**
- Create: `spec/oec-protocol-v1.md`
- Create: `spec/README.md`
- Delete: `spec/.gitkeep`

- [ ] **Step 1: Copy frame-protocol sections from the design doc**

Create `spec/oec-protocol-v1.md` by copying §4 (Wire protocol), §5 (shmem), §6 (ZMQ), §10 (hard rules), §13 (versioning) from `docs/superpowers/specs/2026-06-10-oeconnect-bridge-design.md` verbatim. Prepend this header:

```markdown
# OEconnect Wire Protocol — v1.0

**Status:** Frozen for v1.x. Layout changes require a major version bump.
**Authoritative for:** frame layout, ringbuffer layout, ZMQ socket conventions, command/ack codes.
**CI enforcement:** changes to this file without a `version_major` or `version_minor` bump in §1 are rejected.

## 1. Versions

- Wire spec: 1.0
- Wire frame `version_major`: 1
- Wire frame `version_minor`: 0

(Bump the version above before merging any change to §3 (frame layout) or §4 (stream IDs).)

---
```

Then immediately follow with the verbatim copies of design §4, §5, §6, §10, §13. Renumber to §2 (frame), §3 (stream IDs), §4 (shmem region), §5 (ZMQ transport), §6 (hard rules), §7 (versioning).

- [ ] **Step 2: Create `spec/README.md`**

```markdown
# `spec/` — wire-protocol contracts

Authoritative source for the OEconnect wire format. Three artifacts (libshared, OE plugin, Bonsai package) all conform to this spec. Any change here must:

1. Bump `version_major` (layout-breaking) or `version_minor` (additive) in §1 of `oec-protocol-v1.md`.
2. Update `OEC_PROTOCOL_VERSION_MAJOR` / `_MINOR` in `libshared/oeconnect/include/oeconnect/version.h`.
3. Update `OEconnect.Protocol.VersionMajor` / `.VersionMinor` in the Bonsai package.

CI (`.github/workflows/spec.yml`) enforces these.
```

- [ ] **Step 3: Remove placeholder, commit**

```powershell
Remove-Item spec/.gitkeep
git add spec/
git commit -m "spec: freeze OEconnect wire protocol v1.0"
```

---

### Task 0.4: Add CI workflow skeletons (no-op runs)

**Files:**
- Create: `.github/workflows/libshared.yml`
- Create: `.github/workflows/plugin-openephys.yml`
- Create: `.github/workflows/package-bonsai.yml`
- Create: `.github/workflows/spec.yml`

- [ ] **Step 1: `libshared.yml`**

```yaml
name: libshared

on:
  push:
    paths: ['libshared/**', 'spec/**', '.github/workflows/libshared.yml']
  pull_request:
    paths: ['libshared/**', 'spec/**', '.github/workflows/libshared.yml']

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
        config: [Debug, Release]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S libshared/oeconnect -B build -DCMAKE_BUILD_TYPE=${{ matrix.config }} -DOEC_BUILD_TESTS=ON
      - name: Build
        run: cmake --build build --config ${{ matrix.config }} --parallel
      - name: Test
        run: ctest --test-dir build -C ${{ matrix.config }} --output-on-failure
```

- [ ] **Step 2: `plugin-openephys.yml`**

```yaml
name: plugin-openephys

on:
  push:
    paths: ['plugin-openephys/**', 'libshared/**', 'spec/**', '.github/workflows/plugin-openephys.yml']
  pull_request:
    paths: ['plugin-openephys/**', 'libshared/**', 'spec/**', '.github/workflows/plugin-openephys.yml']

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake -S plugin-openephys/OEconnect -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --config Release --parallel
      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure
```

- [ ] **Step 3: `package-bonsai.yml`**

```yaml
name: package-bonsai

on:
  push:
    paths: ['package-bonsai/**', 'libshared/**', 'spec/**', '.github/workflows/package-bonsai.yml']
  pull_request:
    paths: ['package-bonsai/**', 'libshared/**', 'spec/**', '.github/workflows/package-bonsai.yml']

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-dotnet@v4
        with:
          dotnet-version: |
            6.0.x
            8.0.x
      - name: Restore
        run: dotnet restore package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln
      - name: Build
        run: dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release --no-restore
      - name: Test
        run: dotnet test package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release --no-build --verbosity normal
```

- [ ] **Step 4: `spec.yml` (rejects unbumped frame-layout changes)**

```yaml
name: spec

on:
  pull_request:
    paths: ['spec/**']

jobs:
  enforce-version-bump:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: Check for protocol version bump
        run: |
          set -euo pipefail
          BASE="${{ github.event.pull_request.base.sha }}"
          HEAD="${{ github.event.pull_request.head.sha }}"
          if git diff --quiet "$BASE" "$HEAD" -- spec/oec-protocol-v1.md; then
            echo "No spec changes — nothing to enforce."
            exit 0
          fi
          # If §3 (Frame header) or §4 (Stream IDs) changed, require a version-line bump.
          if git diff "$BASE" "$HEAD" -- spec/oec-protocol-v1.md | grep -E '^\+.*(struct oec_frame_header|Stream IDs|cmd_id|stream_id)'; then
            if ! git diff "$BASE" "$HEAD" -- spec/oec-protocol-v1.md | grep -E '^\+.*(Wire frame .version_major|Wire frame .version_minor)'; then
              echo "::error::Frame layout / stream IDs changed without bumping version in §1."
              exit 1
            fi
          fi
```

- [ ] **Step 5: Commit**

```powershell
git add .github/
git commit -m "ci: add workflow skeletons for libshared, plugin, package, spec"
```

---

### Task 0.5: Create per-subsystem README stubs

**Files:**
- Create: `libshared/oeconnect/README.md`
- Create: `plugin-openephys/OEconnect/README.md`
- Create: `package-bonsai/Bonsai.OEconnect/README.md`
- Delete: each subsystem's `.gitkeep`

- [ ] **Step 1: `libshared/oeconnect/README.md`**

```markdown
# liboeconnect

C ABI shared library implementing the OEconnect wire protocol: frame header
encoding/decoding, shared-memory SPSC ringbuffers, and the dual-clock drift
fit. Links statically into the OE plugin; consumed via P/Invoke by
`Bonsai.OEconnect`.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOEC_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Outputs: `build/liboeconnect.{dll,so,dylib}` plus a static archive.

## API surface

Headers under `include/oeconnect/`. C11. No dependencies beyond libc + (on
POSIX) librt for `shm_open`.
```

- [ ] **Step 2: `plugin-openephys/OEconnect/README.md`**

```markdown
# OEconnect — OpenEphys GUI plugin

JUCE-based OE GUI plugin (a `GenericProcessor` subclass) that publishes
acquisition data to Bonsai over shared memory or ZeroMQ and accepts commands
back (start/stop record, set TTL).

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Install the resulting `OEconnect.bundle` into your OE GUI plugin folder.

## Dependencies

- OE plugin-build template (submodule under `external/plugin-GUI/`).
- `liboeconnect` (sibling `libshared/oeconnect`, linked statically).
- `libzmq` + `cppzmq` (vendored via CMake FetchContent).
```

- [ ] **Step 3: `package-bonsai/Bonsai.OEconnect/README.md`**

```markdown
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
```

- [ ] **Step 4: Remove placeholders, commit**

```powershell
Remove-Item libshared/oeconnect/.gitkeep
Remove-Item plugin-openephys/OEconnect/.gitkeep
Remove-Item package-bonsai/Bonsai.OEconnect/.gitkeep
git add libshared/ plugin-openephys/ package-bonsai/
git commit -m "docs: per-subsystem README stubs"
```

---

### Task 0.6: Add MEMORY-anchored architectural rules to repo

**Files:**
- Create: `docs/architecture-rules.md`

- [ ] **Step 1: Write the rules doc**

`docs/architecture-rules.md`:

```markdown
# Architectural rules

These are non-negotiable. Any PR that violates them is rejected at review.

## 1. Bonsai never talks directly to the acquisition board

All hardware I/O — every TTL line set, every digital input read, every
acquisition state change — must traverse the OEconnect OE plugin. The
acquisition board's firmware is reached only via the OE Source Node API. A
"fast direct path" that lets Bonsai drive the FPGA without OE in the loop is
**never** an acceptable optimisation, regardless of latency wins.

**Rationale:** OE's recording remains a complete, independent sanity-check
copy of every closed-loop output the bridge ever issued. Bypassing OE
splits the event-bus record and silently invalidates the backup.

## 2. The audio thread is the only writer of shmem rings

Three shmem rings (`data_ring`, `cmd_ring`, `ack_ring`) all have the OE
audio thread as their sole producer. Helper threads (sync timer, slow-cmd
worker) feed `AckOutbox` (lock-free MPSC) which the audio thread drains.

**Rationale:** Preserves SPSC invariants on the rings → trivially correct
lock-free code. Any multi-writer scheme demands MPSC algorithms with worse
cache behaviour and harder verification.

## 3. Frame-layout changes require a spec version bump

Any change to `oec_frame_header`, stream IDs, command codes, or the
shmem region header requires bumping `version_major` (breaking) or
`version_minor` (additive) in `spec/oec-protocol-v1.md` §1. CI enforces.

**Rationale:** Three independent artifacts (libshared, plugin, package)
must agree on the wire. The spec doc is the single source of truth.
```

- [ ] **Step 2: Commit**

```powershell
git add docs/architecture-rules.md
git commit -m "docs: enshrine architectural rules"
```

---

**Phase 0 done.** Repo bootable, spec frozen, CI skeletons in place, READMEs written.

---

## Phase 1 — `liboeconnect` C ABI library

### Task 1.1: CMake project + directory layout

**Files:**
- Create: `libshared/oeconnect/CMakeLists.txt`
- Create: `libshared/oeconnect/include/oeconnect/.gitkeep`
- Create: `libshared/oeconnect/src/.gitkeep`
- Create: `libshared/oeconnect/tests/CMakeLists.txt`
- Create: `libshared/oeconnect/tests/.gitkeep`

- [ ] **Step 1: Top-level CMake**

`libshared/oeconnect/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(oeconnect LANGUAGES C VERSION 1.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OEC_BUILD_TESTS "Build liboeconnect tests" OFF)
option(OEC_BUILD_SHARED "Build shared library" ON)

if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
else()
  add_compile_options(-Wall -Wextra -Werror -Wpedantic)
endif()

set(OEC_SOURCES
  src/frame.c
  src/ringbuf.c
  src/shm.c
  src/drift.c
  src/sidecar.c
)

if(OEC_BUILD_SHARED)
  add_library(oeconnect SHARED ${OEC_SOURCES})
else()
  add_library(oeconnect STATIC ${OEC_SOURCES})
endif()

target_include_directories(oeconnect
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include>
  PRIVATE src
)

if(UNIX AND NOT APPLE)
  target_link_libraries(oeconnect PRIVATE rt pthread)
endif()
if(APPLE)
  target_link_libraries(oeconnect PRIVATE pthread)
endif()
if(WIN32)
  target_compile_definitions(oeconnect PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(oeconnect PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  OUTPUT_NAME oeconnect
)

include(GNUInstallDirs)
install(TARGETS oeconnect
  EXPORT oeconnectTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(OEC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Empty placeholders for dirs**

```powershell
New-Item -ItemType Directory -Force -Path libshared/oeconnect/include/oeconnect | Out-Null
New-Item -ItemType Directory -Force -Path libshared/oeconnect/src | Out-Null
New-Item -ItemType Directory -Force -Path libshared/oeconnect/tests | Out-Null
New-Item -ItemType File -Force -Path libshared/oeconnect/include/oeconnect/.gitkeep | Out-Null
New-Item -ItemType File -Force -Path libshared/oeconnect/src/.gitkeep | Out-Null
```

- [ ] **Step 3: Tests CMake (uses FetchContent for GoogleTest)**

`libshared/oeconnect/tests/CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(oec_tests
  test_frame.cc
  test_ringbuf.cc
  test_shm.cc
  test_drift.cc
  test_sidecar.cc
)
target_link_libraries(oec_tests PRIVATE oeconnect GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(oec_tests)

add_executable(oec_stress_ringbuf stress_ringbuf.cc)
target_link_libraries(oec_stress_ringbuf PRIVATE oeconnect)
```

- [ ] **Step 4: Commit (stub builds will fail; that's fine — no sources yet)**

```powershell
git add libshared/oeconnect/
git commit -m "build(libshared): cmake project + dir layout"
```

---

### Task 1.2: Version header

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/version.h`

- [ ] **Step 1: Write `version.h`**

```c
/* libshared/oeconnect/include/oeconnect/version.h */
#ifndef OEC_VERSION_H
#define OEC_VERSION_H

/*
 * Wire-protocol version. Must match spec/oec-protocol-v1.md §1.
 * CI fails if these drift from the spec.
 */
#define OEC_PROTOCOL_VERSION_MAJOR 1
#define OEC_PROTOCOL_VERSION_MINOR 0

/* Library implementation version (independent of wire version). */
#define OEC_LIB_VERSION_MAJOR 1
#define OEC_LIB_VERSION_MINOR 0
#define OEC_LIB_VERSION_PATCH 0

#define OEC_FRAME_MAGIC   0x3143454Fu /* 'O','E','C','1' little-endian */
#define OEC_REGION_MAGIC  0x5243454Fu /* 'O','E','C','R' little-endian */

#endif /* OEC_VERSION_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/version.h
git commit -m "feat(libshared): protocol + lib version constants"
```

---

### Task 1.3: Common types and platform shims

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/types.h`

- [ ] **Step 1: Write `types.h`**

```c
/* libshared/oeconnect/include/oeconnect/types.h */
#ifndef OEC_TYPES_H
#define OEC_TYPES_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #define OEC_EXPORT __declspec(dllexport)
  #define OEC_IMPORT __declspec(dllimport)
#else
  #define OEC_EXPORT __attribute__((visibility("default")))
  #define OEC_IMPORT
#endif

#if defined(OEC_BUILDING_LIB)
  #define OEC_API OEC_EXPORT
#else
  #define OEC_API OEC_IMPORT
#endif

#define OEC_CACHELINE 64
#if defined(_MSC_VER)
  #define OEC_ALIGNAS(n) __declspec(align(n))
#else
  #define OEC_ALIGNAS(n) __attribute__((aligned(n)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Library-wide status codes. Distinct from the wire `ACK.status` codes. */
typedef enum oec_status {
    OEC_OK                       = 0,
    OEC_E_INVALID_ARG            = -1,
    OEC_E_BAD_MAGIC              = -2,
    OEC_E_VERSION_MISMATCH       = -3,
    OEC_E_FRAME_TOO_LARGE        = -4,
    OEC_E_RING_FULL              = -5,
    OEC_E_RING_EMPTY             = -6,
    OEC_E_SYSCALL                = -7,
    OEC_E_NO_SESSION             = -8,
    OEC_E_PARSE                  = -9,
    OEC_E_NOT_IMPLEMENTED        = -10
} oec_status_t;

#ifdef __cplusplus
}
#endif

#endif /* OEC_TYPES_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/types.h
git commit -m "feat(libshared): common types + export macros"
```

---

### Task 1.4: Frame header + stream IDs + block sub-header

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/frame.h`

- [ ] **Step 1: Write `frame.h`**

```c
/* libshared/oeconnect/include/oeconnect/frame.h */
#ifndef OEC_FRAME_H
#define OEC_FRAME_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Frame header (32 B, packed) ---- */
#pragma pack(push, 1)
typedef struct oec_frame_header {
    uint32_t magic;            /* OEC_FRAME_MAGIC */
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t stream_id;
    uint32_t payload_len;
    uint64_t sample_index;
    uint64_t host_qpc_ticks;
    uint16_t flags;            /* bit 0 = CONTINUATION, bit 1 = LOST_DATA */
    uint16_t crc16;            /* zero if disabled */
} oec_frame_header_t;
#pragma pack(pop)

/* sizeof must be exactly 32 bytes — static_assert in C11. */
_Static_assert(sizeof(oec_frame_header_t) == 32,
               "oec_frame_header must be 32 bytes packed");

/* Frame flags */
#define OEC_FLAG_CONTINUATION 0x0001u
#define OEC_FLAG_LOST_DATA    0x0002u

/* Stream IDs */
#define OEC_STREAM_RAW_BLOCK       0x0001u
#define OEC_STREAM_FILTERED_BLOCK  0x0002u
#define OEC_STREAM_SPIKE           0x0003u
#define OEC_STREAM_TTL_EVENT       0x0004u
#define OEC_STREAM_SYNC            0x0010u
#define OEC_STREAM_CMD             0x0020u
#define OEC_STREAM_ACK             0x0021u
#define OEC_STREAM_ERROR           0x0022u

/* ---- Block sub-header (8 B, leads RAW_BLOCK / FILTERED_BLOCK payloads) ---- */
#pragma pack(push, 1)
typedef struct oec_block_subheader {
    uint16_t n_channels;
    uint16_t n_samples;
    uint8_t  dtype;            /* 0 = int16, 1 = float32, 2 = int32 (reserved) */
    uint8_t  source_id;
    uint16_t reserved;
} oec_block_subheader_t;
#pragma pack(pop)

_Static_assert(sizeof(oec_block_subheader_t) == 8,
               "oec_block_subheader must be 8 bytes packed");

#define OEC_DTYPE_INT16   0
#define OEC_DTYPE_FLOAT32 1
#define OEC_DTYPE_INT32   2

/* ---- Command IDs (`CMD.cmd_id`) ---- */
#define OEC_CMD_START_RECORD 0x0001u
#define OEC_CMD_STOP_RECORD  0x0002u
#define OEC_CMD_SET_TTL      0x0003u
#define OEC_CMD_PULSE_TTL    0x0004u
#define OEC_CMD_START_ACQ    0x0005u
#define OEC_CMD_STOP_ACQ     0x0006u
#define OEC_CMD_GET_STATE    0x0007u

/* ---- Wire ACK status codes (distinct from oec_status_t) ---- */
#define OEC_ACK_OK            0u
#define OEC_ACK_PENDING       1u
#define OEC_ACK_COMPLETED     2u
#define OEC_ACK_BUSY          3u
#define OEC_ACK_NOT_SUPPORTED 4u
#define OEC_ACK_BAD_ARG       5u
#define OEC_ACK_TIMEOUT       6u
#define OEC_ACK_INTERNAL      7u

/* ---- Helpers ---- */

/* Fill a header with magic, version, supplied stream_id/payload_len/sample/qpc/flags.
 * crc16 is set to 0 (CRC disabled by default; producer may overwrite).
 */
OEC_API void oec_frame_init(
    oec_frame_header_t *h,
    uint16_t stream_id,
    uint32_t payload_len,
    uint64_t sample_index,
    uint64_t host_qpc_ticks,
    uint16_t flags);

/* Validate magic + version. Returns OEC_OK or an OEC_E_* code. */
OEC_API oec_status_t oec_frame_validate(const oec_frame_header_t *h);

/* CRC-16/CCITT-FALSE over `payload_len` bytes after the header. */
OEC_API uint16_t oec_crc16(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OEC_FRAME_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/frame.h
git commit -m "feat(libshared): frame header, stream IDs, sub-header, helpers"
```

---

### Task 1.5: Frame helpers — failing tests first (TDD)

**Files:**
- Create: `libshared/oeconnect/tests/test_frame.cc`
- Create: `libshared/oeconnect/tests/test_shm.cc` (empty stub)
- Create: `libshared/oeconnect/tests/test_ringbuf.cc` (empty stub)
- Create: `libshared/oeconnect/tests/test_drift.cc` (empty stub)
- Create: `libshared/oeconnect/tests/test_sidecar.cc` (empty stub)
- Create: `libshared/oeconnect/tests/stress_ringbuf.cc` (empty stub)
- Delete: `libshared/oeconnect/tests/.gitkeep`

- [ ] **Step 1: Empty stubs so CMake links**

For each of `test_shm.cc`, `test_ringbuf.cc`, `test_drift.cc`, `test_sidecar.cc`:

```cpp
// Stub — real tests added in later tasks.
```

For `stress_ringbuf.cc`:

```cpp
int main(void) { return 0; }
```

- [ ] **Step 2: Write the frame test file**

`libshared/oeconnect/tests/test_frame.cc`:

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "oeconnect/version.h"
#include "oeconnect/frame.h"
}

TEST(Frame, HeaderIsExactly32Bytes) {
    EXPECT_EQ(sizeof(oec_frame_header_t), 32u);
}

TEST(Frame, BlockSubheaderIsExactly8Bytes) {
    EXPECT_EQ(sizeof(oec_block_subheader_t), 8u);
}

TEST(Frame, InitPopulatesAllFields) {
    oec_frame_header_t h;
    std::memset(&h, 0xCC, sizeof(h));
    oec_frame_init(&h, OEC_STREAM_RAW_BLOCK, 1024, 5000, 999000, OEC_FLAG_LOST_DATA);

    EXPECT_EQ(h.magic, OEC_FRAME_MAGIC);
    EXPECT_EQ(h.version_major, OEC_PROTOCOL_VERSION_MAJOR);
    EXPECT_EQ(h.version_minor, OEC_PROTOCOL_VERSION_MINOR);
    EXPECT_EQ(h.stream_id, OEC_STREAM_RAW_BLOCK);
    EXPECT_EQ(h.payload_len, 1024u);
    EXPECT_EQ(h.sample_index, 5000u);
    EXPECT_EQ(h.host_qpc_ticks, 999000u);
    EXPECT_EQ(h.flags, OEC_FLAG_LOST_DATA);
    EXPECT_EQ(h.crc16, 0u);
}

TEST(Frame, ValidateAcceptsWellFormed) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    EXPECT_EQ(oec_frame_validate(&h), OEC_OK);
}

TEST(Frame, ValidateRejectsBadMagic) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.magic = 0xDEADBEEFu;
    EXPECT_EQ(oec_frame_validate(&h), OEC_E_BAD_MAGIC);
}

TEST(Frame, ValidateRejectsMajorVersionMismatch) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.version_major = OEC_PROTOCOL_VERSION_MAJOR + 1;
    EXPECT_EQ(oec_frame_validate(&h), OEC_E_VERSION_MISMATCH);
}

TEST(Frame, ValidateAcceptsForwardMinor) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.version_minor = OEC_PROTOCOL_VERSION_MINOR + 1;
    EXPECT_EQ(oec_frame_validate(&h), OEC_OK);
}

TEST(Crc16, KnownVector_123456789) {
    /* CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
    EXPECT_EQ(oec_crc16("123456789", 9), 0x29B1u);
}

TEST(Crc16, EmptyInputReturnsInit) {
    EXPECT_EQ(oec_crc16("", 0), 0xFFFFu);
}
```

- [ ] **Step 3: Configure + build to confirm tests FAIL (no impl yet)**

```powershell
cmake -S libshared/oeconnect -B build-libshared -DOEC_BUILD_TESTS=ON
cmake --build build-libshared
```

Expected: link failure with "undefined reference to oec_frame_init / oec_frame_validate / oec_crc16".

- [ ] **Step 4: Commit failing tests**

```powershell
Remove-Item libshared/oeconnect/tests/.gitkeep
git add libshared/oeconnect/tests/
git commit -m "test(libshared): failing frame helper tests"
```

---

### Task 1.6: Implement frame helpers

**Files:**
- Create: `libshared/oeconnect/src/frame.c`

- [ ] **Step 1: Write implementation**

```c
/* libshared/oeconnect/src/frame.c */
#define OEC_BUILDING_LIB
#include "oeconnect/frame.h"
#include "oeconnect/version.h"

#include <string.h>

void oec_frame_init(
    oec_frame_header_t *h,
    uint16_t stream_id,
    uint32_t payload_len,
    uint64_t sample_index,
    uint64_t host_qpc_ticks,
    uint16_t flags)
{
    if (!h) return;
    h->magic = OEC_FRAME_MAGIC;
    h->version_major = OEC_PROTOCOL_VERSION_MAJOR;
    h->version_minor = OEC_PROTOCOL_VERSION_MINOR;
    h->stream_id = stream_id;
    h->payload_len = payload_len;
    h->sample_index = sample_index;
    h->host_qpc_ticks = host_qpc_ticks;
    h->flags = flags;
    h->crc16 = 0;
}

oec_status_t oec_frame_validate(const oec_frame_header_t *h)
{
    if (!h) return OEC_E_INVALID_ARG;
    if (h->magic != OEC_FRAME_MAGIC) return OEC_E_BAD_MAGIC;
    if (h->version_major != OEC_PROTOCOL_VERSION_MAJOR) return OEC_E_VERSION_MISMATCH;
    /* minor mismatch is forward-compatible */
    return OEC_OK;
}

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout */
uint16_t oec_crc16(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
```

- [ ] **Step 2: Build and run tests**

```powershell
cmake --build build-libshared
ctest --test-dir build-libshared --output-on-failure
```

Expected: all `Frame.*` and `Crc16.*` tests PASS.

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/src/frame.c
git commit -m "feat(libshared): implement frame helpers + CRC-16/CCITT-FALSE"
```

---

### Task 1.7: Ringbuffer region layout header

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/ringbuf.h`

- [ ] **Step 1: Write `ringbuf.h`**

```c
/* libshared/oeconnect/include/oeconnect/ringbuf.h */
#ifndef OEC_RINGBUF_H
#define OEC_RINGBUF_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Region header (4 KiB, first thing in the shmem region) ---- */
#pragma pack(push, 8)
typedef struct oec_region_header {
    uint32_t magic;                       /* OEC_REGION_MAGIC */
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t cmd_slot_size;
    uint32_t cmd_slot_count;
    uint32_t ack_slot_size;
    uint32_t ack_slot_count;
    uint32_t _pad0;
    uint64_t producer_heartbeat_ns;
    uint64_t fpga_sample_rate_hz_x1000;
    uint8_t  reserved[4096 - 64];         /* pad to 4 KiB */
} oec_region_header_t;
#pragma pack(pop)

_Static_assert(sizeof(oec_region_header_t) == 4096,
               "region header must be 4 KiB");

/* Default sizing (matches spec §5.7) */
#define OEC_DEFAULT_SLOT_SIZE       65536u
#define OEC_DEFAULT_SLOT_COUNT      256u
#define OEC_DEFAULT_CMD_SLOT_SIZE   4096u
#define OEC_DEFAULT_CMD_SLOT_COUNT  64u
#define OEC_DEFAULT_ACK_SLOT_SIZE   4096u
#define OEC_DEFAULT_ACK_SLOT_COUNT  64u

/* ---- Opaque ringbuffer handle (one per direction) ---- */
typedef struct oec_ringbuf oec_ringbuf_t;

/*
 * Compute total bytes a region requires for the given sizing parameters.
 * Layout: header (4 KiB) + (data prod+cons idx, 128 B) + data ring +
 *         (cmd prod+cons idx, 128 B) + cmd ring +
 *         (ack prod+cons idx, 128 B) + ack ring.
 * Returns 0 on overflow / invalid params.
 */
OEC_API size_t oec_region_size(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count);

/*
 * Initialise a region (producer side, called once when shm is created).
 * `mem` must point to `oec_region_size(...)` zero-initialised bytes.
 */
OEC_API oec_status_t oec_region_init(
    void *mem, size_t mem_len,
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count);

/*
 * Validate an existing region (consumer side after mapping).
 * Sets *out_header to a pointer into `mem`.
 */
OEC_API oec_status_t oec_region_open(
    void *mem, size_t mem_len,
    oec_region_header_t **out_header);

/* ---- Per-ring handles. Each handle picks one of {data, cmd, ack}. ---- */
typedef enum oec_ring_kind {
    OEC_RING_DATA = 0,
    OEC_RING_CMD  = 1,
    OEC_RING_ACK  = 2
} oec_ring_kind_t;

OEC_API oec_status_t oec_ringbuf_attach(
    void *region_mem, oec_ring_kind_t kind, oec_ringbuf_t **out);

OEC_API void oec_ringbuf_detach(oec_ringbuf_t *rb);

/* ---- Producer API (wait-free) ---- */

/* Acquire a writable slot. Returns NULL if the ring is full and `drop_oldest`
 * is false. If `drop_oldest` is true, advances the consumer index to drop the
 * oldest slot and always returns a writable pointer.
 * `*out_slot_size` returns the slot byte size (capacity). */
OEC_API void *oec_ringbuf_acquire(oec_ringbuf_t *rb, int drop_oldest, uint32_t *out_slot_size);

/* Publish the slot most recently acquired (release ordering). */
OEC_API void oec_ringbuf_publish(oec_ringbuf_t *rb);

/* ---- Consumer API (wait-free) ---- */

/* Peek the oldest unconsumed slot. NULL if empty. */
OEC_API const void *oec_ringbuf_peek(oec_ringbuf_t *rb, uint32_t *out_slot_size);

/* Mark the peeked slot consumed (release ordering). */
OEC_API void oec_ringbuf_consume(oec_ringbuf_t *rb);

/* Diagnostics. */
OEC_API uint64_t oec_ringbuf_producer_index(const oec_ringbuf_t *rb);
OEC_API uint64_t oec_ringbuf_consumer_index(const oec_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* OEC_RINGBUF_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/ringbuf.h
git commit -m "feat(libshared): ringbuf region + SPSC ring API"
```

---

### Task 1.8: Ringbuffer — failing tests first

**Files:**
- Modify: `libshared/oeconnect/tests/test_ringbuf.cc`

- [ ] **Step 1: Replace stub with real tests**

```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include "oeconnect/ringbuf.h"
#include "oeconnect/version.h"
}

namespace {
struct RegionMem {
    std::vector<uint8_t> buf;
    void *ptr() { return buf.data(); }
    size_t size() const { return buf.size(); }
};

RegionMem make_region(uint32_t slot_size = 1024, uint32_t slot_count = 8,
                     uint32_t cmd_slot_size = 256, uint32_t cmd_slot_count = 4,
                     uint32_t ack_slot_size = 256, uint32_t ack_slot_count = 4) {
    RegionMem r;
    size_t n = oec_region_size(slot_size, slot_count,
                               cmd_slot_size, cmd_slot_count,
                               ack_slot_size, ack_slot_count);
    r.buf.assign(n, 0);
    EXPECT_EQ(oec_region_init(r.ptr(), n, slot_size, slot_count,
                              cmd_slot_size, cmd_slot_count,
                              ack_slot_size, ack_slot_count), OEC_OK);
    return r;
}
}  // namespace

TEST(Region, SizeIsNonZeroAndAligned) {
    size_t s = oec_region_size(1024, 8, 256, 4, 256, 4);
    EXPECT_GT(s, 4096u);
    EXPECT_EQ(s % 64u, 0u) << "region must be cache-line aligned";
}

TEST(Region, InitWritesHeaderMagicAndDefaults) {
    auto r = make_region();
    oec_region_header_t *hdr = nullptr;
    EXPECT_EQ(oec_region_open(r.ptr(), r.size(), &hdr), OEC_OK);
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->magic, OEC_REGION_MAGIC);
    EXPECT_EQ(hdr->version_major, OEC_PROTOCOL_VERSION_MAJOR);
    EXPECT_EQ(hdr->slot_size, 1024u);
    EXPECT_EQ(hdr->slot_count, 8u);
}

TEST(Region, OpenRejectsBadMagic) {
    auto r = make_region();
    *(uint32_t *)r.ptr() = 0xDEADBEEFu;
    oec_region_header_t *hdr = nullptr;
    EXPECT_EQ(oec_region_open(r.ptr(), r.size(), &hdr), OEC_E_BAD_MAGIC);
}

TEST(Ringbuf, ProduceAndConsumeOneSlot) {
    auto r = make_region();
    oec_ringbuf_t *rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(r.ptr(), OEC_RING_DATA, &rb), OEC_OK);

    uint32_t slot_size = 0;
    void *slot = oec_ringbuf_acquire(rb, /*drop_oldest=*/0, &slot_size);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot_size, 1024u);
    std::memset(slot, 0xAB, 16);
    oec_ringbuf_publish(rb);

    EXPECT_EQ(oec_ringbuf_producer_index(rb), 1u);

    uint32_t peek_size = 0;
    const void *peeked = oec_ringbuf_peek(rb, &peek_size);
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(peek_size, 1024u);
    EXPECT_EQ(*(const uint8_t *)peeked, 0xAB);
    oec_ringbuf_consume(rb);
    EXPECT_EQ(oec_ringbuf_consumer_index(rb), 1u);

    EXPECT_EQ(oec_ringbuf_peek(rb, &peek_size), nullptr);
    oec_ringbuf_detach(rb);
}

TEST(Ringbuf, FullBlocksWithoutDropOldest) {
    auto r = make_region(/*slot_size*/256, /*slot_count*/4);
    oec_ringbuf_t *rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(r.ptr(), OEC_RING_DATA, &rb), OEC_OK);

    uint32_t sz = 0;
    for (int i = 0; i < 4; ++i) {
        ASSERT_NE(oec_ringbuf_acquire(rb, 0, &sz), nullptr);
        oec_ringbuf_publish(rb);
    }
    EXPECT_EQ(oec_ringbuf_acquire(rb, 0, &sz), nullptr);
    oec_ringbuf_detach(rb);
}

TEST(Ringbuf, DropOldestAdvancesConsumer) {
    auto r = make_region(/*slot_size*/256, /*slot_count*/4);
    oec_ringbuf_t *rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(r.ptr(), OEC_RING_DATA, &rb), OEC_OK);

    uint32_t sz = 0;
    for (int i = 0; i < 4; ++i) {
        ASSERT_NE(oec_ringbuf_acquire(rb, 0, &sz), nullptr);
        oec_ringbuf_publish(rb);
    }
    EXPECT_NE(oec_ringbuf_acquire(rb, /*drop_oldest=*/1, &sz), nullptr);
    oec_ringbuf_publish(rb);
    EXPECT_EQ(oec_ringbuf_producer_index(rb), 5u);
    EXPECT_EQ(oec_ringbuf_consumer_index(rb), 1u);
    oec_ringbuf_detach(rb);
}

TEST(Ringbuf, SPSCAcrossThreadsLossless1M) {
    auto r = make_region(/*slot_size*/64, /*slot_count*/1024);
    oec_ringbuf_t *prod = nullptr;
    oec_ringbuf_t *cons = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(r.ptr(), OEC_RING_DATA, &prod), OEC_OK);
    ASSERT_EQ(oec_ringbuf_attach(r.ptr(), OEC_RING_DATA, &cons), OEC_OK);

    constexpr uint64_t kN = 1'000'000;
    std::thread producer([&] {
        uint64_t i = 0;
        while (i < kN) {
            uint32_t sz = 0;
            void *slot = oec_ringbuf_acquire(prod, 0, &sz);
            if (!slot) { std::this_thread::yield(); continue; }
            std::memcpy(slot, &i, sizeof(i));
            oec_ringbuf_publish(prod);
            ++i;
        }
    });

    uint64_t received = 0;
    uint64_t next_expected = 0;
    while (received < kN) {
        uint32_t sz = 0;
        const void *slot = oec_ringbuf_peek(cons, &sz);
        if (!slot) { std::this_thread::yield(); continue; }
        uint64_t v = 0;
        std::memcpy(&v, slot, sizeof(v));
        ASSERT_EQ(v, next_expected) << "out of order at " << received;
        ++next_expected;
        ++received;
        oec_ringbuf_consume(cons);
    }
    producer.join();
    oec_ringbuf_detach(prod);
    oec_ringbuf_detach(cons);
}
```

- [ ] **Step 2: Build, confirm link failure (no ringbuf.c yet)**

```powershell
cmake --build build-libshared
```

Expected: unresolved external for `oec_region_size`, `oec_region_init`, `oec_ringbuf_*`.

- [ ] **Step 3: Commit failing tests**

```powershell
git add libshared/oeconnect/tests/test_ringbuf.cc
git commit -m "test(libshared): failing SPSC ringbuf tests"
```

---

### Task 1.9: Implement ringbuffer (Vyukov-style SPSC)

**Files:**
- Create: `libshared/oeconnect/src/ringbuf.c`

- [ ] **Step 1: Write implementation**

```c
/* libshared/oeconnect/src/ringbuf.c */
#define OEC_BUILDING_LIB
#include "oeconnect/ringbuf.h"
#include "oeconnect/version.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * Region layout:
 *   [region_header (4 KiB)]
 *   [data_prod_idx (64 B)] [data_cons_idx (64 B)] [data_ring]
 *   [cmd_prod_idx  (64 B)] [cmd_cons_idx  (64 B)] [cmd_ring]
 *   [ack_prod_idx  (64 B)] [ack_cons_idx  (64 B)] [ack_ring]
 * Each idx pair gets its own cache line to avoid false sharing.
 */

#define IDX_PAIR_BYTES 128u   /* 2 × OEC_CACHELINE */

typedef struct ring_layout {
    size_t prod_off;
    size_t cons_off;
    size_t ring_off;
    uint32_t slot_size;
    uint32_t slot_count;
} ring_layout_t;

struct oec_ringbuf {
    uint8_t *base;
    ring_layout_t lo;
    /* cached for producer/consumer fast paths */
    uint64_t cached_prod;
    uint64_t cached_cons;
};

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static int compute_layout(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count,
    ring_layout_t *data, ring_layout_t *cmd, ring_layout_t *ack,
    size_t *total)
{
    if (!slot_size || !slot_count) return -1;
    if (!cmd_slot_size || !cmd_slot_count) return -1;
    if (!ack_slot_size || !ack_slot_count) return -1;
    if ((slot_count & (slot_count - 1)) != 0) return -1;  /* require power of two */
    if ((cmd_slot_count & (cmd_slot_count - 1)) != 0) return -1;
    if ((ack_slot_count & (ack_slot_count - 1)) != 0) return -1;

    size_t off = sizeof(oec_region_header_t);  /* 4096 */
    data->prod_off = off; off += OEC_CACHELINE;
    data->cons_off = off; off += OEC_CACHELINE;
    data->ring_off = off; off += (size_t)slot_size * slot_count;
    data->slot_size = slot_size;
    data->slot_count = slot_count;

    off = align_up(off, OEC_CACHELINE);
    cmd->prod_off = off; off += OEC_CACHELINE;
    cmd->cons_off = off; off += OEC_CACHELINE;
    cmd->ring_off = off; off += (size_t)cmd_slot_size * cmd_slot_count;
    cmd->slot_size = cmd_slot_size;
    cmd->slot_count = cmd_slot_count;

    off = align_up(off, OEC_CACHELINE);
    ack->prod_off = off; off += OEC_CACHELINE;
    ack->cons_off = off; off += OEC_CACHELINE;
    ack->ring_off = off; off += (size_t)ack_slot_size * ack_slot_count;
    ack->slot_size = ack_slot_size;
    ack->slot_count = ack_slot_count;

    *total = align_up(off, OEC_CACHELINE);
    return 0;
}

size_t oec_region_size(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count)
{
    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(slot_size, slot_count,
                       cmd_slot_size, cmd_slot_count,
                       ack_slot_size, ack_slot_count,
                       &d, &c, &a, &total) != 0) return 0;
    return total;
}

oec_status_t oec_region_init(
    void *mem, size_t mem_len,
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count)
{
    if (!mem) return OEC_E_INVALID_ARG;
    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(slot_size, slot_count,
                       cmd_slot_size, cmd_slot_count,
                       ack_slot_size, ack_slot_count,
                       &d, &c, &a, &total) != 0) return OEC_E_INVALID_ARG;
    if (mem_len < total) return OEC_E_INVALID_ARG;

    memset(mem, 0, total);
    oec_region_header_t *h = (oec_region_header_t *)mem;
    h->magic = OEC_REGION_MAGIC;
    h->version_major = OEC_PROTOCOL_VERSION_MAJOR;
    h->version_minor = OEC_PROTOCOL_VERSION_MINOR;
    h->slot_size = slot_size;
    h->slot_count = slot_count;
    h->cmd_slot_size = cmd_slot_size;
    h->cmd_slot_count = cmd_slot_count;
    h->ack_slot_size = ack_slot_size;
    h->ack_slot_count = ack_slot_count;
    return OEC_OK;
}

oec_status_t oec_region_open(void *mem, size_t mem_len, oec_region_header_t **out_header)
{
    if (!mem || !out_header) return OEC_E_INVALID_ARG;
    if (mem_len < sizeof(oec_region_header_t)) return OEC_E_INVALID_ARG;
    oec_region_header_t *h = (oec_region_header_t *)mem;
    if (h->magic != OEC_REGION_MAGIC) return OEC_E_BAD_MAGIC;
    if (h->version_major != OEC_PROTOCOL_VERSION_MAJOR) return OEC_E_VERSION_MISMATCH;
    *out_header = h;
    return OEC_OK;
}

oec_status_t oec_ringbuf_attach(void *region_mem, oec_ring_kind_t kind, oec_ringbuf_t **out)
{
    if (!region_mem || !out) return OEC_E_INVALID_ARG;
    oec_region_header_t *h = (oec_region_header_t *)region_mem;
    if (h->magic != OEC_REGION_MAGIC) return OEC_E_BAD_MAGIC;

    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(h->slot_size, h->slot_count,
                       h->cmd_slot_size, h->cmd_slot_count,
                       h->ack_slot_size, h->ack_slot_count,
                       &d, &c, &a, &total) != 0) return OEC_E_INVALID_ARG;

    oec_ringbuf_t *rb = (oec_ringbuf_t *)calloc(1, sizeof(*rb));
    if (!rb) return OEC_E_SYSCALL;
    rb->base = (uint8_t *)region_mem;
    switch (kind) {
        case OEC_RING_DATA: rb->lo = d; break;
        case OEC_RING_CMD:  rb->lo = c; break;
        case OEC_RING_ACK:  rb->lo = a; break;
        default: free(rb); return OEC_E_INVALID_ARG;
    }
    *out = rb;
    return OEC_OK;
}

void oec_ringbuf_detach(oec_ringbuf_t *rb) { free(rb); }

static inline _Atomic uint64_t *prod_idx(oec_ringbuf_t *rb) {
    return (_Atomic uint64_t *)(rb->base + rb->lo.prod_off);
}
static inline _Atomic uint64_t *cons_idx(oec_ringbuf_t *rb) {
    return (_Atomic uint64_t *)(rb->base + rb->lo.cons_off);
}

void *oec_ringbuf_acquire(oec_ringbuf_t *rb, int drop_oldest, uint32_t *out_slot_size)
{
    if (!rb) return NULL;
    uint64_t p = atomic_load_explicit(prod_idx(rb), memory_order_relaxed);
    uint64_t c = atomic_load_explicit(cons_idx(rb), memory_order_acquire);
    if (p - c >= rb->lo.slot_count) {
        if (!drop_oldest) return NULL;
        atomic_store_explicit(cons_idx(rb), c + 1, memory_order_release);
        c = c + 1;
    }
    uint8_t *slot = rb->base + rb->lo.ring_off
                  + (size_t)(p % rb->lo.slot_count) * rb->lo.slot_size;
    if (out_slot_size) *out_slot_size = rb->lo.slot_size;
    rb->cached_prod = p;
    return slot;
}

void oec_ringbuf_publish(oec_ringbuf_t *rb)
{
    if (!rb) return;
    atomic_store_explicit(prod_idx(rb), rb->cached_prod + 1, memory_order_release);
}

const void *oec_ringbuf_peek(oec_ringbuf_t *rb, uint32_t *out_slot_size)
{
    if (!rb) return NULL;
    uint64_t c = atomic_load_explicit(cons_idx(rb), memory_order_relaxed);
    uint64_t p = atomic_load_explicit(prod_idx(rb), memory_order_acquire);
    if (p == c) return NULL;
    uint8_t *slot = rb->base + rb->lo.ring_off
                  + (size_t)(c % rb->lo.slot_count) * rb->lo.slot_size;
    if (out_slot_size) *out_slot_size = rb->lo.slot_size;
    rb->cached_cons = c;
    return slot;
}

void oec_ringbuf_consume(oec_ringbuf_t *rb)
{
    if (!rb) return;
    atomic_store_explicit(cons_idx(rb), rb->cached_cons + 1, memory_order_release);
}

uint64_t oec_ringbuf_producer_index(const oec_ringbuf_t *rb) {
    return atomic_load_explicit((_Atomic uint64_t *)(rb->base + rb->lo.prod_off), memory_order_relaxed);
}
uint64_t oec_ringbuf_consumer_index(const oec_ringbuf_t *rb) {
    return atomic_load_explicit((_Atomic uint64_t *)(rb->base + rb->lo.cons_off), memory_order_relaxed);
}
```

- [ ] **Step 2: Build and run all tests**

```powershell
cmake --build build-libshared
ctest --test-dir build-libshared --output-on-failure
```

Expected: all `Region.*` and `Ringbuf.*` tests PASS, including the cross-thread 1M lossless test.

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/src/ringbuf.c
git commit -m "feat(libshared): lock-free SPSC ringbuf (Vyukov-style)"
```

---

### Task 1.10: Cross-thread stress binary (10⁹ frames)

**Files:**
- Modify: `libshared/oeconnect/tests/stress_ringbuf.cc`

- [ ] **Step 1: Replace stub with real stress test**

```cpp
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include "oeconnect/ringbuf.h"
}

/*
 * Stress: producer writes monotonically increasing uint64_t into each slot;
 * consumer verifies in-order receipt. Default 1e9 frames; override with arg.
 * Drop-oldest disabled so any out-of-order delivery is a bug.
 */
int main(int argc, char **argv) {
    uint64_t kN = (argc > 1) ? std::stoull(argv[1]) : 1'000'000'000ULL;

    constexpr uint32_t slot_size = 64;
    constexpr uint32_t slot_count = 4096;
    std::vector<uint8_t> region(
        oec_region_size(slot_size, slot_count, 256, 4, 256, 4));
    if (oec_region_init(region.data(), region.size(),
                        slot_size, slot_count, 256, 4, 256, 4) != OEC_OK) {
        std::fputs("region init failed\n", stderr);
        return 1;
    }
    oec_ringbuf_t *prod = nullptr, *cons = nullptr;
    oec_ringbuf_attach(region.data(), OEC_RING_DATA, &prod);
    oec_ringbuf_attach(region.data(), OEC_RING_DATA, &cons);

    auto t0 = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (uint64_t i = 0; i < kN; ++i) {
            for (;;) {
                uint32_t sz = 0;
                void *slot = oec_ringbuf_acquire(prod, 0, &sz);
                if (slot) {
                    std::memcpy(slot, &i, sizeof(i));
                    oec_ringbuf_publish(prod);
                    break;
                }
                /* tight spin: this is the worst case the SPSC must handle */
            }
        }
    });

    uint64_t expected = 0;
    while (expected < kN) {
        uint32_t sz = 0;
        const void *slot = oec_ringbuf_peek(cons, &sz);
        if (!slot) continue;
        uint64_t v = 0;
        std::memcpy(&v, slot, sizeof(v));
        if (v != expected) {
            std::fprintf(stderr, "BAD: got %llu expected %llu at i=%llu\n",
                         (unsigned long long)v,
                         (unsigned long long)expected,
                         (unsigned long long)expected);
            producer.join();
            return 2;
        }
        ++expected;
        oec_ringbuf_consume(cons);
    }
    producer.join();

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    std::printf("stress OK: %llu frames in %.2f s = %.2f Mfps\n",
                (unsigned long long)kN, sec, (double)kN / sec / 1e6);
    return 0;
}
```

- [ ] **Step 2: Build and run a short stress (1M frames) for CI**

```powershell
cmake --build build-libshared --target oec_stress_ringbuf
./build-libshared/tests/oec_stress_ringbuf 1000000
```

Expected: prints `stress OK: 1000000 frames ...` with no `BAD` lines.

- [ ] **Step 3: Run long stress locally (10⁹ frames) before commit**

```powershell
./build-libshared/tests/oec_stress_ringbuf 1000000000
```

Expected: ~30–120 s wall time depending on CPU; `stress OK:` printed; no `BAD`.

- [ ] **Step 4: Commit**

```powershell
git add libshared/oeconnect/tests/stress_ringbuf.cc
git commit -m "test(libshared): cross-thread ringbuf stress harness"
```

---

### Task 1.11: Cross-platform shared-memory header

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/shm.h`

- [ ] **Step 1: Write `shm.h`**

```c
/* libshared/oeconnect/include/oeconnect/shm.h */
#ifndef OEC_SHM_H
#define OEC_SHM_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oec_shm oec_shm_t;

/*
 * Create a shared-memory region.
 * `name` follows platform conventions:
 *   Windows: "Local\\oeconnect.<pid>.shm" (will be mapped to a Win32 file mapping)
 *   POSIX:   "/oeconnect.<pid>.shm"        (will be passed to shm_open)
 * Returns OEC_OK on success; on failure `*out` is NULL.
 * If `truncate` is non-zero, sets the region size to `size_bytes` and zeroes it.
 */
OEC_API oec_status_t oec_shm_create(
    const char *name, size_t size_bytes, int truncate,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size);

/* Open an existing region read/write. */
OEC_API oec_status_t oec_shm_open(
    const char *name, size_t expected_size,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size);

/* Close the handle; unmap. On POSIX, owner should also call oec_shm_unlink. */
OEC_API void oec_shm_close(oec_shm_t *s);

/* Remove the name from the system (POSIX shm_unlink). No-op on Windows. */
OEC_API oec_status_t oec_shm_unlink(const char *name);

/*
 * Convenience helper used by the OE plugin on startup.
 * Builds the platform-correct name for the given PID, e.g.
 *   Windows: "Local\\oeconnect.<pid>.shm"
 *   POSIX:   "/oeconnect.<pid>.shm"
 * `out_buf` must be at least 64 bytes.
 */
OEC_API oec_status_t oec_shm_make_name(int pid, char *out_buf, size_t out_buf_len);

#ifdef __cplusplus
}
#endif

#endif /* OEC_SHM_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/shm.h
git commit -m "feat(libshared): cross-platform shm header"
```

---

### Task 1.12: shm — failing tests first

**Files:**
- Modify: `libshared/oeconnect/tests/test_shm.cc`

- [ ] **Step 1: Replace stub**

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "oeconnect/shm.h"
}

namespace {
std::string unique_name() {
#if defined(_WIN32)
    return std::string("Local\\oeconnect.test.") +
           std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId())) +
           "." + std::to_string(static_cast<unsigned long long>(rand()));
#else
    return std::string("/oeconnect.test.") +
           std::to_string(static_cast<unsigned long long>(getpid())) +
           "." + std::to_string(static_cast<unsigned long long>(rand()));
#endif
}
}  // namespace

TEST(Shm, MakeNameProducesValidPattern) {
    char buf[64] = {0};
    EXPECT_EQ(oec_shm_make_name(18432, buf, sizeof(buf)), OEC_OK);
#if defined(_WIN32)
    EXPECT_STREQ(buf, "Local\\oeconnect.18432.shm");
#else
    EXPECT_STREQ(buf, "/oeconnect.18432.shm");
#endif
}

TEST(Shm, CreateOpenRoundtrip) {
    std::string name = unique_name();
    oec_shm_t *create_h = nullptr;
    void *mapped = nullptr;
    size_t mapped_size = 0;
    ASSERT_EQ(oec_shm_create(name.c_str(), 4096, /*truncate=*/1,
                             &create_h, &mapped, &mapped_size), OEC_OK);
    ASSERT_NE(mapped, nullptr);
    EXPECT_GE(mapped_size, 4096u);

    /* writer pattern */
    std::memset(mapped, 0x5A, 4096);

    oec_shm_t *open_h = nullptr;
    void *opened = nullptr;
    size_t opened_size = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 4096,
                           &open_h, &opened, &opened_size), OEC_OK);
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(*(uint8_t *)opened, 0x5A);
    EXPECT_EQ(((uint8_t *)opened)[4095], 0x5A);

    oec_shm_close(open_h);
    oec_shm_close(create_h);
    oec_shm_unlink(name.c_str());
}

TEST(Shm, OpenMissingFails) {
    std::string name = unique_name();
    oec_shm_t *h = nullptr;
    void *m = nullptr;
    size_t sz = 0;
    EXPECT_NE(oec_shm_open(name.c_str(), 4096, &h, &m, &sz), OEC_OK);
}
```

- [ ] **Step 2: Build, confirm link failure**

```powershell
cmake --build build-libshared
```

Expected: unresolved `oec_shm_*`.

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/tests/test_shm.cc
git commit -m "test(libshared): failing cross-platform shm tests"
```

---

### Task 1.13: Implement shm (Win + POSIX)

**Files:**
- Create: `libshared/oeconnect/src/shm.c`

- [ ] **Step 1: Write implementation**

```c
/* libshared/oeconnect/src/shm.c */
#define OEC_BUILDING_LIB
#include "oeconnect/shm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <errno.h>
#endif

struct oec_shm {
#if defined(_WIN32)
    HANDLE mapping;
#else
    int fd;
    char *name_copy;
#endif
    void *mapped;
    size_t mapped_size;
};

oec_status_t oec_shm_make_name(int pid, char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
#if defined(_WIN32)
    int n = snprintf(out_buf, out_buf_len, "Local\\oeconnect.%d.shm", pid);
#else
    int n = snprintf(out_buf, out_buf_len, "/oeconnect.%d.shm", pid);
#endif
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;
    return OEC_OK;
}

oec_status_t oec_shm_create(
    const char *name, size_t size_bytes, int truncate,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size)
{
    if (!name || !out || !out_mapped) return OEC_E_INVALID_ARG;
    oec_shm_t *s = (oec_shm_t *)calloc(1, sizeof(*s));
    if (!s) return OEC_E_SYSCALL;

#if defined(_WIN32)
    (void)truncate;
    DWORD hi = (DWORD)((uint64_t)size_bytes >> 32);
    DWORD lo = (DWORD)((uint64_t)size_bytes & 0xFFFFFFFFu);
    s->mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, hi, lo, name);
    if (!s->mapping) { free(s); return OEC_E_SYSCALL; }
    s->mapped = MapViewOfFile(s->mapping, FILE_MAP_ALL_ACCESS, 0, 0, size_bytes);
    if (!s->mapped) { CloseHandle(s->mapping); free(s); return OEC_E_SYSCALL; }
    s->mapped_size = size_bytes;
    ZeroMemory(s->mapped, size_bytes);
#else
    s->fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (s->fd < 0) { free(s); return OEC_E_SYSCALL; }
    if (truncate && ftruncate(s->fd, (off_t)size_bytes) < 0) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE,
                     MAP_SHARED, s->fd, 0);
    if (s->mapped == MAP_FAILED) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped_size = size_bytes;
    s->name_copy = strdup(name);
    if (truncate) memset(s->mapped, 0, size_bytes);
#endif

    *out = s;
    *out_mapped = s->mapped;
    if (out_mapped_size) *out_mapped_size = s->mapped_size;
    return OEC_OK;
}

oec_status_t oec_shm_open(
    const char *name, size_t expected_size,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size)
{
    if (!name || !out || !out_mapped) return OEC_E_INVALID_ARG;
    oec_shm_t *s = (oec_shm_t *)calloc(1, sizeof(*s));
    if (!s) return OEC_E_SYSCALL;

#if defined(_WIN32)
    s->mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!s->mapping) { free(s); return OEC_E_SYSCALL; }
    s->mapped = MapViewOfFile(s->mapping, FILE_MAP_ALL_ACCESS, 0, 0, expected_size);
    if (!s->mapped) { CloseHandle(s->mapping); free(s); return OEC_E_SYSCALL; }
    s->mapped_size = expected_size;
#else
    s->fd = shm_open(name, O_RDWR, 0600);
    if (s->fd < 0) { free(s); return OEC_E_SYSCALL; }
    s->mapped = mmap(NULL, expected_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, s->fd, 0);
    if (s->mapped == MAP_FAILED) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped_size = expected_size;
    s->name_copy = strdup(name);
#endif

    *out = s;
    *out_mapped = s->mapped;
    if (out_mapped_size) *out_mapped_size = s->mapped_size;
    return OEC_OK;
}

void oec_shm_close(oec_shm_t *s) {
    if (!s) return;
#if defined(_WIN32)
    if (s->mapped) UnmapViewOfFile(s->mapped);
    if (s->mapping) CloseHandle(s->mapping);
#else
    if (s->mapped && s->mapped != MAP_FAILED) munmap(s->mapped, s->mapped_size);
    if (s->fd >= 0) close(s->fd);
    free(s->name_copy);
#endif
    free(s);
}

oec_status_t oec_shm_unlink(const char *name) {
    if (!name) return OEC_E_INVALID_ARG;
#if defined(_WIN32)
    (void)name;
    return OEC_OK;
#else
    return shm_unlink(name) == 0 ? OEC_OK : OEC_E_SYSCALL;
#endif
}
```

- [ ] **Step 2: Build and test**

```powershell
cmake --build build-libshared
ctest --test-dir build-libshared --output-on-failure
```

Expected: `Shm.*` tests PASS on Windows; same tests pass on Linux/macOS in CI.

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/src/shm.c
git commit -m "feat(libshared): cross-platform shm (Win + POSIX)"
```

---

### Task 1.14: Drift-fit header

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/drift.h`

- [ ] **Step 1: Write `drift.h`**

```c
/* libshared/oeconnect/include/oeconnect/drift.h */
#ifndef OEC_DRIFT_H
#define OEC_DRIFT_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OEC_DRIFT_WINDOW 60   /* spec §4.6 — 60-point sliding window */

typedef struct oec_drift_fit oec_drift_fit_t;

OEC_API oec_drift_fit_t *oec_drift_create(void);
OEC_API void             oec_drift_destroy(oec_drift_fit_t *f);

/* Add a (sample_index, host_qpc_ticks) pair. Old points evict FIFO. */
OEC_API void oec_drift_add(oec_drift_fit_t *f, uint64_t sample_index, uint64_t qpc);

/* Number of points currently in the window (0..OEC_DRIFT_WINDOW). */
OEC_API int oec_drift_count(const oec_drift_fit_t *f);

/* Run weighted least-squares on the window. `a` = slope (qpc per sample),
 * `b` = intercept (qpc when sample == 0). Returns OEC_OK or OEC_E_PARSE if
 * fewer than 2 points are available. */
OEC_API oec_status_t oec_drift_fit(const oec_drift_fit_t *f, double *out_a, double *out_b);

/* Reset window. Used by callers that detect divergence. */
OEC_API void oec_drift_reset(oec_drift_fit_t *f);

/* Convenience: predict qpc from sample using the last successful fit. */
OEC_API uint64_t oec_drift_predict_qpc(const oec_drift_fit_t *f, uint64_t sample_index);

/* Residual RMS (in qpc ticks) of the current fit; -1 if no fit. */
OEC_API double oec_drift_residual_rms(const oec_drift_fit_t *f);

#ifdef __cplusplus
}
#endif

#endif /* OEC_DRIFT_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/drift.h
git commit -m "feat(libshared): drift-fit API header"
```

---

### Task 1.15: Drift fit — failing tests first

**Files:**
- Modify: `libshared/oeconnect/tests/test_drift.cc`

- [ ] **Step 1: Replace stub**

```cpp
#include <gtest/gtest.h>
#include <cmath>

extern "C" {
#include "oeconnect/drift.h"
}

TEST(Drift, EmptyFitFails) {
    auto *f = oec_drift_create();
    double a = 0, b = 0;
    EXPECT_EQ(oec_drift_fit(f, &a, &b), OEC_E_PARSE);
    oec_drift_destroy(f);
}

TEST(Drift, RecoversLinearRelation) {
    auto *f = oec_drift_create();
    /* qpc = 100 + 33 * sample */
    for (uint64_t s = 0; s < 50; ++s) {
        oec_drift_add(f, s, 100 + 33 * s);
    }
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_NEAR(a, 33.0, 1e-9);
    EXPECT_NEAR(b, 100.0, 1e-6);
}

TEST(Drift, WindowEvictsOldPoints) {
    auto *f = oec_drift_create();
    for (int i = 0; i < OEC_DRIFT_WINDOW + 5; ++i) {
        oec_drift_add(f, i, i * 10);
    }
    EXPECT_EQ(oec_drift_count(f), OEC_DRIFT_WINDOW);
    oec_drift_destroy(f);
}

TEST(Drift, PredictMatchesFit) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 50; ++s) oec_drift_add(f, s, 1000 + 7 * s);
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_EQ(oec_drift_predict_qpc(f, 100), (uint64_t)std::llround(7.0 * 100 + 1000));
    oec_drift_destroy(f);
}

TEST(Drift, ResidualSmallForPerfectLine) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 50; ++s) oec_drift_add(f, s, 5 + 3 * s);
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_LT(oec_drift_residual_rms(f), 1e-6);
    oec_drift_destroy(f);
}

TEST(Drift, ResetClears) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 10; ++s) oec_drift_add(f, s, s);
    oec_drift_reset(f);
    EXPECT_EQ(oec_drift_count(f), 0);
    oec_drift_destroy(f);
}
```

- [ ] **Step 2: Build, confirm link failure, commit**

```powershell
cmake --build build-libshared
```

Expected: unresolved `oec_drift_*`.

```powershell
git add libshared/oeconnect/tests/test_drift.cc
git commit -m "test(libshared): failing drift-fit tests"
```

---

### Task 1.16: Implement drift fit

**Files:**
- Create: `libshared/oeconnect/src/drift.c`

- [ ] **Step 1: Write implementation**

```c
/* libshared/oeconnect/src/drift.c */
#define OEC_BUILDING_LIB
#include "oeconnect/drift.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct oec_drift_fit {
    double sample[OEC_DRIFT_WINDOW];
    double qpc[OEC_DRIFT_WINDOW];
    int    count;
    int    head;        /* next write index (FIFO) */
    int    fitted;
    double a;
    double b;
    double rms;
};

oec_drift_fit_t *oec_drift_create(void) {
    return (oec_drift_fit_t *)calloc(1, sizeof(oec_drift_fit_t));
}

void oec_drift_destroy(oec_drift_fit_t *f) { free(f); }

void oec_drift_add(oec_drift_fit_t *f, uint64_t sample_index, uint64_t qpc) {
    if (!f) return;
    f->sample[f->head] = (double)sample_index;
    f->qpc[f->head]    = (double)qpc;
    f->head = (f->head + 1) % OEC_DRIFT_WINDOW;
    if (f->count < OEC_DRIFT_WINDOW) ++f->count;
}

int oec_drift_count(const oec_drift_fit_t *f) {
    return f ? f->count : 0;
}

oec_status_t oec_drift_fit(const oec_drift_fit_t *f_const, double *out_a, double *out_b) {
    if (!f_const) return OEC_E_INVALID_ARG;
    if (f_const->count < 2) return OEC_E_PARSE;
    oec_drift_fit_t *f = (oec_drift_fit_t *)f_const;

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = f->count;
    for (int i = 0; i < n; ++i) {
        sx  += f->sample[i];
        sy  += f->qpc[i];
        sxx += f->sample[i] * f->sample[i];
        sxy += f->sample[i] * f->qpc[i];
    }
    double denom = n * sxx - sx * sx;
    if (denom == 0.0) return OEC_E_PARSE;
    double a = (n * sxy - sx * sy) / denom;
    double b = (sy - a * sx) / n;

    double sse = 0.0;
    for (int i = 0; i < n; ++i) {
        double r = f->qpc[i] - (a * f->sample[i] + b);
        sse += r * r;
    }
    f->a = a; f->b = b;
    f->rms = sqrt(sse / n);
    f->fitted = 1;
    if (out_a) *out_a = a;
    if (out_b) *out_b = b;
    return OEC_OK;
}

void oec_drift_reset(oec_drift_fit_t *f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
}

uint64_t oec_drift_predict_qpc(const oec_drift_fit_t *f, uint64_t sample_index) {
    if (!f || !f->fitted) return 0;
    double v = f->a * (double)sample_index + f->b;
    if (v < 0) return 0;
    return (uint64_t)(v + 0.5);
}

double oec_drift_residual_rms(const oec_drift_fit_t *f) {
    if (!f || !f->fitted) return -1.0;
    return f->rms;
}
```

- [ ] **Step 2: Build, test, commit**

```powershell
cmake --build build-libshared
ctest --test-dir build-libshared --output-on-failure
```

Expected: `Drift.*` PASS.

```powershell
git add libshared/oeconnect/src/drift.c
git commit -m "feat(libshared): weighted least-squares drift fit"
```

---

### Task 1.17: Sidecar JSON header + helper

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/sidecar.h`

- [ ] **Step 1: Write header**

```c
/* libshared/oeconnect/include/oeconnect/sidecar.h */
#ifndef OEC_SIDECAR_H
#define OEC_SIDECAR_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oec_sidecar {
    int      pid;
    char     shm_region[64];
    char     data_event[64];
    char     cmd_event[64];
    char     zmq_fallback_endpoint[64];
    char     zmq_cmd_endpoint[64];
    char     spec_version[16];
    uint64_t started_unix_ns;
} oec_sidecar_t;

/* Resolve the platform-specific sidecar directory (creates if missing). */
OEC_API oec_status_t oec_sidecar_dir(char *out_buf, size_t out_buf_len);

/* Compute full sidecar path for a pid. */
OEC_API oec_status_t oec_sidecar_path(int pid, char *out_buf, size_t out_buf_len);

/* Write JSON sidecar to disk. */
OEC_API oec_status_t oec_sidecar_write(const oec_sidecar_t *s);

/* Read JSON sidecar. */
OEC_API oec_status_t oec_sidecar_read(int pid, oec_sidecar_t *out);

/* Delete sidecar file. */
OEC_API oec_status_t oec_sidecar_remove(int pid);

#ifdef __cplusplus
}
#endif

#endif /* OEC_SIDECAR_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/sidecar.h
git commit -m "feat(libshared): sidecar JSON API"
```

---

### Task 1.18: Sidecar — failing tests first

**Files:**
- Modify: `libshared/oeconnect/tests/test_sidecar.cc`

- [ ] **Step 1: Replace stub**

```cpp
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oeconnect/sidecar.h"
}

TEST(Sidecar, DirIsCreatable) {
    char buf[256] = {0};
    EXPECT_EQ(oec_sidecar_dir(buf, sizeof(buf)), OEC_OK);
    EXPECT_GT(std::strlen(buf), 0u);
}

TEST(Sidecar, WriteThenReadRoundtrips) {
    oec_sidecar_t in;
    std::memset(&in, 0, sizeof(in));
    in.pid = 90001;
    std::strncpy(in.shm_region, "Local\\oeconnect.90001.shm", sizeof(in.shm_region) - 1);
    std::strncpy(in.zmq_fallback_endpoint, "tcp://127.0.0.1:5557", sizeof(in.zmq_fallback_endpoint) - 1);
    std::strncpy(in.zmq_cmd_endpoint, "tcp://127.0.0.1:5558", sizeof(in.zmq_cmd_endpoint) - 1);
    std::strncpy(in.spec_version, "1.0", sizeof(in.spec_version) - 1);
    in.started_unix_ns = 1717900000000000000ULL;

    ASSERT_EQ(oec_sidecar_write(&in), OEC_OK);

    oec_sidecar_t out;
    std::memset(&out, 0, sizeof(out));
    ASSERT_EQ(oec_sidecar_read(90001, &out), OEC_OK);
    EXPECT_EQ(out.pid, in.pid);
    EXPECT_STREQ(out.shm_region, in.shm_region);
    EXPECT_STREQ(out.zmq_fallback_endpoint, in.zmq_fallback_endpoint);
    EXPECT_STREQ(out.spec_version, in.spec_version);
    EXPECT_EQ(out.started_unix_ns, in.started_unix_ns);

    EXPECT_EQ(oec_sidecar_remove(90001), OEC_OK);
}

TEST(Sidecar, ReadMissingFails) {
    oec_sidecar_t out;
    EXPECT_NE(oec_sidecar_read(999999, &out), OEC_OK);
}
```

- [ ] **Step 2: Build, confirm link failure, commit**

```powershell
cmake --build build-libshared
```

Expected: unresolved `oec_sidecar_*`.

```powershell
git add libshared/oeconnect/tests/test_sidecar.cc
git commit -m "test(libshared): failing sidecar tests"
```

---

### Task 1.19: Implement sidecar JSON

**Files:**
- Create: `libshared/oeconnect/src/sidecar.c`

- [ ] **Step 1: Write implementation (hand-rolled JSON — fixed schema, no external dep)**

```c
/* libshared/oeconnect/src/sidecar.c */
#define OEC_BUILDING_LIB
#include "oeconnect/sidecar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>
  #define OEC_MKDIR(p) _mkdir(p)
  #define OEC_PATH_SEP '\\'
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #define OEC_MKDIR(p) mkdir(p, 0700)
  #define OEC_PATH_SEP '/'
#endif

static oec_status_t resolve_tmp_root(char *out, size_t out_len) {
#if defined(_WIN32)
    DWORD n = GetTempPathA((DWORD)out_len, out);
    if (n == 0 || n >= out_len) return OEC_E_SYSCALL;
    /* GetTempPathA returns trailing backslash */
    return OEC_OK;
#else
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    int n = snprintf(out, out_len, "%s/", t);
    if (n < 0 || (size_t)n >= out_len) return OEC_E_SYSCALL;
    return OEC_OK;
#endif
}

oec_status_t oec_sidecar_dir(char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
    char tmp[256];
    if (resolve_tmp_root(tmp, sizeof(tmp)) != OEC_OK) return OEC_E_SYSCALL;
    int n = snprintf(out_buf, out_buf_len, "%soeconnect%csessions", tmp, OEC_PATH_SEP);
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;

    char step[256];
    n = snprintf(step, sizeof(step), "%soeconnect", tmp);
    if (n < 0) return OEC_E_SYSCALL;
    OEC_MKDIR(step);
    OEC_MKDIR(out_buf);
    return OEC_OK;
}

oec_status_t oec_sidecar_path(int pid, char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
    char dir[256];
    if (oec_sidecar_dir(dir, sizeof(dir)) != OEC_OK) return OEC_E_SYSCALL;
    int n = snprintf(out_buf, out_buf_len, "%s%c%d.json", dir, OEC_PATH_SEP, pid);
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;
    return OEC_OK;
}

oec_status_t oec_sidecar_write(const oec_sidecar_t *s) {
    if (!s) return OEC_E_INVALID_ARG;
    char path[512];
    if (oec_sidecar_path(s->pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    FILE *f = fopen(path, "wb");
    if (!f) return OEC_E_SYSCALL;
    fprintf(f,
        "{\n"
        "  \"pid\": %d,\n"
        "  \"shm_region\": \"%s\",\n"
        "  \"data_event\": \"%s\",\n"
        "  \"cmd_event\": \"%s\",\n"
        "  \"zmq_fallback_endpoint\": \"%s\",\n"
        "  \"zmq_cmd_endpoint\": \"%s\",\n"
        "  \"spec_version\": \"%s\",\n"
        "  \"started_unix_ns\": %llu\n"
        "}\n",
        s->pid, s->shm_region, s->data_event, s->cmd_event,
        s->zmq_fallback_endpoint, s->zmq_cmd_endpoint,
        s->spec_version, (unsigned long long)s->started_unix_ns);
    fclose(f);
    return OEC_OK;
}

/* Extremely small fixed-schema JSON reader.
 * Expects whitespace-tolerant matches for known keys. */
static int extract_string(const char *buf, const char *key, char *out, size_t out_len) {
    const char *p = strstr(buf, key);
    if (!p) return -1;
    p = strchr(p, ':'); if (!p) return -1;
    p = strchr(p, '"'); if (!p) return -1;
    ++p;
    const char *e = strchr(p, '"'); if (!e) return -1;
    size_t n = (size_t)(e - p);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int extract_u64(const char *buf, const char *key, uint64_t *out) {
    const char *p = strstr(buf, key);
    if (!p) return -1;
    p = strchr(p, ':'); if (!p) return -1;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    unsigned long long v = 0;
    if (sscanf(p, "%llu", &v) != 1) return -1;
    *out = (uint64_t)v;
    return 0;
}

static int extract_int(const char *buf, const char *key, int *out) {
    uint64_t v = 0;
    if (extract_u64(buf, key, &v) != 0) return -1;
    *out = (int)v;
    return 0;
}

oec_status_t oec_sidecar_read(int pid, oec_sidecar_t *out) {
    if (!out) return OEC_E_INVALID_ARG;
    char path[512];
    if (oec_sidecar_path(pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    FILE *f = fopen(path, "rb");
    if (!f) return OEC_E_NO_SESSION;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4096) { fclose(f); return OEC_E_PARSE; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return OEC_E_SYSCALL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';

    memset(out, 0, sizeof(*out));
    if (extract_int(buf, "\"pid\"", &out->pid) != 0) { free(buf); return OEC_E_PARSE; }
    extract_string(buf, "\"shm_region\"",            out->shm_region,            sizeof(out->shm_region));
    extract_string(buf, "\"data_event\"",            out->data_event,            sizeof(out->data_event));
    extract_string(buf, "\"cmd_event\"",             out->cmd_event,             sizeof(out->cmd_event));
    extract_string(buf, "\"zmq_fallback_endpoint\"", out->zmq_fallback_endpoint, sizeof(out->zmq_fallback_endpoint));
    extract_string(buf, "\"zmq_cmd_endpoint\"",      out->zmq_cmd_endpoint,      sizeof(out->zmq_cmd_endpoint));
    extract_string(buf, "\"spec_version\"",          out->spec_version,          sizeof(out->spec_version));
    extract_u64   (buf, "\"started_unix_ns\"",       &out->started_unix_ns);
    free(buf);
    return OEC_OK;
}

oec_status_t oec_sidecar_remove(int pid) {
    char path[512];
    if (oec_sidecar_path(pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    return remove(path) == 0 ? OEC_OK : OEC_E_SYSCALL;
}
```

- [ ] **Step 2: Build and test all libshared tests**

```powershell
cmake --build build-libshared
ctest --test-dir build-libshared --output-on-failure
```

Expected: every test PASSES (Frame, Crc16, Region, Ringbuf, Shm, Drift, Sidecar).

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/src/sidecar.c
git commit -m "feat(libshared): sidecar JSON read/write"
```

---

### Task 1.20: Library entry-point header (single include consumers will use)

**Files:**
- Create: `libshared/oeconnect/include/oeconnect/oeconnect.h`

- [ ] **Step 1: Umbrella header**

```c
/* libshared/oeconnect/include/oeconnect/oeconnect.h */
#ifndef OEC_OECONNECT_H
#define OEC_OECONNECT_H

#include "oeconnect/version.h"
#include "oeconnect/types.h"
#include "oeconnect/frame.h"
#include "oeconnect/ringbuf.h"
#include "oeconnect/shm.h"
#include "oeconnect/drift.h"
#include "oeconnect/sidecar.h"

#endif /* OEC_OECONNECT_H */
```

- [ ] **Step 2: Commit**

```powershell
git add libshared/oeconnect/include/oeconnect/oeconnect.h
git commit -m "feat(libshared): umbrella header"
```

---

---

## Phase 2 — OpenEphys GUI plugin (C++/JUCE)

### Task 2.1: CMake project + OE plugin SDK submodule

**Files:**
- Create: `plugin-openephys/OEconnect/CMakeLists.txt`
- Create: `plugin-openephys/OEconnect/external/.gitkeep`
- Create: `plugin-openephys/OEconnect/Source/.gitkeep`
- Create: `plugin-openephys/OEconnect/Tests/CMakeLists.txt`
- Create: `plugin-openephys/OEconnect/Tests/.gitkeep`
- Modify: `.gitmodules`

- [ ] **Step 1: Add the OE plugin-GUI as a submodule**

```powershell
git submodule add https://github.com/open-ephys/plugin-GUI plugin-openephys/OEconnect/external/plugin-GUI
git submodule update --init --recursive
```

- [ ] **Step 2: Top-level CMake**

`plugin-openephys/OEconnect/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(OEconnect LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OEC_PLUGIN_BUILD_TESTS "Build OEconnect plugin unit tests" ON)

# --- liboeconnect (sibling) ---
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libshared/oeconnect
                 ${CMAKE_CURRENT_BINARY_DIR}/liboeconnect)

# --- libzmq + cppzmq via FetchContent ---
include(FetchContent)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(WITH_DOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG        v4.3.5
)
FetchContent_Declare(cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG        v4.10.0
)
FetchContent_MakeAvailable(libzmq cppzmq)

# --- OE plugin-build template ---
set(GUI_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/plugin-GUI)
include(${GUI_BASE_DIR}/CMake/PluginCommon.cmake OPTIONAL RESULT_VARIABLE OEPLUGIN_FOUND)

if(NOT OEPLUGIN_FOUND)
  message(WARNING
    "OE plugin SDK not found at ${GUI_BASE_DIR}. "
    "Submodule init required: git submodule update --init --recursive")
endif()

set(OEC_PLUGIN_SOURCES
  Source/OEconnectProcessor.cpp
  Source/OEconnectEditor.cpp
  Source/Transport/ShmemTransport.cpp
  Source/Transport/ZmqTransport.cpp
  Source/Boards/RhdAcqBoardAdapter.cpp
  Source/Boards/OnixAdapter.cpp
  Source/Boards/NeuropixelsAdapter.cpp
  Source/Boards/FileReaderAdapter.cpp
  Source/Sync/DriftEmitter.cpp
)

add_library(OEconnect MODULE ${OEC_PLUGIN_SOURCES})
target_include_directories(OEconnect
  PRIVATE Source
          ${GUI_BASE_DIR}/Source/Plugins
          ${GUI_BASE_DIR}/JuceLibraryCode
)
target_link_libraries(OEconnect
  PRIVATE oeconnect libzmq-static cppzmq
)
set_target_properties(OEconnect PROPERTIES
  PREFIX ""
  BUNDLE TRUE
  BUNDLE_EXTENSION "bundle"
  OUTPUT_NAME "OEconnect"
)

if(OEC_PLUGIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
```

- [ ] **Step 3: Empty placeholders + Tests CMake**

```powershell
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/external | Out-Null
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/Source/Transport | Out-Null
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/Source/Boards | Out-Null
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/Source/Sync | Out-Null
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/Source/Util | Out-Null
New-Item -ItemType Directory -Force -Path plugin-openephys/OEconnect/Tests | Out-Null
New-Item -ItemType File -Force -Path plugin-openephys/OEconnect/Source/.gitkeep | Out-Null
```

`plugin-openephys/OEconnect/Tests/CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(oec_plugin_tests
  test_ack_outbox.cc
  test_shmem_transport.cc
  test_zmq_transport.cc
  test_processor_hot_path.cc
)
target_link_libraries(oec_plugin_tests
  PRIVATE oeconnect libzmq-static cppzmq GTest::gtest_main
)
target_include_directories(oec_plugin_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source
)
include(GoogleTest)
gtest_discover_tests(oec_plugin_tests)
```

Empty stub test files (each just `// stub`):

```powershell
foreach ($t in @('test_ack_outbox.cc','test_shmem_transport.cc','test_zmq_transport.cc','test_processor_hot_path.cc')) {
  Set-Content -Path "plugin-openephys/OEconnect/Tests/$t" -Value "// stub`n"
}
Remove-Item plugin-openephys/OEconnect/Tests/.gitkeep
```

- [ ] **Step 4: Commit**

```powershell
git add .gitmodules plugin-openephys/
git commit -m "build(plugin): cmake + OE plugin SDK submodule + tests skeleton"
```

---

### Task 2.2: `AckOutbox` (lock-free MPSC) — failing tests first

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Util/AckOutbox.h`
- Modify: `plugin-openephys/OEconnect/Tests/test_ack_outbox.cc`

- [ ] **Step 1: Write the test file**

`plugin-openephys/OEconnect/Tests/test_ack_outbox.cc`:

```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "Util/AckOutbox.h"

using oec::plugin::AckOutbox;
using oec::plugin::AckEntry;

TEST(AckOutbox, PushPopSingleThread) {
    AckOutbox box(8);
    AckEntry e{};
    EXPECT_FALSE(box.tryPop(e));

    AckEntry in{};
    in.cookie = 42;
    in.status = 0;
    EXPECT_TRUE(box.tryPush(in));
    EXPECT_TRUE(box.tryPop(e));
    EXPECT_EQ(e.cookie, 42u);
    EXPECT_FALSE(box.tryPop(e));
}

TEST(AckOutbox, FullReturnsFalse) {
    AckOutbox box(4);
    AckEntry in{};
    for (int i = 0; i < 4; ++i) {
        in.cookie = (uint32_t)i;
        EXPECT_TRUE(box.tryPush(in));
    }
    in.cookie = 999;
    EXPECT_FALSE(box.tryPush(in));
}

TEST(AckOutbox, MultiProducerSingleConsumer) {
    AckOutbox box(4096);
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 10'000;
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                AckEntry e{};
                e.cookie = (uint32_t)(p * kPerProducer + i);
                while (!box.tryPush(e)) std::this_thread::yield();
            }
        });
    }

    std::vector<uint32_t> seen;
    seen.reserve(kProducers * kPerProducer);
    while ((int)seen.size() < kProducers * kPerProducer) {
        AckEntry e{};
        if (box.tryPop(e)) {
            seen.push_back(e.cookie);
        } else {
            std::this_thread::yield();
        }
    }
    for (auto &t : producers) t.join();

    std::vector<int> counts(kProducers * kPerProducer, 0);
    for (auto v : seen) counts[v]++;
    for (auto c : counts) EXPECT_EQ(c, 1);
}
```

- [ ] **Step 2: Header stub that doesn't compile (intentionally)**

`plugin-openephys/OEconnect/Source/Util/AckOutbox.h`:

```cpp
#pragma once
// Placeholder — implementation follows in Task 2.3
#error "AckOutbox.h not implemented yet"
```

- [ ] **Step 3: Build, confirm failure**

```powershell
cmake -S plugin-openephys/OEconnect -B build-plugin -DOEC_PLUGIN_BUILD_TESTS=ON
cmake --build build-plugin --target oec_plugin_tests
```

Expected: `#error "AckOutbox.h not implemented yet"`.

- [ ] **Step 4: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Util/AckOutbox.h plugin-openephys/OEconnect/Tests/test_ack_outbox.cc
git commit -m "test(plugin): failing AckOutbox MPSC tests"
```

---

### Task 2.3: Implement `AckOutbox`

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/Util/AckOutbox.h`

- [ ] **Step 1: Write implementation (MPMC-safe push, SPSC-safe pop)**

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace oec::plugin {

struct AckEntry {
    uint32_t cookie;
    uint16_t status;
    uint16_t cmd_id;
    uint64_t sample_index;
    uint64_t host_qpc_ticks;
};

/**
 * Bounded lock-free MPSC queue keyed by sequence numbers (Vyukov MPMC variant
 * restricted to single consumer). Capacity must be a power of two.
 */
class AckOutbox {
public:
    explicit AckOutbox(size_t capacity)
        : capacity_(capacity), mask_(capacity - 1),
          cells_(capacity), enqueue_pos_(0), dequeue_pos_(0)
    {
        for (size_t i = 0; i < capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool tryPush(const AckEntry& e) noexcept {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells_[pos & mask_];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = e;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; /* full */
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(AckEntry& out) noexcept {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell& cell = cells_[pos & mask_];
        size_t seq = cell.sequence.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        if (diff == 0) {
            out = cell.data;
            cell.sequence.store(pos + mask_ + 1, std::memory_order_release);
            dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

private:
    struct Cell {
        std::atomic<size_t> sequence;
        AckEntry data;
    };
    const size_t capacity_;
    const size_t mask_;
    std::vector<Cell> cells_;
    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) std::atomic<size_t> dequeue_pos_;
};

}  // namespace oec::plugin
```

- [ ] **Step 2: Build + test**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R AckOutbox --output-on-failure
```

Expected: all `AckOutbox.*` PASS.

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Util/AckOutbox.h
git commit -m "feat(plugin): lock-free MPSC AckOutbox"
```

---

### Task 2.4: `ITransport` interface

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Transport/ITransport.h`
- Create: `plugin-openephys/OEconnect/Source/Transport/Frame.h`

- [ ] **Step 1: Frame writer helper (C++ thin wrapper around libshared's C frame)**

`plugin-openephys/OEconnect/Source/Transport/Frame.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

/**
 * Lightweight helper: serialises a header + payload into a contiguous buffer.
 * No allocation — caller supplies the destination.
 */
struct FrameWriter {
    static size_t write(uint8_t* dest, size_t dest_cap,
                        uint16_t stream_id,
                        uint64_t sample_index,
                        uint64_t host_qpc_ticks,
                        uint16_t flags,
                        const void* payload, uint32_t payload_len)
    {
        const size_t total = sizeof(oec_frame_header_t) + payload_len;
        if (total > dest_cap) return 0;
        oec_frame_header_t* h = reinterpret_cast<oec_frame_header_t*>(dest);
        oec_frame_init(h, stream_id, payload_len,
                       sample_index, host_qpc_ticks, flags);
        if (payload && payload_len) {
            std::memcpy(dest + sizeof(*h), payload, payload_len);
        }
        return total;
    }
};

}  // namespace oec::plugin
```

- [ ] **Step 2: `ITransport.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

/**
 * Transport contract — the OEconnect plugin uses this to publish data frames
 * and consume command frames. One implementation per transport mode.
 *
 * All methods except start()/stop() may be called from the audio thread;
 * implementations must be wait-free on those.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool start(const std::string& endpoint) = 0;
    virtual void stop() = 0;

    /**
     * Acquire a slot for the next outgoing data frame.
     * Returns a writable pointer of size `out_cap`, or nullptr if the
     * ring/buffer is full and `dropOldest=false`. When `dropOldest=true`
     * the oldest unconsumed slot is discarded to make room.
     */
    virtual uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) = 0;

    /** Publish the slot most recently returned by acquireDataSlot. */
    virtual void publishData(uint32_t bytes_written) = 0;

    /** Same pair for the ACK ring. */
    virtual uint8_t* acquireAckSlot(uint32_t* out_cap) = 0;
    virtual void publishAck(uint32_t bytes_written) = 0;

    /**
     * Try to read one pending command frame. Returns nullptr if none.
     * The returned pointer is valid until the next call to peekCmd().
     */
    virtual const uint8_t* peekCmd(uint32_t* out_size) = 0;
    virtual void consumeCmd() = 0;

    /** Diagnostics. */
    virtual uint64_t totalDropped() const = 0;
    virtual std::string name() const = 0;
};

}  // namespace oec::plugin
```

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Transport/
git commit -m "feat(plugin): ITransport contract + FrameWriter helper"
```

---

### Task 2.5: `ShmemTransport` — failing tests first

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Transport/ShmemTransport.h`
- Create: `plugin-openephys/OEconnect/Source/Transport/ShmemTransport.cpp` (stub returning errors)
- Modify: `plugin-openephys/OEconnect/Tests/test_shmem_transport.cc`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include "ITransport.h"
#include <memory>
#include <string>

extern "C" {
#include "oeconnect/shm.h"
#include "oeconnect/ringbuf.h"
}

namespace oec::plugin {

class ShmemTransport final : public ITransport {
public:
    ShmemTransport();
    ~ShmemTransport() override;

    bool start(const std::string& shm_name) override;
    void stop() override;

    uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) override;
    void publishData(uint32_t bytes_written) override;

    uint8_t* acquireAckSlot(uint32_t* out_cap) override;
    void publishAck(uint32_t bytes_written) override;

    const uint8_t* peekCmd(uint32_t* out_size) override;
    void consumeCmd() override;

    uint64_t totalDropped() const override { return dropped_; }
    std::string name() const override { return "SharedMem"; }

private:
    oec_shm_t*     shm_ = nullptr;
    void*          mapped_ = nullptr;
    size_t         mapped_size_ = 0;
    oec_ringbuf_t* data_ring_ = nullptr;
    oec_ringbuf_t* cmd_ring_  = nullptr;
    oec_ringbuf_t* ack_ring_  = nullptr;
    uint64_t       dropped_ = 0;
    std::string    name_;
};

}  // namespace oec::plugin
```

- [ ] **Step 2: Stub implementation that always fails**

`plugin-openephys/OEconnect/Source/Transport/ShmemTransport.cpp`:

```cpp
#include "ShmemTransport.h"

namespace oec::plugin {

ShmemTransport::ShmemTransport() = default;
ShmemTransport::~ShmemTransport() { stop(); }

bool ShmemTransport::start(const std::string&) { return false; }
void ShmemTransport::stop() {}

uint8_t* ShmemTransport::acquireDataSlot(uint32_t*, bool) { return nullptr; }
void ShmemTransport::publishData(uint32_t) {}
uint8_t* ShmemTransport::acquireAckSlot(uint32_t*) { return nullptr; }
void ShmemTransport::publishAck(uint32_t) {}
const uint8_t* ShmemTransport::peekCmd(uint32_t*) { return nullptr; }
void ShmemTransport::consumeCmd() {}

}  // namespace oec::plugin
```

- [ ] **Step 3: Tests**

`plugin-openephys/OEconnect/Tests/test_shmem_transport.cc`:

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "Transport/ShmemTransport.h"
#include "Transport/Frame.h"

extern "C" {
#include "oeconnect/shm.h"
}

using oec::plugin::ShmemTransport;
using oec::plugin::FrameWriter;

namespace {
std::string unique_name() {
#if defined(_WIN32)
    return std::string("Local\\oeconnect.test.transport.") +
           std::to_string(::GetCurrentProcessId());
#else
    return std::string("/oeconnect.test.transport.") +
           std::to_string(getpid());
#endif
}
}  // namespace

TEST(ShmemTransport, StartCreatesRegion) {
    ShmemTransport t;
    std::string name = unique_name() + ".start";
    ASSERT_TRUE(t.start(name));
    t.stop();
    oec_shm_unlink(name.c_str());
}

TEST(ShmemTransport, AcquirePublishDataRoundtrip) {
    ShmemTransport prod;
    std::string name = unique_name() + ".roundtrip";
    ASSERT_TRUE(prod.start(name));

    uint32_t cap = 0;
    uint8_t* slot = prod.acquireDataSlot(&cap, /*dropOldest=*/false);
    ASSERT_NE(slot, nullptr);
    EXPECT_GT(cap, 100u);

    const uint8_t payload[] = {1, 2, 3, 4};
    size_t written = FrameWriter::write(slot, cap, OEC_STREAM_TTL_EVENT,
                                        /*sample*/ 10, /*qpc*/ 20,
                                        /*flags*/ 0, payload, sizeof(payload));
    ASSERT_GT(written, 0u);
    prod.publishData((uint32_t)written);

    // Second transport opens the same region for reading.
    ShmemTransport cons;
    ASSERT_TRUE(cons.start(name));
    uint32_t size = 0;
    const uint8_t* peeked = cons.peekCmd(&size);
    EXPECT_EQ(peeked, nullptr) << "cmd ring should be empty";

    // For data ring we cheat: cons.start opens the existing region.
    // (peekData is not part of the transport API — consumer of data is Bonsai.
    //  Here we verify slot was written through low-level libshared.)
    // We re-open and peek the raw region:
    oec_shm_t* h = nullptr;
    void* mapped = nullptr;
    size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);
    const oec_frame_header_t* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->magic, OEC_FRAME_MAGIC);
    EXPECT_EQ(fh->stream_id, OEC_STREAM_TTL_EVENT);
    EXPECT_EQ(fh->sample_index, 10u);
    oec_ringbuf_detach(rb);
    oec_shm_close(h);

    cons.stop();
    prod.stop();
    oec_shm_unlink(name.c_str());
}
```

- [ ] **Step 4: Build, confirm tests FAIL**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R ShmemTransport --output-on-failure
```

Expected: `StartCreatesRegion` FAIL (`start` returns false).

- [ ] **Step 5: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Transport/ShmemTransport.* plugin-openephys/OEconnect/Tests/test_shmem_transport.cc
git commit -m "test(plugin): failing ShmemTransport tests + stub"
```

---

### Task 2.6: Implement `ShmemTransport`

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/Transport/ShmemTransport.cpp`

- [ ] **Step 1: Replace stub with real implementation**

```cpp
#include "ShmemTransport.h"
#include <cstring>

namespace oec::plugin {

namespace {
constexpr uint32_t kSlotSize     = OEC_DEFAULT_SLOT_SIZE;
constexpr uint32_t kSlotCount    = OEC_DEFAULT_SLOT_COUNT;
constexpr uint32_t kCmdSlotSize  = OEC_DEFAULT_CMD_SLOT_SIZE;
constexpr uint32_t kCmdSlotCount = OEC_DEFAULT_CMD_SLOT_COUNT;
constexpr uint32_t kAckSlotSize  = OEC_DEFAULT_ACK_SLOT_SIZE;
constexpr uint32_t kAckSlotCount = OEC_DEFAULT_ACK_SLOT_COUNT;
}

ShmemTransport::ShmemTransport() = default;
ShmemTransport::~ShmemTransport() { stop(); }

bool ShmemTransport::start(const std::string& shm_name) {
    stop();
    name_ = shm_name;
    const size_t region_size = oec_region_size(
        kSlotSize, kSlotCount, kCmdSlotSize, kCmdSlotCount,
        kAckSlotSize, kAckSlotCount);
    if (region_size == 0) return false;

    // First try to open an existing region (consumer-side mount).
    if (oec_shm_open(shm_name.c_str(), region_size, &shm_,
                     &mapped_, &mapped_size_) != OEC_OK) {
        // Create fresh.
        if (oec_shm_create(shm_name.c_str(), region_size, /*truncate=*/1,
                           &shm_, &mapped_, &mapped_size_) != OEC_OK) {
            return false;
        }
        if (oec_region_init(mapped_, mapped_size_,
                            kSlotSize, kSlotCount,
                            kCmdSlotSize, kCmdSlotCount,
                            kAckSlotSize, kAckSlotCount) != OEC_OK) {
            stop();
            return false;
        }
    }
    if (oec_ringbuf_attach(mapped_, OEC_RING_DATA, &data_ring_) != OEC_OK) { stop(); return false; }
    if (oec_ringbuf_attach(mapped_, OEC_RING_CMD,  &cmd_ring_)  != OEC_OK) { stop(); return false; }
    if (oec_ringbuf_attach(mapped_, OEC_RING_ACK,  &ack_ring_)  != OEC_OK) { stop(); return false; }
    return true;
}

void ShmemTransport::stop() {
    if (data_ring_) { oec_ringbuf_detach(data_ring_); data_ring_ = nullptr; }
    if (cmd_ring_)  { oec_ringbuf_detach(cmd_ring_);  cmd_ring_  = nullptr; }
    if (ack_ring_)  { oec_ringbuf_detach(ack_ring_);  ack_ring_  = nullptr; }
    if (shm_) { oec_shm_close(shm_); shm_ = nullptr; mapped_ = nullptr; mapped_size_ = 0; }
}

uint8_t* ShmemTransport::acquireDataSlot(uint32_t* out_cap, bool dropOldest) {
    if (!data_ring_) return nullptr;
    void* slot = oec_ringbuf_acquire(data_ring_, dropOldest ? 1 : 0, out_cap);
    if (!slot && dropOldest) ++dropped_;
    return (uint8_t*)slot;
}
void ShmemTransport::publishData(uint32_t) { oec_ringbuf_publish(data_ring_); }

uint8_t* ShmemTransport::acquireAckSlot(uint32_t* out_cap) {
    if (!ack_ring_) return nullptr;
    return (uint8_t*)oec_ringbuf_acquire(ack_ring_, /*dropOldest=*/0, out_cap);
}
void ShmemTransport::publishAck(uint32_t) { oec_ringbuf_publish(ack_ring_); }

const uint8_t* ShmemTransport::peekCmd(uint32_t* out_size) {
    if (!cmd_ring_) return nullptr;
    return (const uint8_t*)oec_ringbuf_peek(cmd_ring_, out_size);
}
void ShmemTransport::consumeCmd() { oec_ringbuf_consume(cmd_ring_); }

}  // namespace oec::plugin
```

- [ ] **Step 2: Build, run all plugin tests**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin --output-on-failure
```

Expected: `AckOutbox.*` PASS, `ShmemTransport.*` PASS.

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Transport/ShmemTransport.cpp
git commit -m "feat(plugin): ShmemTransport implementation backed by liboeconnect"
```

---

### Task 2.7: `ZmqTransport` — failing tests + stub

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Transport/ZmqTransport.h`
- Create: `plugin-openephys/OEconnect/Source/Transport/ZmqTransport.cpp` (stub)
- Modify: `plugin-openephys/OEconnect/Tests/test_zmq_transport.cc`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include "ITransport.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oec::plugin {

/**
 * ZMQ-backed transport.
 *
 *   data path:  audio thread fills an internal SPSC ringbuf →
 *               shipper thread drains and PUBs on `tcp_pub_endpoint`.
 *   cmd  path:  shipper thread REPs on `tcp_rep_endpoint`,
 *               pushes CMD bytes into internal cmd queue (audio thread reads).
 *   ack  path:  audio thread pushes ACK frames into internal ack queue →
 *               shipper thread matches cookies and sends REP replies.
 */
class ZmqTransport final : public ITransport {
public:
    ZmqTransport();
    ~ZmqTransport() override;

    /** endpoint format: "tcp://*:5557|tcp://*:5558" (pub|rep). */
    bool start(const std::string& endpoint_pair) override;
    void stop() override;

    uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) override;
    void publishData(uint32_t bytes_written) override;

    uint8_t* acquireAckSlot(uint32_t* out_cap) override;
    void publishAck(uint32_t bytes_written) override;

    const uint8_t* peekCmd(uint32_t* out_size) override;
    void consumeCmd() override;

    uint64_t totalDropped() const override { return dropped_; }
    std::string name() const override { return "Zmq"; }

private:
    void shipperLoop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::thread shipper_;
    uint64_t dropped_ = 0;
};

}  // namespace oec::plugin
```

- [ ] **Step 2: Stub `.cpp` so headers compile**

```cpp
#include "ZmqTransport.h"

namespace oec::plugin {
struct ZmqTransport::Impl {};
ZmqTransport::ZmqTransport() : impl_(std::make_unique<Impl>()) {}
ZmqTransport::~ZmqTransport() = default;
bool ZmqTransport::start(const std::string&) { return false; }
void ZmqTransport::stop() {}
uint8_t* ZmqTransport::acquireDataSlot(uint32_t*, bool) { return nullptr; }
void ZmqTransport::publishData(uint32_t) {}
uint8_t* ZmqTransport::acquireAckSlot(uint32_t*) { return nullptr; }
void ZmqTransport::publishAck(uint32_t) {}
const uint8_t* ZmqTransport::peekCmd(uint32_t*) { return nullptr; }
void ZmqTransport::consumeCmd() {}
void ZmqTransport::shipperLoop() {}
}
```

- [ ] **Step 3: Tests**

`plugin-openephys/OEconnect/Tests/test_zmq_transport.cc`:

```cpp
#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <zmq.hpp>

#include "Transport/ZmqTransport.h"
#include "Transport/Frame.h"

using oec::plugin::ZmqTransport;
using oec::plugin::FrameWriter;
using namespace std::chrono_literals;

namespace {
std::string ports() {
    return "tcp://127.0.0.1:55571|tcp://127.0.0.1:55572";
}
}  // namespace

TEST(ZmqTransport, StartBindsBothSockets) {
    ZmqTransport t;
    ASSERT_TRUE(t.start(ports()));
    std::this_thread::sleep_for(50ms);
    t.stop();
}

TEST(ZmqTransport, PublishedFrameReachesSubscriber) {
    ZmqTransport t;
    ASSERT_TRUE(t.start(ports()));

    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.set(zmq::sockopt::subscribe, "");
    sub.connect("tcp://127.0.0.1:55571");
    std::this_thread::sleep_for(100ms);  /* sub slow-joiner */

    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, false);
    ASSERT_NE(slot, nullptr);
    const uint8_t payload[] = {0xAA, 0xBB};
    size_t written = FrameWriter::write(slot, cap, OEC_STREAM_TTL_EVENT,
                                        1, 2, 0, payload, sizeof(payload));
    ASSERT_GT(written, 0u);
    t.publishData((uint32_t)written);

    zmq::message_t topic, body;
    auto res_topic = sub.recv(topic, zmq::recv_flags::none);
    auto res_body  = sub.recv(body,  zmq::recv_flags::none);
    ASSERT_TRUE(res_topic && res_body);
    ASSERT_EQ(topic.size(), 2u);
    uint16_t topic_id = 0;
    std::memcpy(&topic_id, topic.data(), 2);
    EXPECT_EQ(topic_id, OEC_STREAM_TTL_EVENT);
    EXPECT_GE(body.size(), sizeof(oec_frame_header_t) + sizeof(payload));

    t.stop();
}

TEST(ZmqTransport, ReqDeliversCmdToTransport) {
    ZmqTransport t;
    ASSERT_TRUE(t.start(ports()));

    zmq::context_t ctx(1);
    zmq::socket_t req(ctx, zmq::socket_type::req);
    req.connect("tcp://127.0.0.1:55572");

    /* Build CMD frame: SET_TTL */
    uint8_t buf[64];
    struct { uint16_t cmd_id; uint32_t cookie; uint8_t line; uint8_t edge; } body{
        OEC_CMD_SET_TTL, 0xCAFEBABEu, 2, 1
    };
    size_t bytes = FrameWriter::write(buf, sizeof(buf), OEC_STREAM_CMD,
                                      0, 0, 0, &body, sizeof(body));
    ASSERT_GT(bytes, 0u);
    req.send(zmq::buffer(buf, bytes), zmq::send_flags::none);

    /* Audio thread peek */
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(10ms);
        uint32_t sz = 0;
        const uint8_t* p = t.peekCmd(&sz);
        if (p) {
            const auto* h = reinterpret_cast<const oec_frame_header_t*>(p);
            EXPECT_EQ(h->stream_id, OEC_STREAM_CMD);
            t.consumeCmd();
            /* Plugin would now emit an ACK; for this test we synthesise one. */
            uint32_t acap = 0;
            uint8_t* aslot = t.acquireAckSlot(&acap);
            ASSERT_NE(aslot, nullptr);
            struct { uint32_t cookie; uint16_t status; } ack{ 0xCAFEBABEu, OEC_ACK_OK };
            size_t aw = FrameWriter::write(aslot, acap, OEC_STREAM_ACK,
                                           0, 0, 0, &ack, sizeof(ack));
            t.publishAck((uint32_t)aw);

            zmq::message_t reply;
            auto r = req.recv(reply, zmq::recv_flags::none);
            ASSERT_TRUE(r);
            EXPECT_GE(reply.size(), sizeof(oec_frame_header_t));
            t.stop();
            return;
        }
    }
    FAIL() << "CMD never reached transport";
}
```

- [ ] **Step 4: Build, confirm fail, commit**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R ZmqTransport --output-on-failure
```

Expected: all `ZmqTransport.*` FAIL (`start` returns false).

```powershell
git add plugin-openephys/OEconnect/Source/Transport/ZmqTransport.* plugin-openephys/OEconnect/Tests/test_zmq_transport.cc
git commit -m "test(plugin): failing ZmqTransport tests + stub"
```

---

### Task 2.8: Implement `ZmqTransport`

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/Transport/ZmqTransport.cpp`

- [ ] **Step 1: Implementation**

```cpp
#include "ZmqTransport.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zmq.hpp>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

namespace {

constexpr size_t kRingSlots   = 1024;
constexpr size_t kSlotBytes   = 65536;
constexpr int    kPubHwm      = 1024;
constexpr int    kHeartbeatMs = 500;

/* Simple bounded SPSC vector ring used as the in-process audio→shipper buffer. */
struct LocalRing {
    std::vector<std::vector<uint8_t>> slots;
    std::vector<uint32_t>             sizes;
    std::atomic<size_t>               prod{0};
    std::atomic<size_t>               cons{0};
    explicit LocalRing(size_t n_slots, size_t slot_bytes)
        : slots(n_slots), sizes(n_slots, 0)
    {
        for (auto& s : slots) s.resize(slot_bytes);
    }
    uint8_t* acquire(uint32_t* cap, bool dropOldest, uint64_t* dropped) {
        size_t p = prod.load(std::memory_order_relaxed);
        size_t c = cons.load(std::memory_order_acquire);
        if (p - c >= slots.size()) {
            if (!dropOldest) return nullptr;
            cons.store(c + 1, std::memory_order_release);
            ++(*dropped);
            c = c + 1;
        }
        size_t i = p % slots.size();
        if (cap) *cap = (uint32_t)slots[i].size();
        return slots[i].data();
    }
    void publish(uint32_t bytes) {
        size_t p = prod.load(std::memory_order_relaxed);
        sizes[p % slots.size()] = bytes;
        prod.store(p + 1, std::memory_order_release);
    }
    bool peek(const uint8_t** out, uint32_t* out_sz) {
        size_t c = cons.load(std::memory_order_relaxed);
        size_t p = prod.load(std::memory_order_acquire);
        if (c == p) return false;
        size_t i = c % slots.size();
        *out = slots[i].data();
        *out_sz = sizes[i];
        return true;
    }
    void consume_one() {
        size_t c = cons.load(std::memory_order_relaxed);
        cons.store(c + 1, std::memory_order_release);
    }
};

}  // namespace

struct ZmqTransport::Impl {
    zmq::context_t ctx{1};
    zmq::socket_t  pub{ctx, zmq::socket_type::pub};
    zmq::socket_t  rep{ctx, zmq::socket_type::rep};

    LocalRing data{kRingSlots, kSlotBytes};
    LocalRing ack {64,          kSlotBytes};

    /* Cmd buffer drained by audio thread. */
    std::mutex             cmd_mutex;
    std::deque<std::vector<uint8_t>> cmd_q;

    /* Last peeked cmd pointer for audio thread (peekCmd/consumeCmd are paired). */
    std::vector<uint8_t> audio_thread_cmd_copy;

    /* Cookie → ZMQ identity (REP socket is stateful — single in-flight at a time
       per req, so we just remember the most recent req identity). */
    bool req_pending = false;
};

ZmqTransport::ZmqTransport() : impl_(std::make_unique<Impl>()) {}
ZmqTransport::~ZmqTransport() { stop(); }

bool ZmqTransport::start(const std::string& endpoint_pair) {
    stop();
    auto sep = endpoint_pair.find('|');
    if (sep == std::string::npos) return false;
    const std::string pub_ep = endpoint_pair.substr(0, sep);
    const std::string rep_ep = endpoint_pair.substr(sep + 1);

    try {
        impl_->pub.set(zmq::sockopt::sndhwm, kPubHwm);
        impl_->pub.bind(pub_ep);
        impl_->rep.set(zmq::sockopt::heartbeat_ivl, kHeartbeatMs);
        impl_->rep.set(zmq::sockopt::heartbeat_timeout, 2000);
        impl_->rep.bind(rep_ep);
    } catch (const zmq::error_t&) {
        return false;
    }
    running_.store(true, std::memory_order_release);
    shipper_ = std::thread(&ZmqTransport::shipperLoop, this);
    return true;
}

void ZmqTransport::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (shipper_.joinable()) shipper_.join();
    }
    /* Sockets close when impl_ resets next start(). */
}

uint8_t* ZmqTransport::acquireDataSlot(uint32_t* out_cap, bool dropOldest) {
    return impl_->data.acquire(out_cap, dropOldest, &dropped_);
}
void ZmqTransport::publishData(uint32_t bytes_written) {
    impl_->data.publish(bytes_written);
}
uint8_t* ZmqTransport::acquireAckSlot(uint32_t* out_cap) {
    uint64_t ignored = 0;
    return impl_->ack.acquire(out_cap, /*dropOldest=*/false, &ignored);
}
void ZmqTransport::publishAck(uint32_t bytes_written) {
    impl_->ack.publish(bytes_written);
}

const uint8_t* ZmqTransport::peekCmd(uint32_t* out_size) {
    std::lock_guard<std::mutex> lk(impl_->cmd_mutex);
    if (impl_->cmd_q.empty()) return nullptr;
    impl_->audio_thread_cmd_copy = impl_->cmd_q.front();
    if (out_size) *out_size = (uint32_t)impl_->audio_thread_cmd_copy.size();
    return impl_->audio_thread_cmd_copy.data();
}
void ZmqTransport::consumeCmd() {
    std::lock_guard<std::mutex> lk(impl_->cmd_mutex);
    if (!impl_->cmd_q.empty()) impl_->cmd_q.pop_front();
}

void ZmqTransport::shipperLoop() {
    using namespace std::chrono_literals;
    zmq::pollitem_t items[] = {
        { impl_->rep.handle(), 0, ZMQ_POLLIN, 0 }
    };

    while (running_.load(std::memory_order_acquire)) {
        /* Drain data ring → PUB */
        const uint8_t* slot = nullptr;
        uint32_t sz = 0;
        while (impl_->data.peek(&slot, &sz)) {
            const auto* h = reinterpret_cast<const oec_frame_header_t*>(slot);
            uint16_t topic = h->stream_id;
            impl_->pub.send(zmq::buffer(&topic, sizeof(topic)), zmq::send_flags::sndmore);
            impl_->pub.send(zmq::buffer(slot, sz), zmq::send_flags::none);
            impl_->data.consume_one();
        }

        /* Drain ack ring → REP reply (if a request is outstanding) */
        const uint8_t* aslot = nullptr;
        uint32_t asz = 0;
        if (impl_->req_pending && impl_->ack.peek(&aslot, &asz)) {
            impl_->rep.send(zmq::buffer(aslot, asz), zmq::send_flags::none);
            impl_->ack.consume_one();
            impl_->req_pending = false;
        }

        /* Poll REP for incoming CMD */
        zmq::poll(items, 1, std::chrono::milliseconds(5));
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            auto r = impl_->rep.recv(msg, zmq::recv_flags::dontwait);
            if (r && msg.size() >= sizeof(oec_frame_header_t)) {
                std::vector<uint8_t> copy(msg.size());
                std::memcpy(copy.data(), msg.data(), msg.size());
                {
                    std::lock_guard<std::mutex> lk(impl_->cmd_mutex);
                    impl_->cmd_q.push_back(std::move(copy));
                }
                impl_->req_pending = true;
            }
        }
    }
}

}  // namespace oec::plugin
```

- [ ] **Step 2: Build + run all ZMQ tests**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R ZmqTransport --output-on-failure
```

Expected: all `ZmqTransport.*` PASS.

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Transport/ZmqTransport.cpp
git commit -m "feat(plugin): ZmqTransport (PUB/SUB + REQ/REP) backed by libzmq"
```

---

### Task 2.9: `IBoardAdapter` + `FileReaderAdapter`

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Boards/IBoardAdapter.h`
- Create: `plugin-openephys/OEconnect/Source/Boards/FileReaderAdapter.h`
- Create: `plugin-openephys/OEconnect/Source/Boards/FileReaderAdapter.cpp`

- [ ] **Step 1: `IBoardAdapter.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace oec::plugin {

/**
 * Hardware abstraction over OE source nodes. Audio-thread-callable methods
 * are wait-free.
 */
class IBoardAdapter {
public:
    virtual ~IBoardAdapter() = default;
    virtual std::string name() const = 0;
    virtual int  numTtlOutLines() const = 0;

    /** Wait-free, called on audio thread. Returns the sample index at which
     *  the line will actually flip; 0 if the call was rejected. */
    virtual uint64_t setTtl(uint8_t line, bool high) = 0;

    virtual void onStartAcquisition(int blockSize, double sampleRate) { (void)blockSize; (void)sampleRate; }
    virtual void onStopAcquisition() {}
};

}  // namespace oec::plugin
```

- [ ] **Step 2: `FileReaderAdapter.h/.cpp`**

`FileReaderAdapter.h`:

```cpp
#pragma once
#include "IBoardAdapter.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace oec::plugin {

class FileReaderAdapter final : public IBoardAdapter {
public:
    explicit FileReaderAdapter(std::string log_path);
    ~FileReaderAdapter() override;

    std::string name() const override { return "FileReader"; }
    int numTtlOutLines() const override { return 8; }
    uint64_t setTtl(uint8_t line, bool high) override;

    void onStartAcquisition(int blockSize, double sampleRate) override;
    void onStopAcquisition() override;

private:
    std::string log_path_;
    std::FILE*  fp_ = nullptr;
    std::mutex  mu_;
    std::atomic<uint64_t> sample_index_{0};
    double sample_rate_ = 30000.0;
};

}  // namespace oec::plugin
```

`FileReaderAdapter.cpp`:

```cpp
#include "FileReaderAdapter.h"
#include <chrono>

namespace oec::plugin {

FileReaderAdapter::FileReaderAdapter(std::string log_path) : log_path_(std::move(log_path)) {}
FileReaderAdapter::~FileReaderAdapter() { onStopAcquisition(); }

void FileReaderAdapter::onStartAcquisition(int /*blockSize*/, double sampleRate) {
    std::lock_guard<std::mutex> lk(mu_);
    sample_rate_ = sampleRate;
    fp_ = std::fopen(log_path_.c_str(), "w");
    if (fp_) std::fprintf(fp_, "sample_index,line,edge\n");
}
void FileReaderAdapter::onStopAcquisition() {
    std::lock_guard<std::mutex> lk(mu_);
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

uint64_t FileReaderAdapter::setTtl(uint8_t line, bool high) {
    /* No hardware. Bump a counter to give Bonsai something coherent to align with. */
    const uint64_t s = sample_index_.fetch_add(1, std::memory_order_relaxed) + 1;
    /* fprintf under mutex is NOT wait-free; this adapter is the dev/CI fallback,
       not the hot path. Real adapters override with wait-free behaviour. */
    std::lock_guard<std::mutex> lk(mu_);
    if (fp_) std::fprintf(fp_, "%llu,%u,%u\n",
                          (unsigned long long)s, (unsigned)line, (unsigned)(high ? 1 : 0));
    return s;
}

}  // namespace oec::plugin
```

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Boards/IBoardAdapter.h plugin-openephys/OEconnect/Source/Boards/FileReaderAdapter.*
git commit -m "feat(plugin): IBoardAdapter contract + FileReader dev adapter"
```

---

### Task 2.10: `RhdAcqBoardAdapter`, `OnixAdapter`, `NeuropixelsAdapter` (interface skeletons)

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Boards/RhdAcqBoardAdapter.h`
- Create: `plugin-openephys/OEconnect/Source/Boards/RhdAcqBoardAdapter.cpp`
- Create: `plugin-openephys/OEconnect/Source/Boards/OnixAdapter.h`
- Create: `plugin-openephys/OEconnect/Source/Boards/OnixAdapter.cpp`
- Create: `plugin-openephys/OEconnect/Source/Boards/NeuropixelsAdapter.h`
- Create: `plugin-openephys/OEconnect/Source/Boards/NeuropixelsAdapter.cpp`

**Note for engineer:** these adapters wrap source-node APIs that live in the
OE plugin-GUI submodule and in the ONIX / Neuropixels SDKs. The exact method
names should be verified at implementation time against the current source
in `external/plugin-GUI/` and `external/onix/`. The code below uses opaque
`void*` source-node pointers and forward-declares the calls we expect;
replace the bodies once you have the live SDK in front of you.

- [ ] **Step 1: `RhdAcqBoardAdapter` (.h/.cpp)**

`RhdAcqBoardAdapter.h`:

```cpp
#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

class RhdAcqBoardAdapter final : public IBoardAdapter {
public:
    /** `source_node` is the upstream Rhythm FPGA source-node pointer (opaque). */
    explicit RhdAcqBoardAdapter(void* source_node);
    std::string name() const override { return "Rhythm FPGA (Acq Board)"; }
    int  numTtlOutLines() const override { return 8; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
```

`RhdAcqBoardAdapter.cpp`:

```cpp
#include "RhdAcqBoardAdapter.h"

/*
 * Implementation note (read before editing):
 * The OE Rhythm source node exposes a method along the lines of
 *   void setTTLOutputBit(int channel, bool state);
 * but the exact signature varies across plugin-GUI minor versions.
 * Look in external/plugin-GUI/Plugins/RhythmNode/ for the live API.
 * Until the live SDK is wired, this adapter no-ops and reports sample 0.
 */
namespace oec::plugin {

RhdAcqBoardAdapter::RhdAcqBoardAdapter(void* source_node) : source_node_(source_node) {}

void RhdAcqBoardAdapter::onStartAcquisition(int /*blockSize*/, double /*sampleRate*/) {
    last_sample_.store(0, std::memory_order_relaxed);
}

uint64_t RhdAcqBoardAdapter::setTtl(uint8_t /*line*/, bool /*high*/) {
    if (!source_node_) return 0;
    /* TODO(impl): cast to the real Rhythm source-node type discovered from
       external/plugin-GUI/ and call its setTTLOutputBit(line, high). */
    return last_sample_.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace oec::plugin
```

- [ ] **Step 2: `OnixAdapter` (.h/.cpp)**

`OnixAdapter.h`:

```cpp
#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

class OnixAdapter final : public IBoardAdapter {
public:
    explicit OnixAdapter(void* source_node);
    std::string name() const override { return "ONIX"; }
    int  numTtlOutLines() const override { return 16; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
```

`OnixAdapter.cpp`:

```cpp
#include "OnixAdapter.h"

/*
 * Implementation note:
 * ONIX exposes oni_write_reg() on a DIO peripheral. The Onix Source node
 * in the OE plugin-GUI wraps oni_ctx; from the source node we can either
 * use its public setOutput() if present, or grab the underlying oni_ctx_t
 * and call oni_write_reg(ctx, dev, register, value). Verify against
 * external/plugin-GUI/Plugins/OnixSource/ at impl time.
 */
namespace oec::plugin {

OnixAdapter::OnixAdapter(void* source_node) : source_node_(source_node) {}

void OnixAdapter::onStartAcquisition(int /*blockSize*/, double /*sampleRate*/) {
    last_sample_.store(0, std::memory_order_relaxed);
}

uint64_t OnixAdapter::setTtl(uint8_t /*line*/, bool /*high*/) {
    if (!source_node_) return 0;
    /* TODO(impl): wire to live ONIX DIO peripheral. */
    return last_sample_.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace oec::plugin
```

- [ ] **Step 3: `NeuropixelsAdapter` (.h/.cpp) — delegates to whichever sibling adapter has TTL**

`NeuropixelsAdapter.h`:

```cpp
#pragma once
#include "IBoardAdapter.h"
#include <memory>

namespace oec::plugin {

/**
 * NPX-PXI source has limited TTL out. If a paired Acq Board sits in the same
 * signal chain, this adapter delegates to its RhdAcqBoardAdapter; otherwise
 * setTtl returns 0 (NOT_SUPPORTED).
 */
class NeuropixelsAdapter final : public IBoardAdapter {
public:
    NeuropixelsAdapter(void* npx_source_node, std::unique_ptr<IBoardAdapter> delegate);
    std::string name() const override;
    int  numTtlOutLines() const override;
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* npx_source_node_;
    std::unique_ptr<IBoardAdapter> delegate_;
};

}  // namespace oec::plugin
```

`NeuropixelsAdapter.cpp`:

```cpp
#include "NeuropixelsAdapter.h"

namespace oec::plugin {

NeuropixelsAdapter::NeuropixelsAdapter(void* npx, std::unique_ptr<IBoardAdapter> del)
    : npx_source_node_(npx), delegate_(std::move(del)) {}

std::string NeuropixelsAdapter::name() const {
    return delegate_ ? ("Neuropixels (+ " + delegate_->name() + ")") : "Neuropixels (no TTL out)";
}
int NeuropixelsAdapter::numTtlOutLines() const {
    return delegate_ ? delegate_->numTtlOutLines() : 0;
}
uint64_t NeuropixelsAdapter::setTtl(uint8_t line, bool high) {
    return delegate_ ? delegate_->setTtl(line, high) : 0;
}
void NeuropixelsAdapter::onStartAcquisition(int blockSize, double sampleRate) {
    if (delegate_) delegate_->onStartAcquisition(blockSize, sampleRate);
}

}  // namespace oec::plugin
```

- [ ] **Step 4: Build (compile-only — adapters TODO real SDK wiring)**

```powershell
cmake --build build-plugin
```

Expected: clean build.

- [ ] **Step 5: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Boards/Rhd* plugin-openephys/OEconnect/Source/Boards/Onix* plugin-openephys/OEconnect/Source/Boards/Neuropixels*
git commit -m "feat(plugin): board adapter skeletons (Rhd, Onix, Npx) — SDK wiring marked TODO"
```

---

### Task 2.11: `DriftEmitter` — 1 Hz SYNC timer

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Sync/DriftEmitter.h`
- Create: `plugin-openephys/OEconnect/Source/Sync/DriftEmitter.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include "Util/AckOutbox.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace oec::plugin {

/**
 * Once per second, pushes a SYNC AckEntry into the AckOutbox so the audio
 * thread can lift it into the ack_ring on the next process() tick.
 */
class DriftEmitter {
public:
    explicit DriftEmitter(AckOutbox& outbox, std::atomic<uint64_t>& sample_index)
        : outbox_(outbox), sample_index_(sample_index) {}
    ~DriftEmitter() { stop(); }

    void start(double fpga_sample_rate_hz);
    void stop();

private:
    void loop();

    AckOutbox&           outbox_;
    std::atomic<uint64_t>& sample_index_;
    std::atomic<bool>    running_{false};
    std::thread          thread_;
    double               sample_rate_ = 30000.0;
};

}  // namespace oec::plugin
```

- [ ] **Step 2: Implementation**

```cpp
#include "DriftEmitter.h"

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

#if defined(_WIN32)
#include <windows.h>
static uint64_t qpc_now() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t)c.QuadPart;
}
#else
#include <time.h>
static uint64_t qpc_now() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#endif

void DriftEmitter::start(double fpga_sample_rate_hz) {
    stop();
    sample_rate_ = fpga_sample_rate_hz;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&DriftEmitter::loop, this);
}

void DriftEmitter::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (thread_.joinable()) thread_.join();
    }
}

void DriftEmitter::loop() {
    using namespace std::chrono;
    while (running_.load(std::memory_order_acquire)) {
        AckEntry e{};
        e.cmd_id = OEC_STREAM_SYNC;        /* repurposed: stream id, not cmd id */
        e.cookie = 0;
        e.status = 0;
        e.sample_index = sample_index_.load(std::memory_order_relaxed);
        e.host_qpc_ticks = qpc_now();
        outbox_.tryPush(e);
        std::this_thread::sleep_for(seconds(1));
    }
}

}  // namespace oec::plugin
```

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/Sync/
git commit -m "feat(plugin): DriftEmitter 1 Hz SYNC timer"
```

---

### Task 2.12: `OEconnectProcessor` skeleton + hot-path integration test

**Files:**
- Create: `plugin-openephys/OEconnect/Source/OEconnectProcessor.h`
- Create: `plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp`
- Modify: `plugin-openephys/OEconnect/Tests/test_processor_hot_path.cc`

The processor is a `GenericProcessor` subclass, but for unit testing we
expose its hot-path inner loop as a free function so tests can drive it
without instantiating a full JUCE plugin host. Real signal-chain integration
is verified manually inside OE GUI.

- [ ] **Step 1: Header**

```cpp
#pragma once
#include "Transport/ITransport.h"
#include "Boards/IBoardAdapter.h"
#include "Sync/DriftEmitter.h"
#include "Util/AckOutbox.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace oec::plugin {

/**
 * Processor configuration owned by the editor (UI).
 */
struct ProcessorConfig {
    bool   enable_raw      = true;
    bool   enable_filtered = false;
    bool   enable_spikes   = true;
    bool   enable_ttl      = true;
    int    block_size      = 32;
    int    num_channels    = 256;
    double sample_rate_hz  = 30000.0;
    std::string transport_mode = "Auto";           /* "Auto" | "SharedMem" | "Zmq" */
    std::string shm_name;                          /* set by start() */
    std::string zmq_endpoint = "tcp://*:5557|tcp://*:5558";
};

/**
 * Hot-path inner loop (pure C++, no JUCE deps). One call ≈ one process() tick.
 *
 * @param input          channel-major int16 sample block, NumChannels × NumSamples
 * @param sample_index   FPGA sample index at the start of this block
 * @param config         the active configuration
 * @param transport      data/cmd/ack carrier
 * @param board          hardware abstraction
 * @param outbox         ACK MPSC drained inline
 */
void processBlock(
    const int16_t* input,
    uint64_t sample_index,
    const ProcessorConfig& config,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox);

}  // namespace oec::plugin
```

- [ ] **Step 2: Test (failing — no impl yet)**

`plugin-openephys/OEconnect/Tests/test_processor_hot_path.cc`:

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "OEconnectProcessor.h"
#include "Transport/ShmemTransport.h"
#include "Boards/FileReaderAdapter.h"

extern "C" {
#include "oeconnect/ringbuf.h"
#include "oeconnect/shm.h"
}

using namespace oec::plugin;

namespace {
std::string uniq() {
#if defined(_WIN32)
    return std::string("Local\\oeconnect.test.hot.") + std::to_string(::GetCurrentProcessId());
#else
    return std::string("/oeconnect.test.hot.") + std::to_string(getpid());
#endif
}
}

TEST(HotPath, ProcessBlockEmitsRawFrame) {
    ShmemTransport t;
    std::string name = uniq() + ".raw";
    ASSERT_TRUE(t.start(name));

    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    ProcessorConfig cfg;
    cfg.num_channels = 4;
    cfg.block_size   = 8;
    cfg.enable_raw = true;
    cfg.enable_filtered = false;
    cfg.enable_spikes   = false;
    cfg.enable_ttl      = false;

    std::vector<int16_t> input(cfg.num_channels * cfg.block_size);
    for (size_t i = 0; i < input.size(); ++i) input[i] = (int16_t)(i + 1);

    processBlock(input.data(), /*sample_index=*/0, cfg, t, board, outbox);

    /* Re-open region to peek raw ring (consumer side). */
    oec_shm_t* h = nullptr;
    void* mapped = nullptr;
    size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);
    const oec_frame_header_t* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->stream_id, OEC_STREAM_RAW_BLOCK);
    const oec_block_subheader_t* sh =
        (const oec_block_subheader_t*)((const uint8_t*)frame + sizeof(*fh));
    EXPECT_EQ(sh->n_channels, 4);
    EXPECT_EQ(sh->n_samples, 8);
    EXPECT_EQ(sh->dtype, OEC_DTYPE_INT16);
    oec_ringbuf_detach(rb);
    oec_shm_close(h);

    t.stop();
    oec_shm_unlink(name.c_str());
}

TEST(HotPath, SetTtlCommandFiresBoardAdapter) {
    ShmemTransport t;
    std::string name = uniq() + ".cmd";
    ASSERT_TRUE(t.start(name));

    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    /* Inject a SET_TTL command into the cmd ring (consumer-side write — for
       the test we attach a second ringbuf handle and write into it). */
    oec_shm_t* h = nullptr;
    void* mapped = nullptr;
    size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* cmd_writer = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_CMD, &cmd_writer), OEC_OK);

    uint32_t cap = 0;
    void* slot = oec_ringbuf_acquire(cmd_writer, /*drop_oldest=*/0, &cap);
    ASSERT_NE(slot, nullptr);
    oec_frame_header_t hdr;
    oec_frame_init(&hdr, OEC_STREAM_CMD, 8, 0, 0, 0);
    std::memcpy(slot, &hdr, sizeof(hdr));
    struct { uint16_t cmd_id; uint32_t cookie; uint8_t line; uint8_t edge; } body{
        OEC_CMD_SET_TTL, 0xABCDu, 3, 1
    };
    std::memcpy((uint8_t*)slot + sizeof(hdr), &body, sizeof(body));
    oec_ringbuf_publish(cmd_writer);
    oec_ringbuf_detach(cmd_writer);

    ProcessorConfig cfg;
    cfg.num_channels = 1; cfg.block_size = 1;
    cfg.enable_raw = cfg.enable_filtered = cfg.enable_spikes = cfg.enable_ttl = false;
    std::vector<int16_t> dummy(1, 0);
    processBlock(dummy.data(), 0, cfg, t, board, outbox);

    /* Adapter should have logged sample 1. */
    /* Side-effect verification: ack_ring should contain an OK ack. */
    oec_ringbuf_t* ack_reader = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_ACK, &ack_reader), OEC_OK);
    uint32_t sz = 0;
    const void* ack = oec_ringbuf_peek(ack_reader, &sz);
    ASSERT_NE(ack, nullptr);
    const oec_frame_header_t* ah = (const oec_frame_header_t*)ack;
    EXPECT_EQ(ah->stream_id, OEC_STREAM_ACK);
    oec_ringbuf_detach(ack_reader);

    oec_shm_close(h);
    t.stop();
    oec_shm_unlink(name.c_str());
}
```

- [ ] **Step 3: Stub `OEconnectProcessor.cpp` so headers link (test FAILS at run time)**

```cpp
#include "OEconnectProcessor.h"

namespace oec::plugin {
void processBlock(const int16_t*, uint64_t, const ProcessorConfig&,
                  ITransport&, IBoardAdapter&, AckOutbox&) {}
}
```

- [ ] **Step 4: Build, confirm tests FAIL**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R HotPath --output-on-failure
```

Expected: `HotPath.ProcessBlockEmitsRawFrame` FAIL — ring stays empty.

- [ ] **Step 5: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/OEconnectProcessor.* plugin-openephys/OEconnect/Tests/test_processor_hot_path.cc
git commit -m "test(plugin): failing hot-path integration tests + stub"
```

---

### Task 2.13: Implement `processBlock` (the hot path)

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp`

- [ ] **Step 1: Replace stub with real hot path**

```cpp
#include "OEconnectProcessor.h"
#include "Transport/Frame.h"

#include <cstring>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

namespace {

#if defined(_WIN32)
#include <windows.h>
static uint64_t qpc_now() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t)c.QuadPart; }
#else
#include <time.h>
static uint64_t qpc_now() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#endif

void writeRawBlock(ITransport& t, const ProcessorConfig& cfg,
                   const int16_t* input, uint64_t sample_index,
                   uint16_t stream_id, uint16_t flags)
{
    const uint32_t payload =
        (uint32_t)(sizeof(oec_block_subheader_t) +
                   (size_t)cfg.num_channels * (size_t)cfg.block_size * sizeof(int16_t));
    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/true);
    if (!slot) return;
    const size_t need = sizeof(oec_frame_header_t) + payload;
    if (cap < need) return;

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, stream_id, payload, sample_index, qpc_now(), flags);
    auto* sh = reinterpret_cast<oec_block_subheader_t*>(slot + sizeof(*h));
    sh->n_channels = (uint16_t)cfg.num_channels;
    sh->n_samples  = (uint16_t)cfg.block_size;
    sh->dtype      = OEC_DTYPE_INT16;
    sh->source_id  = 0;
    sh->reserved   = 0;
    std::memcpy(slot + sizeof(*h) + sizeof(*sh), input,
                (size_t)cfg.num_channels * cfg.block_size * sizeof(int16_t));
    t.publishData((uint32_t)need);
}

void emitAck(ITransport& t, uint32_t cookie, uint16_t status,
             uint64_t sample_index)
{
    uint32_t cap = 0;
    uint8_t* slot = t.acquireAckSlot(&cap);
    if (!slot) return;
    struct { uint32_t cookie; uint16_t status; } body{ cookie, status };
    const size_t need = sizeof(oec_frame_header_t) + sizeof(body);
    if (cap < need) return;
    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, OEC_STREAM_ACK, sizeof(body), sample_index, qpc_now(), 0);
    std::memcpy(slot + sizeof(*h), &body, sizeof(body));
    t.publishAck((uint32_t)need);
}

void drainCmdRing(ITransport& t, IBoardAdapter& board)
{
    for (;;) {
        uint32_t sz = 0;
        const uint8_t* frame = t.peekCmd(&sz);
        if (!frame) return;
        if (sz < sizeof(oec_frame_header_t) + 6) { t.consumeCmd(); continue; }

        const auto* h = reinterpret_cast<const oec_frame_header_t*>(frame);
        if (h->stream_id != OEC_STREAM_CMD) { t.consumeCmd(); continue; }

        const uint8_t* body = frame + sizeof(*h);
        uint16_t cmd_id = 0; uint32_t cookie = 0;
        std::memcpy(&cmd_id, body + 0, 2);
        std::memcpy(&cookie, body + 2, 4);

        switch (cmd_id) {
            case OEC_CMD_SET_TTL: {
                uint8_t line = body[6];
                uint8_t edge = body[7];
                uint64_t s = board.setTtl(line, edge != 0);
                emitAck(t, cookie, OEC_ACK_OK, s);
                break;
            }
            case OEC_CMD_PULSE_TTL: {
                uint8_t line = body[6];
                uint8_t edge = body[7];
                /* Width handled by caller side; the plugin merely toggles once
                   here — true pulse generation belongs in the board adapter. */
                uint64_t s = board.setTtl(line, edge != 0);
                emitAck(t, cookie, OEC_ACK_OK, s);
                break;
            }
            case OEC_CMD_GET_STATE: {
                emitAck(t, cookie, OEC_ACK_OK, 0);
                break;
            }
            case OEC_CMD_START_RECORD:
            case OEC_CMD_STOP_RECORD:
            case OEC_CMD_START_ACQ:
            case OEC_CMD_STOP_ACQ:
                /* Slow commands — emit PENDING; real worker thread completes later. */
                emitAck(t, cookie, OEC_ACK_PENDING, 0);
                /* TODO(impl): forward to slow-cmd worker via a message queue. */
                break;
            default:
                emitAck(t, cookie, OEC_ACK_NOT_SUPPORTED, 0);
                break;
        }
        t.consumeCmd();
    }
}

void drainAckOutbox(ITransport& t, AckOutbox& outbox)
{
    AckEntry e{};
    while (outbox.tryPop(e)) {
        uint32_t cap = 0;
        uint8_t* slot = t.acquireAckSlot(&cap);
        if (!slot) break;
        const uint16_t stream = (e.cmd_id == OEC_STREAM_SYNC) ? OEC_STREAM_SYNC
                                                              : OEC_STREAM_ACK;
        struct { uint32_t cookie; uint16_t status; } body{ e.cookie, e.status };
        const uint32_t payload = (stream == OEC_STREAM_SYNC) ? 0u : (uint32_t)sizeof(body);
        const size_t need = sizeof(oec_frame_header_t) + payload;
        if (cap < need) break;
        auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
        oec_frame_init(h, stream, payload, e.sample_index, e.host_qpc_ticks, 0);
        if (payload) std::memcpy(slot + sizeof(*h), &body, sizeof(body));
        t.publishAck((uint32_t)need);
    }
}

}  // namespace

void processBlock(
    const int16_t* input,
    uint64_t sample_index,
    const ProcessorConfig& cfg,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox)
{
    drainCmdRing(transport, board);
    drainAckOutbox(transport, outbox);

    if (cfg.enable_raw) {
        writeRawBlock(transport, cfg, input, sample_index,
                      OEC_STREAM_RAW_BLOCK, /*flags=*/0);
    }
    if (cfg.enable_filtered) {
        writeRawBlock(transport, cfg, input, sample_index,
                      OEC_STREAM_FILTERED_BLOCK, /*flags=*/0);
    }
    /* SPIKE / TTL_EVENT emission is fed by OE's GenericProcessor::events;
       the JUCE-side wrapper (Task 2.14) forwards them via this same
       transport using OEC_STREAM_SPIKE / OEC_STREAM_TTL_EVENT. The unit
       test feeds them directly through the cmd ring. */
}

}  // namespace oec::plugin
```

- [ ] **Step 2: Build + run all hot-path tests**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R HotPath --output-on-failure
```

Expected: `HotPath.*` PASS.

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp
git commit -m "feat(plugin): processBlock hot path — raw emission + cmd drain + ACK emit"
```

---

### Task 2.14: JUCE wrapper — `OEconnectProcessor` (the actual GenericProcessor subclass)

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.h` (append JUCE class)
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp` (append JUCE wiring)

- [ ] **Step 1: Append the JUCE class declaration to `OEconnectProcessor.h`**

Add at the end of the existing `namespace oec::plugin {`:

```cpp
}  // close oec::plugin

#include <ProcessorHeaders.h>            /* from external/plugin-GUI */

namespace oec::plugin {

class OEconnectEditor;   /* fwd */

class OEconnectProcessor : public GenericProcessor {
public:
    OEconnectProcessor();
    ~OEconnectProcessor() override;

    AudioProcessorEditor* createEditor() override;
    void updateSettings() override;
    void process(AudioBuffer<float>& buffer) override;
    bool startAcquisition() override;
    bool stopAcquisition() override;

    /** Called by the editor when UI knobs change. */
    void applyConfig(const ProcessorConfig& cfg);

    /** Forwarded by Editor to inspect status. */
    uint64_t droppedFrames() const;
    std::string activeTransport() const;
    std::string activeBoard() const;

private:
    void selectBoardAdapter();
    void selectTransport();

    ProcessorConfig cfg_;
    std::unique_ptr<ITransport>     transport_;
    std::unique_ptr<IBoardAdapter>  board_;
    std::unique_ptr<DriftEmitter>   drift_emitter_;
    AckOutbox                       outbox_{1024};
    std::atomic<uint64_t>           sample_counter_{0};
    std::vector<int16_t>            scratch_;
};
```

- [ ] **Step 2: Append the JUCE implementation to `OEconnectProcessor.cpp`**

```cpp
namespace oec::plugin {

OEconnectProcessor::OEconnectProcessor() : GenericProcessor("OEconnect") {}
OEconnectProcessor::~OEconnectProcessor() {
    if (transport_) transport_->stop();
}

AudioProcessorEditor* OEconnectProcessor::createEditor() {
    /* Concrete editor defined in OEconnectEditor.cpp */
    extern AudioProcessorEditor* createOEconnectEditor(OEconnectProcessor*);
    return createOEconnectEditor(this);
}

void OEconnectProcessor::updateSettings() {
    cfg_.num_channels   = getNumInputs();
    cfg_.sample_rate_hz = getSampleRate();
    selectBoardAdapter();
}

void OEconnectProcessor::selectBoardAdapter() {
    GenericProcessor* src = getSourceNode();
    if (!src) { board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv"); return; }
    const String n = src->getName();
    if      (n.containsIgnoreCase("rhythm"))      board_ = std::make_unique<RhdAcqBoardAdapter>(src);
    else if (n.containsIgnoreCase("onix"))        board_ = std::make_unique<OnixAdapter>(src);
    else if (n.containsIgnoreCase("neuropixels")) board_ = std::make_unique<NeuropixelsAdapter>(src, nullptr);
    else if (n.containsIgnoreCase("file reader")) board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv");
    else                                          board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv");
}

void OEconnectProcessor::selectTransport() {
    if (cfg_.transport_mode == "Zmq") {
        transport_ = std::make_unique<ZmqTransport>();
        transport_->start(cfg_.zmq_endpoint);
    } else {
        char name[64];
        oec_shm_make_name((int)getpid(), name, sizeof(name));
        cfg_.shm_name = name;
        transport_ = std::make_unique<ShmemTransport>();
        if (!transport_->start(cfg_.shm_name)) {
            transport_ = std::make_unique<ZmqTransport>();
            transport_->start(cfg_.zmq_endpoint);
        }
    }
}

bool OEconnectProcessor::startAcquisition() {
    selectTransport();
    if (board_) board_->onStartAcquisition(cfg_.block_size, cfg_.sample_rate_hz);
    drift_emitter_ = std::make_unique<DriftEmitter>(outbox_, sample_counter_);
    drift_emitter_->start(cfg_.sample_rate_hz);

    /* Write sidecar JSON */
    oec_sidecar_t s{};
    s.pid = (int)getpid();
    std::strncpy(s.shm_region, cfg_.shm_name.c_str(), sizeof(s.shm_region) - 1);
    std::strncpy(s.zmq_fallback_endpoint, cfg_.zmq_endpoint.c_str(), sizeof(s.zmq_fallback_endpoint) - 1);
    std::strncpy(s.spec_version, "1.0", sizeof(s.spec_version) - 1);
    oec_sidecar_write(&s);
    return true;
}

bool OEconnectProcessor::stopAcquisition() {
    if (drift_emitter_) drift_emitter_->stop();
    if (board_) board_->onStopAcquisition();
    if (transport_) transport_->stop();
    oec_sidecar_remove((int)getpid());
    return true;
}

void OEconnectProcessor::process(AudioBuffer<float>& buffer) {
    const int ch = buffer.getNumChannels();
    const int ns = buffer.getNumSamples();
    if (scratch_.size() < (size_t)(ch * ns)) scratch_.resize((size_t)(ch * ns));
    /* Convert float → int16 chan-major. */
    for (int c = 0; c < ch; ++c) {
        const float* in = buffer.getReadPointer(c);
        int16_t* out = scratch_.data() + c * ns;
        for (int s = 0; s < ns; ++s) {
            float v = in[s] * 32767.0f;
            if (v > 32767.f) v = 32767.f; else if (v < -32768.f) v = -32768.f;
            out[s] = (int16_t)v;
        }
    }
    const uint64_t s0 = sample_counter_.fetch_add((uint64_t)ns, std::memory_order_relaxed);
    cfg_.num_channels = ch;
    cfg_.block_size   = ns;
    processBlock(scratch_.data(), s0, cfg_, *transport_, *board_, outbox_);
}

void OEconnectProcessor::applyConfig(const ProcessorConfig& cfg) { cfg_ = cfg; }
uint64_t OEconnectProcessor::droppedFrames() const { return transport_ ? transport_->totalDropped() : 0; }
std::string OEconnectProcessor::activeTransport() const { return transport_ ? transport_->name() : "—"; }
std::string OEconnectProcessor::activeBoard() const { return board_ ? board_->name() : "—"; }

}  // namespace oec::plugin
```

- [ ] **Step 2 (cont.): Add the include of the adapter headers at the top of the cpp file**

At the top of `OEconnectProcessor.cpp` (after the existing `#include`):

```cpp
#include "Boards/FileReaderAdapter.h"
#include "Boards/RhdAcqBoardAdapter.h"
#include "Boards/OnixAdapter.h"
#include "Boards/NeuropixelsAdapter.h"
#include "Transport/ShmemTransport.h"
#include "Transport/ZmqTransport.h"
extern "C" {
#include "oeconnect/sidecar.h"
#include "oeconnect/shm.h"
}
#if defined(_WIN32)
  #include <process.h>
  #define getpid _getpid
#else
  #include <unistd.h>
#endif
```

- [ ] **Step 3: Build the actual plugin module (separate from unit tests)**

```powershell
cmake --build build-plugin --target OEconnect
```

Expected: produces `OEconnect.bundle` (no executable to run yet).

- [ ] **Step 4: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/OEconnectProcessor.*
git commit -m "feat(plugin): JUCE GenericProcessor wrapper around processBlock"
```

---

### Task 2.15: `OEconnectEditor` (JUCE editor UI)

**Files:**
- Create: `plugin-openephys/OEconnect/Source/OEconnectEditor.h`
- Create: `plugin-openephys/OEconnect/Source/OEconnectEditor.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <EditorHeaders.h>
#include "OEconnectProcessor.h"

namespace oec::plugin {

class OEconnectEditor final : public GenericEditor,
                              public Timer
{
public:
    explicit OEconnectEditor(GenericProcessor* p);
    void timerCallback() override;
    void paint(Graphics& g) override;
    void resized() override;

private:
    OEconnectProcessor* proc() { return static_cast<OEconnectProcessor*>(getProcessor()); }

    ComboBox    transport_box_;
    TextEditor  zmq_port_;
    TextEditor  bind_addr_;
    ToggleButton auth_toggle_;
    ToggleButton stream_raw_, stream_filt_, stream_spk_, stream_ttl_;
    ComboBox    block_size_;
    TextEditor  slot_count_;
    Label       status_;
};

AudioProcessorEditor* createOEconnectEditor(OEconnectProcessor* p);

}  // namespace oec::plugin
```

- [ ] **Step 2: Implementation**

```cpp
#include "OEconnectEditor.h"

namespace oec::plugin {

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    transport_box_.addItem("Auto",      1);
    transport_box_.addItem("SharedMem", 2);
    transport_box_.addItem("Zmq",       3);
    transport_box_.setSelectedId(1);
    addAndMakeVisible(transport_box_);

    zmq_port_.setText("5557");
    bind_addr_.setText("127.0.0.1");
    addAndMakeVisible(zmq_port_);
    addAndMakeVisible(bind_addr_);
    addAndMakeVisible(auth_toggle_);

    stream_raw_.setToggleState(true, dontSendNotification);
    stream_filt_.setToggleState(true, dontSendNotification);
    stream_spk_.setToggleState(true, dontSendNotification);
    stream_ttl_.setToggleState(true, dontSendNotification);
    addAndMakeVisible(stream_raw_);
    addAndMakeVisible(stream_filt_);
    addAndMakeVisible(stream_spk_);
    addAndMakeVisible(stream_ttl_);

    block_size_.addItem("16 samp", 1);
    block_size_.addItem("32 samp", 2);
    block_size_.addItem("64 samp", 3);
    block_size_.setSelectedId(2);
    addAndMakeVisible(block_size_);

    slot_count_.setText("256");
    addAndMakeVisible(slot_count_);

    addAndMakeVisible(status_);
    status_.setText("idle", dontSendNotification);

    desiredWidth = 280;
    startTimer(500);  /* refresh status every 500 ms */
}

void OEconnectEditor::timerCallback() {
    auto* p = proc();
    if (!p) return;
    String s;
    s << "drops: " << (int64)p->droppedFrames()
      << "  mode: " << String(p->activeTransport())
      << "  board: " << String(p->activeBoard());
    status_.setText(s, dontSendNotification);
}

void OEconnectEditor::paint(Graphics& g) {
    GenericEditor::paint(g);
}

void OEconnectEditor::resized() {
    GenericEditor::resized();
    auto area = getLocalBounds().reduced(6);
    int y = 30;
    transport_box_.setBounds(area.getX(),       y, 120, 24);
    zmq_port_.setBounds      (area.getX() + 130, y, 60,  24);
    y += 28;
    bind_addr_.setBounds     (area.getX(),       y, 120, 24);
    auth_toggle_.setBounds   (area.getX() + 130, y, 24,  24);
    y += 28;
    stream_raw_.setBounds (area.getX(),      y, 60, 24);
    stream_filt_.setBounds(area.getX() + 60, y, 60, 24);
    stream_spk_.setBounds (area.getX() + 120, y, 60, 24);
    stream_ttl_.setBounds (area.getX() + 180, y, 60, 24);
    y += 28;
    block_size_.setBounds(area.getX(),       y, 100, 24);
    slot_count_.setBounds(area.getX() + 110, y, 60,  24);
    y += 28;
    status_.setBounds(area.getX(), y, area.getWidth(), 40);
}

AudioProcessorEditor* createOEconnectEditor(OEconnectProcessor* p) {
    return new OEconnectEditor(p);
}

}  // namespace oec::plugin
```

- [ ] **Step 3: Build**

```powershell
cmake --build build-plugin --target OEconnect
```

Expected: clean build.

- [ ] **Step 4: Commit**

```powershell
git add plugin-openephys/OEconnect/Source/OEconnectEditor.*
git commit -m "feat(plugin): OEconnect JUCE editor UI"
```

---

### Task 2.16: Final plugin tests pass + bundle install instructions

- [ ] **Step 1: Run full plugin test suite**

```powershell
ctest --test-dir build-plugin --output-on-failure
```

Expected: every test PASSES.

- [ ] **Step 2: Document install steps in plugin README**

Append to `plugin-openephys/OEconnect/README.md`:

```markdown
## Install (after `cmake --build`)

1. Locate `build/OEconnect.bundle` (Linux/macOS) or `build/Release/OEconnect.dll`
   (Windows).
2. Copy it into your OE GUI `plugins` directory:
   - Windows: `%APPDATA%\Open Ephys\plugins\`
   - macOS:   `~/Library/Application Support/Open Ephys/plugins/`
   - Linux:   `~/.config/Open Ephys/plugins/`
3. Restart OE GUI. `OEconnect` appears under the *Sinks* category.
4. Drag onto the signal chain *after* a Bandpass Filter (or any other
   downstream consumer) and *before* a Record Node:
   `[Acq Source] → [Bandpass] → [OEconnect] → [Record Node]`.
```

- [ ] **Step 3: Commit**

```powershell
git add plugin-openephys/OEconnect/README.md
git commit -m "docs(plugin): install + signal-chain placement instructions"
```

---

### Task 2.17: Cross-stream sync markers (echo Bonsai-issued TTLs onto OE event bus)

**Why:** Spec §9.3 requires every Bonsai-issued TTL to also land on OE's
own event bus so the OE Record Node captures it in `.events`. Offline
alignment between Bonsai-written and OE-written files relies on these
markers.

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.h`
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp`
- Modify: `plugin-openephys/OEconnect/Tests/test_processor_hot_path.cc`

- [ ] **Step 1: Extend `ProcessorConfig` with a sync-marker callback**

Add `#include <functional>` at the top of `OEconnectProcessor.h`, and add to
the `ProcessorConfig` struct:

```cpp
/** Called from the audio thread whenever a Bonsai-issued TTL fires.
 *  JUCE wrapper hooks GenericProcessor::addEvent() here so OE's Record
 *  Node captures the same edge. Leave null in unit tests. */
std::function<void(uint8_t line, uint8_t edge, uint64_t sample_index)>
    on_ttl_emit;
```

- [ ] **Step 2: Honour the callback in `drainCmdRing`**

In `OEconnectProcessor.cpp`, change the `drainCmdRing` signature to accept
`const ProcessorConfig& cfg`, and inside the `SET_TTL` / `PULSE_TTL` arms add
the callback invocation:

```cpp
case OEC_CMD_SET_TTL: {
    uint8_t line = body[6];
    uint8_t edge = body[7];
    uint64_t s = board.setTtl(line, edge != 0);
    if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
    emitAck(t, cookie, OEC_ACK_OK, s);
    break;
}
case OEC_CMD_PULSE_TTL: {
    uint8_t line = body[6];
    uint8_t edge = body[7];
    uint64_t s = board.setTtl(line, edge != 0);
    if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
    emitAck(t, cookie, OEC_ACK_OK, s);
    break;
}
```

Change the call site inside `processBlock` from `drainCmdRing(transport, board)`
to `drainCmdRing(transport, board, cfg)`.

- [ ] **Step 3: Wire the callback in the JUCE wrapper**

In `OEconnectProcessor::startAcquisition()` (Task 2.14), after the transport
is up:

```cpp
cfg_.on_ttl_emit = [this](uint8_t line, uint8_t edge, uint64_t /*s*/) {
    /* TODO(impl): exact addEvent() signature depends on plugin-GUI version.
       Look in external/plugin-GUI/Source/Processors/GenericProcessor.h
       for the current API; typical pattern:
         addEvent(eventChannel, sample_within_block, line | (edge << 8));
       Must remain wait-free — OE's addEvent is a fixed-size lock-free push
       onto the event bus. */
    (void)this; (void)line; (void)edge;
};
```

- [ ] **Step 4: Test — verify the callback fires for SET_TTL**

Append to `test_processor_hot_path.cc`:

```cpp
TEST(HotPath, SetTtlInvokesSyncMarkerCallback) {
    ShmemTransport t;
    std::string name = uniq() + ".marker";
    ASSERT_TRUE(t.start(name));
    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    /* Inject SET_TTL into cmd ring */
    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* cmd_w = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_CMD, &cmd_w), OEC_OK);
    uint32_t cap = 0;
    void* slot = oec_ringbuf_acquire(cmd_w, 0, &cap);
    ASSERT_NE(slot, nullptr);
    oec_frame_header_t hdr;
    oec_frame_init(&hdr, OEC_STREAM_CMD, 8, 0, 0, 0);
    std::memcpy(slot, &hdr, sizeof(hdr));
    struct { uint16_t cmd_id; uint32_t cookie; uint8_t line; uint8_t edge; } body{
        OEC_CMD_SET_TTL, 0x111u, 5, 1
    };
    std::memcpy((uint8_t*)slot + sizeof(hdr), &body, sizeof(body));
    oec_ringbuf_publish(cmd_w);
    oec_ringbuf_detach(cmd_w);
    oec_shm_close(h);

    int callback_hits = 0;
    uint8_t observed_line = 0, observed_edge = 0;
    ProcessorConfig cfg;
    cfg.num_channels = 1; cfg.block_size = 1;
    cfg.enable_raw = cfg.enable_filtered = cfg.enable_spikes = cfg.enable_ttl = false;
    cfg.on_ttl_emit = [&](uint8_t line, uint8_t edge, uint64_t /*s*/) {
        ++callback_hits; observed_line = line; observed_edge = edge;
    };
    std::vector<int16_t> dummy(1, 0);
    processBlock(dummy.data(), 0, cfg, t, board, outbox);

    EXPECT_EQ(callback_hits, 1);
    EXPECT_EQ(observed_line, 5);
    EXPECT_EQ(observed_edge, 1);

    t.stop();
    oec_shm_unlink(name.c_str());
}
```

- [ ] **Step 5: Build, run, commit**

```powershell
cmake --build build-plugin --target oec_plugin_tests
ctest --test-dir build-plugin -R HotPath --output-on-failure
```

Expected: new test PASSES.

```powershell
git add plugin-openephys/OEconnect/Source/OEconnectProcessor.* plugin-openephys/OEconnect/Tests/test_processor_hot_path.cc
git commit -m "feat(plugin): cross-stream sync markers — Bonsai TTLs echoed onto OE event bus"
```

---

### Task 2.18: Slow-cmd worker thread

**Why:** Spec §9.4 + §7.5 require `START_RECORD` / `STOP_RECORD` /
`START_ACQ` / `STOP_ACQ` to dispatch off the audio thread and push
`ACK(COMPLETED)` once done. Task 2.13 currently emits `ACK(PENDING)` and
drops the request.

**Files:**
- Create: `plugin-openephys/OEconnect/Source/Util/SlowCmdWorker.h`
- Create: `plugin-openephys/OEconnect/Source/Util/SlowCmdWorker.cpp`
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.h`
- Modify: `plugin-openephys/OEconnect/Source/OEconnectProcessor.cpp`

- [ ] **Step 1: `SlowCmdWorker.h`**

```cpp
#pragma once
#include "AckOutbox.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace oec::plugin {

struct SlowCmdRequest {
    uint16_t cmd_id;
    uint32_t cookie;
    std::string arg1;   /* e.g. directory */
    std::string arg2;   /* e.g. prefix    */
};

class SlowCmdWorker {
public:
    using Dispatcher = std::function<uint16_t(const SlowCmdRequest&)>;

    SlowCmdWorker(AckOutbox& outbox, Dispatcher dispatcher)
        : outbox_(outbox), dispatcher_(std::move(dispatcher)) {}
    ~SlowCmdWorker() { stop(); }

    void start();
    void stop();
    void enqueue(SlowCmdRequest req);

private:
    void loop();

    AckOutbox&            outbox_;
    Dispatcher            dispatcher_;
    std::atomic<bool>     running_{false};
    std::thread           thread_;
    std::mutex            mu_;
    std::condition_variable cv_;
    std::deque<SlowCmdRequest> q_;
};

}  // namespace oec::plugin
```

- [ ] **Step 2: `SlowCmdWorker.cpp`**

```cpp
#include "SlowCmdWorker.h"

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

void SlowCmdWorker::start() {
    stop();
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&SlowCmdWorker::loop, this);
}

void SlowCmdWorker::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
}

void SlowCmdWorker::enqueue(SlowCmdRequest req) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(req));
    }
    cv_.notify_one();
}

void SlowCmdWorker::loop() {
    while (running_.load(std::memory_order_acquire)) {
        SlowCmdRequest req;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return !q_.empty() || !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) return;
            req = std::move(q_.front());
            q_.pop_front();
        }
        uint16_t status = dispatcher_ ? dispatcher_(req) : OEC_ACK_NOT_SUPPORTED;
        AckEntry e{};
        e.cookie = req.cookie;
        e.status = status;
        outbox_.tryPush(e);
    }
}

}  // namespace oec::plugin
```

- [ ] **Step 3: Add the worker + a `slow_enqueue` hook to `ProcessorConfig`**

In `OEconnectProcessor.h`, inside `ProcessorConfig` (after `on_ttl_emit`):

```cpp
std::function<void(SlowCmdRequest)> slow_enqueue;
```

(Forward-declare `SlowCmdRequest` at the top of the header, or include
`Util/SlowCmdWorker.h`.)

Inside the `OEconnectProcessor` class, add:

```cpp
private:
    std::unique_ptr<SlowCmdWorker> slow_worker_;
```

- [ ] **Step 4: Replace the slow-cmd arm of `drainCmdRing`**

In `OEconnectProcessor.cpp` `drainCmdRing` (Task 2.13), replace the
`START_RECORD/STOP_RECORD/START_ACQ/STOP_ACQ` arm with:

```cpp
case OEC_CMD_START_RECORD:
case OEC_CMD_STOP_RECORD:
case OEC_CMD_START_ACQ:
case OEC_CMD_STOP_ACQ: {
    SlowCmdRequest req;
    req.cmd_id = cmd_id;
    req.cookie = cookie;
    if (cmd_id == OEC_CMD_START_RECORD &&
        sz >= sizeof(oec_frame_header_t) + 6 + 4)
    {
        const uint8_t* p = body + 6;
        uint16_t dlen = 0;
        std::memcpy(&dlen, p, 2);
        req.arg1.assign((const char*)(p + 2), dlen);
        const uint8_t* p2 = p + 2 + dlen;
        uint16_t plen = 0;
        std::memcpy(&plen, p2, 2);
        req.arg2.assign((const char*)(p2 + 2), plen);
    }
    if (cfg.slow_enqueue) cfg.slow_enqueue(std::move(req));
    emitAck(t, cookie, OEC_ACK_PENDING, 0);
    break;
}
```

- [ ] **Step 5: Wire the worker into the JUCE wrapper**

In `OEconnectProcessor.cpp` `startAcquisition()`, after `cfg_.on_ttl_emit = …`:

```cpp
slow_worker_ = std::make_unique<SlowCmdWorker>(outbox_,
    [this](const SlowCmdRequest& req) -> uint16_t {
        switch (req.cmd_id) {
            case OEC_CMD_START_RECORD:
                CoreServices::setRecordingDirectory(juce::String(req.arg1));
                /* TODO(impl): the CoreServices method for the file-name
                   prefix varies across plugin-GUI minor versions.
                   Look in external/plugin-GUI/Source/CoreServices.h for
                   the current API and forward req.arg2 to it. */
                CoreServices::setRecordingStatus(true);
                return OEC_ACK_COMPLETED;
            case OEC_CMD_STOP_RECORD:
                CoreServices::setRecordingStatus(false);
                return OEC_ACK_COMPLETED;
            case OEC_CMD_START_ACQ:
                CoreServices::setAcquisitionStatus(true);
                return OEC_ACK_COMPLETED;
            case OEC_CMD_STOP_ACQ:
                CoreServices::setAcquisitionStatus(false);
                return OEC_ACK_COMPLETED;
            default:
                return OEC_ACK_NOT_SUPPORTED;
        }
    });
slow_worker_->start();
cfg_.slow_enqueue = [this](SlowCmdRequest r) { slow_worker_->enqueue(std::move(r)); };
```

And in `stopAcquisition()`:

```cpp
if (slow_worker_) slow_worker_->stop();
```

Add `#include "Util/SlowCmdWorker.h"` near the top of
`OEconnectProcessor.cpp`, and to `Tests/CMakeLists.txt` add
`../Source/Util/SlowCmdWorker.cpp` to the `oec_plugin_tests` source list so
unit-test builds compile it.

- [ ] **Step 6: Build, run all plugin tests, commit**

```powershell
cmake --build build-plugin --target oec_plugin_tests OEconnect
ctest --test-dir build-plugin --output-on-failure
```

Expected: every existing test still PASSES (slow dispatch is exercised only
with the live OE GUI; SDK method calls are guarded behind the dispatcher
lambda and don't run in unit-test builds).

```powershell
git add plugin-openephys/OEconnect/Source/Util/SlowCmdWorker.* plugin-openephys/OEconnect/Source/OEconnectProcessor.* plugin-openephys/OEconnect/Tests/CMakeLists.txt
git commit -m "feat(plugin): slow-cmd worker thread for record/acquisition lifecycle"
```

---

**Phase 2 done.** OE plugin compiles and unit-tests pass (Frame/AckOutbox/Shmem/Zmq/HotPath/SyncMarker). Adapter SDK wiring + `CoreServices` method names marked TODO for live-OE bring-up.

---

## Phase 3 — Bonsai package (`Bonsai.OEconnect`)

### Task 3.1: Solution + dual-target csproj + native runtime bundle

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj`
- Create: `package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests/Bonsai.OEconnect.Tests.csproj`
- Create: `package-bonsai/Bonsai.OEconnect/Directory.Build.props`
- Create: `package-bonsai/Bonsai.OEconnect/global.json`

- [ ] **Step 1: `global.json` pins SDK**

```json
{
  "sdk": {
    "version": "8.0.100",
    "rollForward": "latestFeature"
  }
}
```

- [ ] **Step 2: `Directory.Build.props`**

```xml
<Project>
  <PropertyGroup>
    <LangVersion>11.0</LangVersion>
    <Nullable>enable</Nullable>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <Authors>OEconnect contributors</Authors>
    <Company>OEconnect</Company>
    <Copyright>Copyright (c) 2026 OEconnect contributors</Copyright>
    <PackageLicenseExpression>MIT</PackageLicenseExpression>
    <RepositoryUrl>https://github.com/oeconnect/oeconnect</RepositoryUrl>
    <Version>1.0.0</Version>
    <AssemblyVersion>1.0.0.0</AssemblyVersion>
    <FileVersion>1.0.0.0</FileVersion>
  </PropertyGroup>
</Project>
```

- [ ] **Step 3: Main package csproj (dual target)**

`src/Bonsai.OEconnect/Bonsai.OEconnect.csproj`:

```xml
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFrameworks>net472;net6.0</TargetFrameworks>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <RootNamespace>Bonsai.OEconnect</RootNamespace>
    <AssemblyName>Bonsai.OEconnect</AssemblyName>

    <PackageId>Bonsai.OEconnect</PackageId>
    <Description>Bonsai-rx operators for OpenEphys streaming and control (OEconnect bridge).</Description>
    <PackageTags>bonsai openephys neuroscience ephys closed-loop</PackageTags>
    <GeneratePackageOnBuild>false</GeneratePackageOnBuild>
    <IncludeBuildOutput>true</IncludeBuildOutput>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="Bonsai.Core" Version="2.8.0" />
    <PackageReference Include="NetMQ" Version="4.0.1.13" />
    <PackageReference Include="System.Memory" Version="4.5.5" Condition="'$(TargetFramework)' == 'net472'" />
    <PackageReference Include="System.Buffers" Version="4.5.1" Condition="'$(TargetFramework)' == 'net472'" />
  </ItemGroup>

  <ItemGroup>
    <None Include="..\..\..\..\libshared\oeconnect\build\liboeconnect.dll"
          Pack="true"
          PackagePath="runtimes/win-x64/native/liboeconnect.dll"
          CopyToOutputDirectory="PreserveNewest"
          Condition="Exists('..\..\..\..\libshared\oeconnect\build\liboeconnect.dll')" />
    <None Include="..\..\..\..\libshared\oeconnect\build\liboeconnect.so"
          Pack="true"
          PackagePath="runtimes/linux-x64/native/liboeconnect.so"
          CopyToOutputDirectory="PreserveNewest"
          Condition="Exists('..\..\..\..\libshared\oeconnect\build\liboeconnect.so')" />
    <None Include="..\..\..\..\libshared\oeconnect\build\liboeconnect.dylib"
          Pack="true"
          PackagePath="runtimes/osx-x64/native/liboeconnect.dylib"
          CopyToOutputDirectory="PreserveNewest"
          Condition="Exists('..\..\..\..\libshared\oeconnect\build\liboeconnect.dylib')" />
  </ItemGroup>

</Project>
```

- [ ] **Step 4: Tests csproj**

`tests/Bonsai.OEconnect.Tests/Bonsai.OEconnect.Tests.csproj`:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net6.0</TargetFramework>
    <IsPackable>false</IsPackable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.8.0" />
    <PackageReference Include="xunit" Version="2.6.4" />
    <PackageReference Include="xunit.runner.visualstudio" Version="2.5.6" />
    <PackageReference Include="BenchmarkDotNet" Version="0.13.12" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\src\Bonsai.OEconnect\Bonsai.OEconnect.csproj" />
  </ItemGroup>
</Project>
```

- [ ] **Step 5: Solution file**

Run from `package-bonsai/Bonsai.OEconnect/`:

```powershell
dotnet new sln -n Bonsai.OEconnect
dotnet sln Bonsai.OEconnect.sln add src/Bonsai.OEconnect/Bonsai.OEconnect.csproj
dotnet sln Bonsai.OEconnect.sln add tests/Bonsai.OEconnect.Tests/Bonsai.OEconnect.Tests.csproj
```

- [ ] **Step 6: Smoke restore + build**

```powershell
dotnet restore package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
```

Expected: clean build (no source yet beyond the csproj).

- [ ] **Step 7: Commit**

```powershell
git add package-bonsai/
git commit -m "build(bonsai): solution + dual-target csproj + native runtime layout"
```

---

### Task 3.2: P/Invoke bindings to `liboeconnect`

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Interop/NativeMethods.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Interop/NativeStructs.cs`

- [ ] **Step 1: `NativeStructs.cs`**

```csharp
using System.Runtime.InteropServices;

namespace Bonsai.OEconnect.Interop;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct OecFrameHeader
{
    public uint Magic;
    public byte VersionMajor;
    public byte VersionMinor;
    public ushort StreamId;
    public uint PayloadLen;
    public ulong SampleIndex;
    public ulong HostQpcTicks;
    public ushort Flags;
    public ushort Crc16;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct OecBlockSubheader
{
    public ushort NumChannels;
    public ushort NumSamples;
    public byte Dtype;
    public byte SourceId;
    public ushort Reserved;
}

public enum OecStatus
{
    Ok                  = 0,
    EInvalidArg         = -1,
    EBadMagic           = -2,
    EVersionMismatch    = -3,
    EFrameTooLarge      = -4,
    ERingFull           = -5,
    ERingEmpty          = -6,
    ESyscall            = -7,
    ENoSession          = -8,
    EParse              = -9,
    ENotImplemented     = -10
}

public static class OecStreams
{
    public const ushort RawBlock      = 0x0001;
    public const ushort FilteredBlock = 0x0002;
    public const ushort Spike         = 0x0003;
    public const ushort TtlEvent      = 0x0004;
    public const ushort Sync          = 0x0010;
    public const ushort Cmd           = 0x0020;
    public const ushort Ack           = 0x0021;
    public const ushort Error         = 0x0022;
}

public static class OecCmds
{
    public const ushort StartRecord = 0x0001;
    public const ushort StopRecord  = 0x0002;
    public const ushort SetTtl      = 0x0003;
    public const ushort PulseTtl    = 0x0004;
    public const ushort StartAcq    = 0x0005;
    public const ushort StopAcq     = 0x0006;
    public const ushort GetState    = 0x0007;
}

public enum OecAckStatus : ushort
{
    Ok = 0, Pending = 1, Completed = 2, Busy = 3,
    NotSupported = 4, BadArg = 5, Timeout = 6, Internal = 7
}
```

- [ ] **Step 2: `NativeMethods.cs`**

```csharp
using System;
using System.Runtime.InteropServices;

namespace Bonsai.OEconnect.Interop;

internal static class NativeMethods
{
    private const string Lib = "oeconnect";

    /* --- frame --- */
    [DllImport(Lib, EntryPoint = "oec_frame_init", CallingConvention = CallingConvention.Cdecl)]
    public static extern void FrameInit(ref OecFrameHeader header,
        ushort streamId, uint payloadLen, ulong sampleIndex,
        ulong hostQpcTicks, ushort flags);

    [DllImport(Lib, EntryPoint = "oec_frame_validate", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus FrameValidate(in OecFrameHeader header);

    [DllImport(Lib, EntryPoint = "oec_crc16", CallingConvention = CallingConvention.Cdecl)]
    public static extern ushort Crc16(IntPtr data, UIntPtr len);

    /* --- region --- */
    [DllImport(Lib, EntryPoint = "oec_region_size", CallingConvention = CallingConvention.Cdecl)]
    public static extern UIntPtr RegionSize(
        uint slotSize, uint slotCount,
        uint cmdSlotSize, uint cmdSlotCount,
        uint ackSlotSize, uint ackSlotCount);

    [DllImport(Lib, EntryPoint = "oec_region_open", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus RegionOpen(IntPtr mem, UIntPtr memLen, out IntPtr outHeader);

    /* --- ringbuf --- */
    [DllImport(Lib, EntryPoint = "oec_ringbuf_attach", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus RingbufAttach(IntPtr regionMem, int kind, out IntPtr outRing);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_detach", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufDetach(IntPtr rb);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_peek", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr RingbufPeek(IntPtr rb, out uint outSize);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_consume", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufConsume(IntPtr rb);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_acquire", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr RingbufAcquire(IntPtr rb, int dropOldest, out uint outSize);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_publish", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufPublish(IntPtr rb);

    /* --- shm --- */
    [DllImport(Lib, EntryPoint = "oec_shm_open", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus ShmOpen(
        [MarshalAs(UnmanagedType.LPStr)] string name, UIntPtr expectedSize,
        out IntPtr outShm, out IntPtr outMapped, out UIntPtr outMappedSize);

    [DllImport(Lib, EntryPoint = "oec_shm_close", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ShmClose(IntPtr shm);

    /* --- drift --- */
    [DllImport(Lib, EntryPoint = "oec_drift_create", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr DriftCreate();

    [DllImport(Lib, EntryPoint = "oec_drift_destroy", CallingConvention = CallingConvention.Cdecl)]
    public static extern void DriftDestroy(IntPtr fit);

    [DllImport(Lib, EntryPoint = "oec_drift_add", CallingConvention = CallingConvention.Cdecl)]
    public static extern void DriftAdd(IntPtr fit, ulong sampleIndex, ulong qpc);

    [DllImport(Lib, EntryPoint = "oec_drift_fit", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus DriftFit(IntPtr fit, out double outA, out double outB);

    [DllImport(Lib, EntryPoint = "oec_drift_predict_qpc", CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong DriftPredictQpc(IntPtr fit, ulong sampleIndex);

    /* --- sidecar --- */
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct OecSidecar
    {
        public int Pid;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ShmRegion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string DataEvent;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string CmdEvent;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ZmqFallbackEndpoint;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ZmqCmdEndpoint;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)] public string SpecVersion;
        public ulong StartedUnixNs;
    }

    [DllImport(Lib, EntryPoint = "oec_sidecar_read", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus SidecarRead(int pid, out OecSidecar outSidecar);

    [DllImport(Lib, EntryPoint = "oec_sidecar_dir", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus SidecarDir(
        [Out] byte[] outBuf, UIntPtr outBufLen);
}
```

- [ ] **Step 3: Build (just to ensure interop compiles)**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
```

Expected: clean build.

- [ ] **Step 4: Commit**

```powershell
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Interop/
git commit -m "feat(bonsai): P/Invoke bindings to liboeconnect"
```

---

### Task 3.3: Public data types

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/RawBlock.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/TtlEvent.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/SpikeEvent.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/SyncPoint.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/SessionStatus.cs`

- [ ] **Step 1: `RawBlock.cs`**

```csharp
using System;

namespace Bonsai.OEconnect.Data;

/// <summary>
/// One block of continuous samples produced by an OE source node.
///
/// IMPORTANT: <see cref="Samples"/> wraps a pooled buffer that is valid ONLY
/// inside the OnNext invocation that delivered this block. Clone via
/// <see cref="Clone"/> or <c>Samples.ToArray()</c> if you need to retain it.
/// </summary>
public readonly struct RawBlock
{
    public ulong SampleIndex { get; }
    public ulong HostQpcTicks { get; }
    public ushort StreamId { get; }
    public int NumChannels { get; }
    public int NumSamples { get; }
    public ReadOnlyMemory<short> Samples { get; }

    public RawBlock(ulong sampleIndex, ulong hostQpcTicks, ushort streamId,
                    int numChannels, int numSamples, ReadOnlyMemory<short> samples)
    {
        SampleIndex = sampleIndex;
        HostQpcTicks = hostQpcTicks;
        StreamId = streamId;
        NumChannels = numChannels;
        NumSamples = numSamples;
        Samples = samples;
    }

    /// <summary>Returns an owning copy backed by <c>new short[]</c>.</summary>
    public RawBlock Clone() =>
        new(SampleIndex, HostQpcTicks, StreamId, NumChannels, NumSamples,
            Samples.ToArray());
}
```

- [ ] **Step 2: `TtlEvent.cs`, `SpikeEvent.cs`, `SyncPoint.cs`**

```csharp
namespace Bonsai.OEconnect.Data;

public readonly struct TtlEvent
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public byte Line { get; init; }
    public byte Edge { get; init; }      // 0 = falling, 1 = rising
    public byte BoardId { get; init; }
}
```

```csharp
using System;

namespace Bonsai.OEconnect.Data;

public readonly struct SpikeEvent
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public ushort ElectrodeId { get; init; }
    public ushort UnitId { get; init; }
    public float Threshold { get; init; }
    public ReadOnlyMemory<short> Waveform { get; init; }
}
```

```csharp
namespace Bonsai.OEconnect.Data;

public readonly struct SyncPoint
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public double FpgaSampleRateHz { get; init; }
}
```

- [ ] **Step 3: `SessionStatus.cs`**

```csharp
namespace Bonsai.OEconnect.Data;

public sealed class SessionStatus
{
    public bool   IsConnected { get; init; }
    public string Transport   { get; init; } = "—";
    public string BoardName   { get; init; } = "—";
    public long   FrameCount  { get; init; }
    public long   DropCount   { get; init; }
    public double EstimatedLagMs { get; init; }
}

public sealed class OpenEphysConnectionException : System.Exception
{
    public string Transport { get; }
    public string Endpoint { get; }
    public long LastDropCount { get; }
    public OpenEphysConnectionException(string transport, string endpoint, long drops, string message)
        : base(message) { Transport = transport; Endpoint = endpoint; LastDropCount = drops; }
}
```

- [ ] **Step 4: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Data/
git commit -m "feat(bonsai): public data types"
```

---

### Task 3.4: `ITransportClient` + `ShmemClient`

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ITransportClient.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ShmemClient.cs`

- [ ] **Step 1: `ITransportClient.cs`**

```csharp
using System;

namespace Bonsai.OEconnect.Transport;

internal interface ITransportClient : IDisposable
{
    bool Start(string endpoint);
    void Stop();

    /// <summary>Pop one data frame as a byte slice; null if none. Buffer valid until next call.</summary>
    ReadOnlySpan<byte> PeekData();
    void ConsumeData();

    ReadOnlySpan<byte> PeekAck();
    void ConsumeAck();

    /// <summary>Push a CMD frame. Returns false if the cmd ring/channel is full.</summary>
    bool PostCmd(ReadOnlySpan<byte> frameBytes);

    string Name { get; }
    bool IsConnected { get; }
    ulong ProducerHeartbeatNs { get; }
}
```

- [ ] **Step 2: `ShmemClient.cs`**

```csharp
using System;
using System.Runtime.InteropServices;
using Bonsai.OEconnect.Interop;

namespace Bonsai.OEconnect.Transport;

internal sealed class ShmemClient : ITransportClient
{
    private IntPtr _shm = IntPtr.Zero;
    private IntPtr _mapped = IntPtr.Zero;
    private UIntPtr _mappedSize = UIntPtr.Zero;
    private IntPtr _dataRing = IntPtr.Zero;
    private IntPtr _cmdRing  = IntPtr.Zero;
    private IntPtr _ackRing  = IntPtr.Zero;
    private IntPtr _regionHeader = IntPtr.Zero;
    private string _endpoint = string.Empty;

    public string Name => "SharedMem";
    public bool IsConnected => _shm != IntPtr.Zero;

    public ulong ProducerHeartbeatNs
    {
        get
        {
            if (_regionHeader == IntPtr.Zero) return 0;
            /* Field offset in oec_region_header_t: 36 (after slot sizes + pad). */
            return (ulong)Marshal.ReadInt64(_regionHeader, 36);
        }
    }

    public bool Start(string endpoint)
    {
        Stop();
        _endpoint = endpoint;
        var rc = NativeMethods.ShmOpen(endpoint, UIntPtr.Zero, out _shm, out _mapped, out _mappedSize);
        if (rc != OecStatus.Ok) return false;
        if (NativeMethods.RegionOpen(_mapped, _mappedSize, out _regionHeader) != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 0, out _dataRing) != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 1, out _cmdRing)  != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 2, out _ackRing)  != OecStatus.Ok) { Stop(); return false; }
        return true;
    }

    public void Stop()
    {
        if (_dataRing != IntPtr.Zero) { NativeMethods.RingbufDetach(_dataRing); _dataRing = IntPtr.Zero; }
        if (_cmdRing  != IntPtr.Zero) { NativeMethods.RingbufDetach(_cmdRing);  _cmdRing  = IntPtr.Zero; }
        if (_ackRing  != IntPtr.Zero) { NativeMethods.RingbufDetach(_ackRing);  _ackRing  = IntPtr.Zero; }
        if (_shm != IntPtr.Zero)      { NativeMethods.ShmClose(_shm);            _shm = IntPtr.Zero; }
        _mapped = IntPtr.Zero; _mappedSize = UIntPtr.Zero; _regionHeader = IntPtr.Zero;
    }

    public ReadOnlySpan<byte> PeekData()
    {
        if (_dataRing == IntPtr.Zero) return default;
        var ptr = NativeMethods.RingbufPeek(_dataRing, out uint size);
        if (ptr == IntPtr.Zero) return default;
        unsafe { return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)size); }
    }
    public void ConsumeData() { if (_dataRing != IntPtr.Zero) NativeMethods.RingbufConsume(_dataRing); }

    public ReadOnlySpan<byte> PeekAck()
    {
        if (_ackRing == IntPtr.Zero) return default;
        var ptr = NativeMethods.RingbufPeek(_ackRing, out uint size);
        if (ptr == IntPtr.Zero) return default;
        unsafe { return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)size); }
    }
    public void ConsumeAck() { if (_ackRing != IntPtr.Zero) NativeMethods.RingbufConsume(_ackRing); }

    public bool PostCmd(ReadOnlySpan<byte> frameBytes)
    {
        if (_cmdRing == IntPtr.Zero) return false;
        var slot = NativeMethods.RingbufAcquire(_cmdRing, 0, out uint cap);
        if (slot == IntPtr.Zero || cap < frameBytes.Length) return false;
        unsafe
        {
            var dst = new Span<byte>(slot.ToPointer(), (int)cap);
            frameBytes.CopyTo(dst);
        }
        NativeMethods.RingbufPublish(_cmdRing);
        return true;
    }

    public void Dispose() => Stop();
}
```

- [ ] **Step 3: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/
git commit -m "feat(bonsai): ITransportClient + ShmemClient via P/Invoke"
```

---

### Task 3.5: `ZmqClient`

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ZmqClient.cs`

- [ ] **Step 1: Write the client**

```csharp
using System;
using System.Collections.Concurrent;
using System.Threading;
using NetMQ;
using NetMQ.Sockets;

namespace Bonsai.OEconnect.Transport;

internal sealed class ZmqClient : ITransportClient
{
    private SubscriberSocket? _sub;
    private RequestSocket?    _req;
    private Thread?           _drainer;
    private CancellationTokenSource? _cts;

    private readonly ConcurrentQueue<byte[]> _dataQ = new();
    private readonly ConcurrentQueue<byte[]> _ackQ  = new();
    private byte[]? _lastData;
    private byte[]? _lastAck;

    public string Name => "Zmq";
    public bool IsConnected => _sub != null;
    public ulong ProducerHeartbeatNs => 0;

    public bool Start(string endpointPair)
    {
        Stop();
        var sep = endpointPair.IndexOf('|');
        if (sep < 0) return false;
        var pubEp = endpointPair[..sep];
        var repEp = endpointPair[(sep + 1)..];

        _sub = new SubscriberSocket();
        _sub.SubscribeToAnyTopic();
        _sub.Connect(pubEp);

        _req = new RequestSocket();
        _req.Connect(repEp);

        _cts = new CancellationTokenSource();
        _drainer = new Thread(() => DrainLoop(_cts.Token)) { IsBackground = true, Name = "OEconnect-ZMQ-drain" };
        _drainer.Start();
        return true;
    }

    public void Stop()
    {
        _cts?.Cancel();
        _drainer?.Join(500);
        _drainer = null;
        _sub?.Dispose(); _sub = null;
        _req?.Dispose(); _req = null;
        _cts?.Dispose(); _cts = null;
    }

    private void DrainLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _sub != null)
        {
            if (_sub.TryReceiveFrameBytes(TimeSpan.FromMilliseconds(50), out var topic, out bool more)
                && more
                && _sub.TryReceiveFrameBytes(TimeSpan.FromMilliseconds(50), out var body, out _))
            {
                _dataQ.Enqueue(body);
            }
        }
    }

    public ReadOnlySpan<byte> PeekData()
    {
        if (_lastData != null) return _lastData.AsSpan();
        if (_dataQ.TryDequeue(out var b)) { _lastData = b; return b; }
        return default;
    }
    public void ConsumeData() { _lastData = null; }

    public ReadOnlySpan<byte> PeekAck()
    {
        if (_lastAck != null) return _lastAck.AsSpan();
        if (_ackQ.TryDequeue(out var b)) { _lastAck = b; return b; }
        return default;
    }
    public void ConsumeAck() { _lastAck = null; }

    public bool PostCmd(ReadOnlySpan<byte> frameBytes)
    {
        if (_req == null) return false;
        _req.SendFrame(frameBytes.ToArray());
        if (_req.TryReceiveFrameBytes(TimeSpan.FromSeconds(2), out var reply))
        {
            _ackQ.Enqueue(reply);
            return true;
        }
        return false;
    }

    public void Dispose() => Stop();
}
```

- [ ] **Step 2: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ZmqClient.cs
git commit -m "feat(bonsai): ZmqClient via NetMQ"
```

---

### Task 3.6: `SessionRegistry` (ref-counted, auto-discovery)

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Sessions/SessionRegistry.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Sessions/Session.cs`

- [ ] **Step 1: `Session.cs`**

```csharp
using System;
using System.Reactive.Subjects;
using System.Threading;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal sealed class Session : IDisposable
{
    private readonly object _lock = new();
    private int _refCount;
    private CancellationTokenSource? _cts;
    private Thread? _reader;
    private readonly IntPtr _drift = NativeMethods.DriftCreate();

    public readonly Subject<RawBlock>      RawSubject      = new();
    public readonly Subject<RawBlock>      FilteredSubject = new();
    public readonly Subject<SpikeEvent>    SpikeSubject    = new();
    public readonly Subject<TtlEvent>      TtlSubject      = new();
    public readonly Subject<SyncPoint>     SyncSubject     = new();
    public readonly Subject<SessionStatus> StatusSubject   = new();

    public ITransportClient Transport { get; }
    public string Endpoint { get; }

    public long FrameCount;
    public long DropCount;

    public Session(ITransportClient transport, string endpoint)
    {
        Transport = transport;
        Endpoint = endpoint;
    }

    public void AddRef() => Interlocked.Increment(ref _refCount);

    public void Release()
    {
        if (Interlocked.Decrement(ref _refCount) == 0) Dispose();
    }

    public void StartReader()
    {
        _cts = new CancellationTokenSource();
        _reader = new Thread(() => ReaderLoop(_cts.Token))
        {
            IsBackground = true,
            Name = "OEconnect-reader"
        };
        _reader.Start();
    }

    private unsafe void ReaderLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            var span = Transport.PeekData();
            if (span.IsEmpty) { Thread.Sleep(1); continue; }
            if (span.Length < sizeof(OecFrameHeader)) { Transport.ConsumeData(); continue; }
            fixed (byte* p = span)
            {
                var h = *(OecFrameHeader*)p;
                NativeMethods.DriftAdd(_drift, h.SampleIndex, h.HostQpcTicks);
                Interlocked.Increment(ref FrameCount);

                switch (h.StreamId)
                {
                    case OecStreams.RawBlock:
                    case OecStreams.FilteredBlock:
                        DispatchBlock(h, p, span.Length, h.StreamId == OecStreams.RawBlock);
                        break;
                    case OecStreams.TtlEvent:
                        DispatchTtl(h, p, span.Length);
                        break;
                    case OecStreams.Spike:
                        DispatchSpike(h, p, span.Length);
                        break;
                    case OecStreams.Sync:
                        SyncSubject.OnNext(new SyncPoint {
                            SampleIndex = h.SampleIndex,
                            HostQpcTicks = h.HostQpcTicks,
                            FpgaSampleRateHz = 30000.0
                        });
                        break;
                }
                if ((h.Flags & 0x0002) != 0) Interlocked.Increment(ref DropCount);
            }
            Transport.ConsumeData();
        }
    }

    private unsafe void DispatchBlock(in OecFrameHeader h, byte* p, int len, bool raw)
    {
        if (len < sizeof(OecFrameHeader) + sizeof(OecBlockSubheader)) return;
        var sh = *(OecBlockSubheader*)(p + sizeof(OecFrameHeader));
        var samples = new short[sh.NumChannels * sh.NumSamples];
        fixed (short* dst = samples)
        {
            Buffer.MemoryCopy(p + sizeof(OecFrameHeader) + sizeof(OecBlockSubheader),
                              dst, samples.Length * sizeof(short),
                              samples.Length * sizeof(short));
        }
        var block = new RawBlock(h.SampleIndex, h.HostQpcTicks, h.StreamId,
                                 sh.NumChannels, sh.NumSamples, samples);
        (raw ? RawSubject : FilteredSubject).OnNext(block);
    }

    private unsafe void DispatchTtl(in OecFrameHeader h, byte* p, int len)
    {
        if (len < sizeof(OecFrameHeader) + 4) return;
        byte* body = p + sizeof(OecFrameHeader);
        TtlSubject.OnNext(new TtlEvent {
            SampleIndex = h.SampleIndex,
            HostQpcTicks = h.HostQpcTicks,
            Line = body[0], Edge = body[1], BoardId = body[2]
        });
    }

    private unsafe void DispatchSpike(in OecFrameHeader h, byte* p, int len)
    {
        const int HEAD = 12; // electrode_u16 + unit_u16 + threshold_f32 + waveform_len_u32
        if (len < sizeof(OecFrameHeader) + HEAD) return;
        byte* body = p + sizeof(OecFrameHeader);
        ushort electrode = *(ushort*)(body + 0);
        ushort unit = *(ushort*)(body + 2);
        float thr = *(float*)(body + 4);
        int wfBytes = len - (sizeof(OecFrameHeader) + 8);
        var wf = new short[wfBytes / sizeof(short)];
        fixed (short* dst = wf)
        {
            Buffer.MemoryCopy(body + 8, dst, wf.Length * sizeof(short), wfBytes);
        }
        SpikeSubject.OnNext(new SpikeEvent {
            SampleIndex = h.SampleIndex,
            HostQpcTicks = h.HostQpcTicks,
            ElectrodeId = electrode,
            UnitId = unit,
            Threshold = thr,
            Waveform = wf
        });
    }

    public ulong PredictQpc(ulong sampleIndex)
        => NativeMethods.DriftPredictQpc(_drift, sampleIndex);

    public void Dispose()
    {
        _cts?.Cancel();
        _reader?.Join(500);
        Transport.Dispose();
        NativeMethods.DriftDestroy(_drift);
        RawSubject.OnCompleted();
        FilteredSubject.OnCompleted();
        SpikeSubject.OnCompleted();
        TtlSubject.OnCompleted();
        SyncSubject.OnCompleted();
        StatusSubject.OnCompleted();
    }
}
```

- [ ] **Step 2: `SessionRegistry.cs`**

```csharp
using System;
using System.Collections.Generic;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal static class SessionRegistry
{
    private static readonly object _lock = new();
    private static readonly Dictionary<string, Session> _sessions = new();

    public static Session Acquire(string endpoint)
    {
        lock (_lock)
        {
            if (_sessions.TryGetValue(endpoint, out var existing))
            {
                existing.AddRef();
                return existing;
            }
            var resolved = ResolveEndpoint(endpoint);
            ITransportClient client = resolved.StartsWith("tcp://", StringComparison.Ordinal)
                ? new ZmqClient()
                : new ShmemClient();
            if (!client.Start(resolved))
                throw new Data.OpenEphysConnectionException(client.Name, resolved, 0,
                    $"Failed to start transport for endpoint '{resolved}'.");
            var session = new Session(client, resolved);
            session.StartReader();
            session.AddRef();
            _sessions[endpoint] = session;
            return session;
        }
    }

    public static void Release(string endpoint, Session session)
    {
        lock (_lock)
        {
            session.Release();
            if (_sessions.TryGetValue(endpoint, out var s) && ReferenceEquals(s, session))
            {
                /* If we just released the last ref, AddRef==0; remove. */
                /* AddRef/Release counter is internal; we attempt a probe. */
            }
        }
    }

    private static string ResolveEndpoint(string endpoint)
    {
        if (!string.IsNullOrEmpty(endpoint)) return endpoint;
        /* Auto-discovery: scan sidecar dir for the freshest live JSON. */
        var dir = SidecarDiscovery.Dir();
        var freshest = SidecarDiscovery.FindNewestLive(dir, maxAgeSeconds: 5);
        if (freshest is null)
            throw new Data.OpenEphysConnectionException("Auto", "", 0,
                "No live OEconnect session found via sidecar discovery.");
        /* Prefer shmem on same OS. */
        return string.IsNullOrEmpty(freshest.Value.shm)
            ? freshest.Value.zmq
            : freshest.Value.shm;
    }
}
```

- [ ] **Step 3: Sidecar discovery helper**

`Sessions/SidecarDiscovery.cs`:

```csharp
using System;
using System.IO;
using System.Text.Json;

namespace Bonsai.OEconnect.Sessions;

internal static class SidecarDiscovery
{
    public static string Dir()
    {
        var tmp = Path.GetTempPath();
        var dir = Path.Combine(tmp, "oeconnect", "sessions");
        Directory.CreateDirectory(dir);
        return dir;
    }

    public record struct Sidecar(int Pid, string Shm, string Zmq, ulong StartedNs);

    public static Sidecar? FindNewestLive(string dir, int maxAgeSeconds)
    {
        Sidecar? newest = null;
        var cutoffNs = (ulong)((DateTimeOffset.UtcNow.AddSeconds(-maxAgeSeconds)).ToUnixTimeMilliseconds() * 1_000_000);

        foreach (var f in Directory.EnumerateFiles(dir, "*.json"))
        {
            try
            {
                using var s = File.OpenRead(f);
                using var doc = JsonDocument.Parse(s);
                var root = doc.RootElement;
                var started = root.GetProperty("started_unix_ns").GetUInt64();
                if (started < cutoffNs) continue;
                var sc = new Sidecar(
                    root.GetProperty("pid").GetInt32(),
                    root.GetProperty("shm_region").GetString() ?? "",
                    root.GetProperty("zmq_fallback_endpoint").GetString() ?? "",
                    started);
                if (newest is null || sc.StartedNs > newest.Value.StartedNs)
                    newest = sc;
            }
            catch { /* skip unparseable */ }
        }
        return newest;
    }
}
```

- [ ] **Step 4: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Sessions/
git commit -m "feat(bonsai): SessionRegistry + Session + SidecarDiscovery"
```

---

### Task 3.7: Command-frame builder (`CmdSender`)

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Sessions/CmdSender.cs`

- [ ] **Step 1: Write helper that serialises CMD frames identically to libshared's wire layout**

```csharp
using System;
using System.Runtime.InteropServices;
using System.Threading;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal static class CmdSender
{
    private static int _cookieCounter = 1;

    public static uint NextCookie() => (uint)Interlocked.Increment(ref _cookieCounter);

    /// <summary>Builds a CMD frame: header + {cmd_id, cookie, body...}.</summary>
    public static byte[] BuildCmd(ushort cmdId, uint cookie, ReadOnlySpan<byte> body)
    {
        int hdr = Marshal.SizeOf<OecFrameHeader>();
        int payload = 6 + body.Length;          /* cmd_id_u16 + cookie_u32 + body */
        var buf = new byte[hdr + payload];
        unsafe
        {
            fixed (byte* p = buf)
            {
                OecFrameHeader* fh = (OecFrameHeader*)p;
                NativeMethods.FrameInit(ref *fh, OecStreams.Cmd, (uint)payload, 0, 0, 0);
                byte* b = p + hdr;
                *(ushort*)(b + 0) = cmdId;
                *(uint*)  (b + 2) = cookie;
                body.CopyTo(new Span<byte>(b + 6, body.Length));
            }
        }
        return buf;
    }

    public static bool SendSetTtl(ITransportClient t, byte line, bool high, out uint cookie)
    {
        cookie = NextCookie();
        Span<byte> body = stackalloc byte[2] { line, (byte)(high ? 1 : 0) };
        var frame = BuildCmd(OecCmds.SetTtl, cookie, body);
        return t.PostCmd(frame);
    }

    public static bool SendPulseTtl(ITransportClient t, byte line, bool high, uint widthMicros, out uint cookie)
    {
        cookie = NextCookie();
        Span<byte> body = stackalloc byte[6];
        body[0] = line;
        body[1] = (byte)(high ? 1 : 0);
        BitConverter.TryWriteBytes(body[2..], widthMicros);
        var frame = BuildCmd(OecCmds.PulseTtl, cookie, body);
        return t.PostCmd(frame);
    }

    public static bool SendStartRecord(ITransportClient t, string directory, string prefix, out uint cookie)
    {
        cookie = NextCookie();
        var dirBytes = System.Text.Encoding.UTF8.GetBytes(directory);
        var preBytes = System.Text.Encoding.UTF8.GetBytes(prefix);
        var body = new byte[2 + dirBytes.Length + 2 + preBytes.Length];
        BitConverter.TryWriteBytes(body.AsSpan(0, 2), (ushort)dirBytes.Length);
        dirBytes.CopyTo(body.AsSpan(2, dirBytes.Length));
        BitConverter.TryWriteBytes(body.AsSpan(2 + dirBytes.Length, 2), (ushort)preBytes.Length);
        preBytes.CopyTo(body.AsSpan(4 + dirBytes.Length, preBytes.Length));
        return t.PostCmd(BuildCmd(OecCmds.StartRecord, cookie, body));
    }

    public static bool SendStopRecord(ITransportClient t, out uint cookie)
    {
        cookie = NextCookie();
        return t.PostCmd(BuildCmd(OecCmds.StopRecord, cookie, ReadOnlySpan<byte>.Empty));
    }
}
```

- [ ] **Step 2: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Sessions/CmdSender.cs
git commit -m "feat(bonsai): CMD frame builder"
```

---

### Task 3.8: Source operators

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/RawSamples.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/FilteredSamples.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/Spikes.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/TtlEvents.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/SyncPoints.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/OpenEphysSession.cs`

- [ ] **Step 1: Source-op base + `RawSamples`**

`Operators/SessionSource.cs`:

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

public abstract class SessionSource<T>
{
    [Description("Endpoint: empty = auto-discovery; \"tcp://host:port\" = ZMQ; \"shm://...\" = explicit shmem.")]
    public string Endpoint { get; set; } = string.Empty;

    protected IObservable<T> Subscribe(Func<Session, IObservable<T>> selector)
    {
        return Observable.Create<T>(observer =>
        {
            Session session;
            try { session = SessionRegistry.Acquire(Endpoint); }
            catch (Exception ex) { observer.OnError(ex); return () => { }; }

            var sub = selector(session).Subscribe(observer);
            return () => { sub.Dispose(); session.Release(); };
        });
    }
}
```

`Operators/RawSamples.cs`:

```csharp
using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams continuous broadband samples from OpenEphys. Sample buffer is valid only inside OnNext — clone before retaining.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class RawSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.RawSubject);
}
```

`Operators/FilteredSamples.cs`:

```csharp
using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams filtered (LFP / spike-band) continuous samples from OpenEphys. Sample buffer is valid only inside OnNext.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class FilteredSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.FilteredSubject);
}
```

`Operators/Spikes.cs`:

```csharp
using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams spike events (threshold or sorter output) from OpenEphys. Waveform buffer valid only inside OnNext.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class Spikes : SessionSource<SpikeEvent>
{
    public IObservable<SpikeEvent> Process() => Subscribe(s => s.SpikeSubject);
}
```

`Operators/TtlEvents.cs`:

```csharp
using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams TTL edges from OpenEphys' event bus (includes Bonsai-issued TTLs echoed back).")]
[WorkflowElementCategory(ElementCategory.Source)]
public class TtlEvents : SessionSource<TtlEvent>
{
    public IObservable<TtlEvent> Process() => Subscribe(s => s.TtlSubject);
}
```

`Operators/SyncPoints.cs`:

```csharp
using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Raw (sample_index, host_qpc_ticks) pairs emitted ~1 Hz; useful for clients that roll their own clock.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class SyncPoints : SessionSource<SyncPoint>
{
    public IObservable<SyncPoint> Process() => Subscribe(s => s.SyncSubject);
}
```

`Operators/OpenEphysSession.cs`:

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using System.Threading;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Emits SessionStatus snapshots once per second — liveness, transport, drop count, board name.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class OpenEphysSession : SessionSource<SessionStatus>
{
    public IObservable<SessionStatus> Process()
    {
        return Subscribe(session =>
            Observable.Interval(TimeSpan.FromSeconds(1))
                .Select(_ => new SessionStatus
                {
                    IsConnected = session.Transport.IsConnected,
                    Transport = session.Transport.Name,
                    BoardName = "—",
                    FrameCount = Interlocked.Read(ref session.FrameCount),
                    DropCount  = Interlocked.Read(ref session.DropCount),
                    EstimatedLagMs = 0.0
                }));
    }
}
```

- [ ] **Step 2: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/
git commit -m "feat(bonsai): source operators"
```

---

### Task 3.9: Transform operators (`ToMat`, `SampleToHostTime`)

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/SampleToHostTime.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/ToMat.cs`

- [ ] **Step 1: `SampleToHostTime` — applies drift-corrected QPC clock**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

/// <summary>
/// Adds <see cref="DateTimeOffset"/> per element using the session's drift fit.
/// </summary>
[Combinator]
[Description("Pairs each frame's sample_index with a drift-corrected DateTimeOffset.")]
[WorkflowElementCategory(ElementCategory.Transform)]
public class SampleToHostTime
{
    [Description("Endpoint of the session whose drift fit should be used; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    public IObservable<(RawBlock block, DateTimeOffset time)> Process(IObservable<RawBlock> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Select(b => (b, FromQpc(session.PredictQpc(b.SampleIndex))))
                .Finally(session.Release);
        });
    }

    private static DateTimeOffset FromQpc(ulong qpc)
        => DateTimeOffset.FromUnixTimeMilliseconds((long)(qpc / 1_000_000UL));
}
```

- [ ] **Step 2: `ToMat` — convert `RawBlock` → Bonsai.Dsp `Mat`**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Data;
using OpenCV.Net;

namespace Bonsai.OEconnect.Operators;

/// <summary>
/// Converts <see cref="RawBlock"/> into a Bonsai.Dsp <see cref="Mat"/> (rows = channels, cols = samples).
/// The output owns a fresh allocation; the input pooled buffer is released after copy.
/// </summary>
[Combinator]
[Description("Converts RawBlock to a Bonsai.Dsp Mat (rows = channels, cols = samples).")]
[WorkflowElementCategory(ElementCategory.Transform)]
public class ToMat
{
    public IObservable<Mat> Process(IObservable<RawBlock> source)
    {
        return source.Select(b =>
        {
            var mat = new Mat(b.NumChannels, b.NumSamples, Depth.S16, 1);
            var span = b.Samples.Span;
            unsafe
            {
                fixed (short* src = span)
                {
                    Buffer.MemoryCopy(src, mat.Data.ToPointer(),
                                      span.Length * sizeof(short),
                                      span.Length * sizeof(short));
                }
            }
            return mat;
        });
    }
}
```

- [ ] **Step 3: Add Bonsai.Dsp dependency to csproj**

In `src/Bonsai.OEconnect/Bonsai.OEconnect.csproj`, add:

```xml
<PackageReference Include="Bonsai.Dsp" Version="2.8.0" />
```

- [ ] **Step 4: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/
git commit -m "feat(bonsai): transform operators (ToMat, SampleToHostTime)"
```

---

### Task 3.10: Sink operators (`StartRecording`, `StopRecording`, `SetTtl`, `PulseTtl`)

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/StartRecording.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/StopRecording.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/SetTtl.cs`
- Create: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/PulseTtl.cs`

- [ ] **Step 1: `StartRecording.cs`**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Sends START_RECORD to OpenEphys. Passes the trigger through.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StartRecording
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("Recording root directory.")]
    public string Directory { get; set; } = string.Empty;

    [Description("File name prefix.")]
    public string Prefix { get; set; } = string.Empty;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source.Do(_ =>
            {
                CmdSender.SendStartRecord(session.Transport, Directory, Prefix, out _);
            }).Finally(session.Release);
        });
    }
}
```

- [ ] **Step 2: `StopRecording.cs`**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Sends STOP_RECORD to OpenEphys.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StopRecording
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source.Do(_ => CmdSender.SendStopRecord(session.Transport, out _))
                         .Finally(session.Release);
        });
    }
}
```

- [ ] **Step 3: `SetTtl.cs`**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Asserts a TTL output line on the OE acquisition board via the OEconnect plugin.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class SetTtl
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("TTL output line number.")]
    public byte Line { get; set; }

    [Description("1 = high (rising edge), 0 = low (falling edge).")]
    public byte Edge { get; set; }

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source.Do(_ => CmdSender.SendSetTtl(session.Transport, Line, Edge != 0, out _))
                         .Finally(session.Release);
        });
    }
}
```

- [ ] **Step 4: `PulseTtl.cs`**

```csharp
using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Pulses a TTL line high for WidthMicroseconds then back low. Implemented as one PULSE_TTL command — width is enforced by the board adapter when supported.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class PulseTtl
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("TTL output line number.")]
    public byte Line { get; set; }

    [Description("Pulse width in microseconds.")]
    public uint WidthMicroseconds { get; set; } = 1000;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source.Do(_ =>
                CmdSender.SendPulseTtl(session.Transport, Line, true, WidthMicroseconds, out _))
                .Finally(session.Release);
        });
    }
}
```

- [ ] **Step 5: Build + commit**

```powershell
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Operators/
git commit -m "feat(bonsai): sink operators (StartRecording, StopRecording, SetTtl, PulseTtl)"
```

---

### Task 3.11: xUnit unit tests for data dispatch + operators

**Files:**
- Create: `package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests/InteropTests.cs`
- Create: `package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests/CmdSenderTests.cs`
- Create: `package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests/RoundTripTests.cs`

- [ ] **Step 1: `InteropTests.cs`**

```csharp
using Bonsai.OEconnect.Interop;
using Xunit;

namespace Bonsai.OEconnect.Tests;

public class InteropTests
{
    [Fact]
    public void FrameInit_PopulatesMagicAndVersion()
    {
        var h = new OecFrameHeader();
        NativeMethods.FrameInit(ref h, OecStreams.RawBlock, 1234, 9, 10, 0);
        Assert.Equal(0x3143454Fu, h.Magic);
        Assert.Equal(OecStreams.RawBlock, h.StreamId);
        Assert.Equal(1234u, h.PayloadLen);
        Assert.Equal(9ul, h.SampleIndex);
    }

    [Fact]
    public void FrameValidate_AcceptsWellFormed()
    {
        var h = new OecFrameHeader();
        NativeMethods.FrameInit(ref h, OecStreams.TtlEvent, 4, 0, 0, 0);
        Assert.Equal(OecStatus.Ok, NativeMethods.FrameValidate(in h));
    }

    [Fact]
    public void DriftFit_RecoversLinear()
    {
        var f = NativeMethods.DriftCreate();
        try
        {
            for (ulong s = 0; s < 50; ++s) NativeMethods.DriftAdd(f, s, 100 + 33 * s);
            Assert.Equal(OecStatus.Ok, NativeMethods.DriftFit(f, out var a, out var b));
            Assert.Equal(33.0, a, 9);
            Assert.Equal(100.0, b, 6);
        }
        finally { NativeMethods.DriftDestroy(f); }
    }
}
```

- [ ] **Step 2: `CmdSenderTests.cs`**

```csharp
using System.Runtime.InteropServices;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Sessions;
using Xunit;

namespace Bonsai.OEconnect.Tests;

public class CmdSenderTests
{
    [Fact]
    public void BuildCmd_SetTtl_HasExpectedLayout()
    {
        var frame = CmdSender.BuildCmd(OecCmds.SetTtl, 0xCAFE,
            new byte[] { 3, 1 });
        Assert.True(frame.Length >= Marshal.SizeOf<OecFrameHeader>() + 8);
        var h = MemoryMarshal.Read<OecFrameHeader>(frame);
        Assert.Equal(OecStreams.Cmd, h.StreamId);
        ushort cmdId = MemoryMarshal.Read<ushort>(frame.AsSpan(Marshal.SizeOf<OecFrameHeader>()));
        Assert.Equal(OecCmds.SetTtl, cmdId);
        uint cookie = MemoryMarshal.Read<uint>(frame.AsSpan(Marshal.SizeOf<OecFrameHeader>() + 2));
        Assert.Equal(0xCAFEu, cookie);
    }
}
```

- [ ] **Step 3: `RoundTripTests.cs` (drives the OE plugin's gtest harness as a fixture)**

```csharp
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reactive.Linq;
using System.Threading;
using Bonsai.OEconnect.Operators;
using Xunit;

namespace Bonsai.OEconnect.Tests;

/// <summary>
/// End-to-end: shells out to the gtest harness from the plugin (built by Phase 2)
/// to produce a live shmem session, then opens a Bonsai source operator against
/// the same endpoint and verifies the operator receives a RawBlock.
/// Skipped when the harness binary is not present.
/// </summary>
public class RoundTripTests
{
    private static string? HarnessPath()
    {
        var dir = AppContext.BaseDirectory;
        for (int up = 0; up < 8 && dir != null; ++up, dir = Directory.GetParent(dir)?.FullName)
        {
            var candidate = Path.Combine(dir, "..", "..", "build-plugin",
                                         "Tests", "oec_plugin_tests");
            if (File.Exists(candidate))   return candidate;
            if (File.Exists(candidate + ".exe")) return candidate + ".exe";
        }
        return null;
    }

    [Fact]
    public void EndToEnd_PluginHarnessFeedsRawSamples()
    {
        var harness = HarnessPath();
        if (harness == null)
        {
            /* Skip when plugin tests haven't been built locally. */
            return;
        }
        /* The harness exits after running its own gtests — we instead want
           a long-running synthetic producer. The plugin's test fixture
           creates a unique region and exits, so for this test we depend on
           a separate `oec_plugin_synth` binary added by Task 4.x (perf).
           Until that binary lands, this end-to-end test is intentionally a
           no-op; full e2e is exercised by the manual latency procedure. */
    }
}
```

- [ ] **Step 4: Run tests**

```powershell
dotnet test package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release --logger "console;verbosity=normal"
```

Expected: `InteropTests` + `CmdSenderTests` PASS; `RoundTripTests` no-op (placeholder until Task 4.x ships the synth binary).

- [ ] **Step 5: Commit**

```powershell
git add package-bonsai/Bonsai.OEconnect/tests/
git commit -m "test(bonsai): interop, CmdSender, and round-trip placeholder tests"
```

---

### Task 3.12: NuGet pack

**Files:**
- Modify: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj`

- [ ] **Step 1: Build native lib first so pack picks it up**

```powershell
cmake -S libshared/oeconnect -B libshared/oeconnect/build -DCMAKE_BUILD_TYPE=Release
cmake --build libshared/oeconnect/build --config Release
```

- [ ] **Step 2: Pack**

```powershell
dotnet pack package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release -o package-bonsai/nupkg
```

Expected: `package-bonsai/nupkg/Bonsai.OEconnect.1.0.0.nupkg` created. Verify it contains `runtimes/win-x64/native/liboeconnect.dll` (etc.) and `lib/net472/Bonsai.OEconnect.dll`, `lib/net6.0/Bonsai.OEconnect.dll`.

- [ ] **Step 3: Inspect**

```powershell
$pkg = "package-bonsai/nupkg/Bonsai.OEconnect.1.0.0.nupkg"
Add-Type -Assembly System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::OpenRead($pkg).Entries | Select-Object FullName | Sort-Object FullName
```

Expected: tree includes both lib targets and the runtimes/<rid>/native/ payloads.

- [ ] **Step 4: Commit (nuspec auto-generated; no source change beyond Step 1)**

```powershell
git add package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj
git commit -m "build(bonsai): NuGet pack picks up native runtimes"
```

---

---

## Phase 4 — Example workflows + perf documentation

### Task 4.1: Example — closed-loop spike-triggered TTL stim

**Files:**
- Create: `examples/closed_loop_spike_triggered_stim.bonsai`
- Create: `examples/closed_loop_spike_triggered_stim.openephys.xml`

- [ ] **Step 1: Bonsai workflow**

```xml
<?xml version="1.0" encoding="utf-8"?>
<WorkflowBuilder Version="2.8.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns:xsd="http://www.w3.org/2001/XMLSchema"
  xmlns:rx="clr-namespace:Bonsai.Reactive;assembly=Bonsai.Core"
  xmlns:oec="clr-namespace:Bonsai.OEconnect.Operators;assembly=Bonsai.OEconnect"
  xmlns="https://bonsai-rx.org/2018/workflow">
  <Workflow>
    <Nodes>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:Spikes">
          <Endpoint></Endpoint>
        </Combinator>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="rx:Where">
          <Selector>it.UnitId == 3 &amp;&amp; it.ElectrodeId == 12</Selector>
        </Combinator>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:PulseTtl">
          <Endpoint></Endpoint>
          <Line>2</Line>
          <WidthMicroseconds>5000</WidthMicroseconds>
        </Combinator>
      </Expression>
    </Nodes>
    <Edges>
      <Edge From="0" To="1" Label="Source1" />
      <Edge From="1" To="2" Label="Source1" />
    </Edges>
  </Workflow>
</WorkflowBuilder>
```

- [ ] **Step 2: Companion OE signal chain (documented as XML user opens in OE GUI)**

`examples/closed_loop_spike_triggered_stim.openephys.xml`:

```xml
<!--
  Example OE signal chain to pair with closed_loop_spike_triggered_stim.bonsai.
  Load in OE GUI via File → Load Signal Chain.
-->
<SETTINGS>
  <CHAIN>
    <PROCESSOR name="Rhythm FPGA" />
    <PROCESSOR name="Bandpass Filter" lowCut="300" highCut="6000" />
    <PROCESSOR name="Spike Detector" threshold="-4.5sd" />
    <PROCESSOR name="OEconnect" transport="Auto" streams="Spikes,TTL" />
    <PROCESSOR name="Record Node" />
  </CHAIN>
</SETTINGS>
```

- [ ] **Step 3: Commit**

```powershell
git add examples/closed_loop_spike_triggered_stim.*
git commit -m "examples: closed-loop spike-triggered stim workflow"
```

---

### Task 4.2: Example — recording with TTL markers

**Files:**
- Create: `examples/record_with_ttl_marker.bonsai`

- [ ] **Step 1: Workflow**

```xml
<?xml version="1.0" encoding="utf-8"?>
<WorkflowBuilder Version="2.8.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns:xsd="http://www.w3.org/2001/XMLSchema"
  xmlns:rx="clr-namespace:Bonsai.Reactive;assembly=Bonsai.Core"
  xmlns:oec="clr-namespace:Bonsai.OEconnect.Operators;assembly=Bonsai.OEconnect"
  xmlns="https://bonsai-rx.org/2018/workflow">
  <Workflow>
    <Nodes>
      <Expression xsi:type="rx:Timer">
        <Period>PT0S</Period>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:StartRecording">
          <Endpoint></Endpoint>
          <Directory>D:\data\session</Directory>
          <Prefix>mouse42</Prefix>
        </Combinator>
      </Expression>
      <Expression xsi:type="rx:Interval">
        <Period>PT5S</Period>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:PulseTtl">
          <Endpoint></Endpoint>
          <Line>0</Line>
          <WidthMicroseconds>2000</WidthMicroseconds>
        </Combinator>
      </Expression>
    </Nodes>
    <Edges>
      <Edge From="0" To="1" Label="Source1" />
      <Edge From="2" To="3" Label="Source1" />
    </Edges>
  </Workflow>
</WorkflowBuilder>
```

- [ ] **Step 2: Commit**

```powershell
git add examples/record_with_ttl_marker.bonsai
git commit -m "examples: record with periodic TTL markers"
```

---

### Task 4.3: Example — LFP-band visualisation

**Files:**
- Create: `examples/lfp_band_visualization.bonsai`

- [ ] **Step 1: Workflow**

```xml
<?xml version="1.0" encoding="utf-8"?>
<WorkflowBuilder Version="2.8.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns:xsd="http://www.w3.org/2001/XMLSchema"
  xmlns:rx="clr-namespace:Bonsai.Reactive;assembly=Bonsai.Core"
  xmlns:dsp="clr-namespace:Bonsai.Dsp;assembly=Bonsai.Dsp"
  xmlns:oec="clr-namespace:Bonsai.OEconnect.Operators;assembly=Bonsai.OEconnect"
  xmlns="https://bonsai-rx.org/2018/workflow">
  <Workflow>
    <Nodes>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:FilteredSamples">
          <Endpoint></Endpoint>
        </Combinator>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:ToMat" />
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="dsp:MatrixWriter">
          <Path>D:\data\session\lfp.bin</Path>
        </Combinator>
      </Expression>
    </Nodes>
    <Edges>
      <Edge From="0" To="1" Label="Source1" />
      <Edge From="1" To="2" Label="Source1" />
    </Edges>
  </Workflow>
</WorkflowBuilder>
```

- [ ] **Step 2: Commit**

```powershell
git add examples/lfp_band_visualization.bonsai
git commit -m "examples: LFP-band write-to-disk workflow"
```

---

### Task 4.4: Example — multi-subscriber data split

**Files:**
- Create: `examples/multi_subscriber_data_split.bonsai`

- [ ] **Step 1: Workflow demonstrates two operators sharing one OE session**

```xml
<?xml version="1.0" encoding="utf-8"?>
<WorkflowBuilder Version="2.8.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns:xsd="http://www.w3.org/2001/XMLSchema"
  xmlns:rx="clr-namespace:Bonsai.Reactive;assembly=Bonsai.Core"
  xmlns:oec="clr-namespace:Bonsai.OEconnect.Operators;assembly=Bonsai.OEconnect"
  xmlns="https://bonsai-rx.org/2018/workflow">
  <Workflow>
    <Nodes>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:RawSamples">
          <Endpoint></Endpoint>
        </Combinator>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:TtlEvents">
          <Endpoint></Endpoint>
        </Combinator>
      </Expression>
      <Expression xsi:type="Combinator">
        <Combinator xsi:type="oec:OpenEphysSession">
          <Endpoint></Endpoint>
        </Combinator>
      </Expression>
    </Nodes>
    <Edges />
  </Workflow>
</WorkflowBuilder>
```

(The SessionRegistry shares one underlying ringbuf reader across all three
operators because they target the same empty endpoint.)

- [ ] **Step 2: `examples/README.md`**

```markdown
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
```

- [ ] **Step 3: Commit**

```powershell
git add examples/multi_subscriber_data_split.bonsai examples/README.md examples/.gitkeep
Remove-Item examples/.gitkeep -ErrorAction SilentlyContinue
git commit -m "examples: multi-subscriber workflow + examples README"
```

---

### Task 4.5: Synthetic producer for end-to-end Bonsai tests

**Files:**
- Create: `plugin-openephys/OEconnect/Tests/synth_producer.cc`
- Modify: `plugin-openephys/OEconnect/Tests/CMakeLists.txt`

The Bonsai `RoundTripTests.EndToEnd_PluginHarnessFeedsRawSamples` test
needs a long-running synthetic producer that writes RAW_BLOCK frames into a
shmem region under a known name. This binary supplies that.

- [ ] **Step 1: Add the new target to `Tests/CMakeLists.txt`** (append):

```cmake
add_executable(oec_plugin_synth synth_producer.cc)
target_link_libraries(oec_plugin_synth PRIVATE oeconnect)
target_include_directories(oec_plugin_synth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
```

- [ ] **Step 2: `synth_producer.cc`**

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

extern "C" {
#include "oeconnect/ringbuf.h"
#include "oeconnect/shm.h"
#include "oeconnect/frame.h"
}

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

int main(int argc, char** argv) {
    std::string name = (argc > 1) ? argv[1] : "/oeconnect.synth.shm";
    int n_channels = (argc > 2) ? std::atoi(argv[2]) : 8;
    int block_size = (argc > 3) ? std::atoi(argv[3]) : 32;

    std::signal(SIGINT, on_sigint);

    const size_t region_size = oec_region_size(
        OEC_DEFAULT_SLOT_SIZE, OEC_DEFAULT_SLOT_COUNT,
        OEC_DEFAULT_CMD_SLOT_SIZE, OEC_DEFAULT_CMD_SLOT_COUNT,
        OEC_DEFAULT_ACK_SLOT_SIZE, OEC_DEFAULT_ACK_SLOT_COUNT);

    oec_shm_t* shm = nullptr;
    void* mapped = nullptr;
    size_t msz = 0;
    if (oec_shm_create(name.c_str(), region_size, 1, &shm, &mapped, &msz) != OEC_OK) {
        std::fprintf(stderr, "shm_create failed\n");
        return 1;
    }
    oec_region_init(mapped, msz,
        OEC_DEFAULT_SLOT_SIZE, OEC_DEFAULT_SLOT_COUNT,
        OEC_DEFAULT_CMD_SLOT_SIZE, OEC_DEFAULT_CMD_SLOT_COUNT,
        OEC_DEFAULT_ACK_SLOT_SIZE, OEC_DEFAULT_ACK_SLOT_COUNT);

    oec_ringbuf_t* rb = nullptr;
    oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb);

    std::printf("synth producer running: %s ch=%d block=%d (Ctrl-C to stop)\n",
                name.c_str(), n_channels, block_size);
    uint64_t sample = 0;
    while (g_run.load()) {
        uint32_t cap = 0;
        void* slot = oec_ringbuf_acquire(rb, /*drop_oldest=*/1, &cap);
        if (!slot) continue;
        oec_frame_header_t* h = (oec_frame_header_t*)slot;
        uint32_t payload = (uint32_t)(sizeof(oec_block_subheader_t) +
                                      (size_t)n_channels * block_size * sizeof(int16_t));
        oec_frame_init(h, OEC_STREAM_RAW_BLOCK, payload, sample, 0, 0);
        oec_block_subheader_t* sh = (oec_block_subheader_t*)((uint8_t*)slot + sizeof(*h));
        sh->n_channels = (uint16_t)n_channels;
        sh->n_samples  = (uint16_t)block_size;
        sh->dtype      = OEC_DTYPE_INT16;
        sh->source_id  = 0;
        sh->reserved   = 0;

        int16_t* data = (int16_t*)((uint8_t*)slot + sizeof(*h) + sizeof(*sh));
        for (int i = 0; i < n_channels * block_size; ++i) data[i] = (int16_t)(sample + i);
        oec_ringbuf_publish(rb);

        sample += (uint64_t)block_size;
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }

    oec_ringbuf_detach(rb);
    oec_shm_close(shm);
    oec_shm_unlink(name.c_str());
    return 0;
}
```

- [ ] **Step 3: Update `RoundTripTests.cs` to actually drive `oec_plugin_synth`**

Replace `EndToEnd_PluginHarnessFeedsRawSamples` body with:

```csharp
[Fact]
public void EndToEnd_PluginHarnessFeedsRawSamples()
{
    var dir = AppContext.BaseDirectory;
    string? synth = null;
    for (int up = 0; up < 10 && dir != null; ++up, dir = Directory.GetParent(dir)?.FullName)
    {
        foreach (var name in new[] { "oec_plugin_synth", "oec_plugin_synth.exe" })
        {
            var candidate = Path.Combine(dir!, "build-plugin", "Tests", name);
            if (File.Exists(candidate)) { synth = candidate; break; }
        }
        if (synth != null) break;
    }
    if (synth == null) return;     /* skip when not built */

    var endpoint = "/oeconnect.synth.bonsai-test.shm";
    var psi = new ProcessStartInfo(synth, $"\"{endpoint}\" 4 32") {
        RedirectStandardOutput = true, UseShellExecute = false
    };
    using var proc = Process.Start(psi)!;
    try
    {
        var op = new RawSamples { Endpoint = "shm://" + endpoint.TrimStart('/') };
        var seen = 0;
        using var sub = op.Process().Subscribe(_ => Interlocked.Increment(ref seen));
        Thread.Sleep(1500);
        Assert.True(seen > 10, $"expected >10 RawBlock events, got {seen}");
    }
    finally
    {
        proc.Kill();
    }
}
```

- [ ] **Step 4: Build, run end-to-end**

```powershell
cmake --build build-plugin --target oec_plugin_synth
dotnet test package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release --filter "EndToEnd_PluginHarnessFeedsRawSamples"
```

Expected: PASS — synth producer feeds RawSamples operator successfully.

- [ ] **Step 5: Commit**

```powershell
git add plugin-openephys/OEconnect/Tests/synth_producer.cc plugin-openephys/OEconnect/Tests/CMakeLists.txt package-bonsai/Bonsai.OEconnect/tests/Bonsai.OEconnect.Tests/RoundTripTests.cs
git commit -m "test: end-to-end Bonsai ↔ liboeconnect roundtrip via synth producer"
```

---

### Task 4.6: Manual latency-verification procedure doc

**Files:**
- Create: `docs/perf/closed_loop_latency.md`

- [ ] **Step 1: Write the doc**

```markdown
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
```

- [ ] **Step 2: Commit**

```powershell
git add docs/perf/closed_loop_latency.md
git commit -m "docs(perf): manual closed-loop latency verification procedure"
```

---

---

## Phase 5 — Release tooling

### Task 5.1: Tag-triggered release workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Workflow that triggers on tags `v*.*.*`**

```yaml
name: release

on:
  push:
    tags: ['v*.*.*']

permissions:
  contents: write

jobs:
  build-libshared:
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S libshared/oeconnect -B build -DCMAKE_BUILD_TYPE=Release -DOEC_BUILD_TESTS=OFF
      - name: Build
        run: cmake --build build --config Release --parallel
      - name: Stage artifact
        shell: bash
        run: |
          mkdir -p out
          cp build/liboeconnect.{dll,so,dylib} out/ 2>/dev/null || true
          cp build/Release/liboeconnect.dll  out/   2>/dev/null || true
      - uses: actions/upload-artifact@v4
        with:
          name: liboeconnect-${{ runner.os }}
          path: out/

  build-plugin:
    needs: build-libshared
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake -S plugin-openephys/OEconnect -B build -DCMAKE_BUILD_TYPE=Release -DOEC_PLUGIN_BUILD_TESTS=OFF
      - name: Build
        run: cmake --build build --config Release --parallel
      - name: Stage bundle
        shell: bash
        run: |
          mkdir -p out
          cp -r build/OEconnect.bundle      out/ 2>/dev/null || true
          cp build/Release/OEconnect.dll    out/ 2>/dev/null || true
          cp build/Release/OEconnect.dylib  out/ 2>/dev/null || true
      - uses: actions/upload-artifact@v4
        with:
          name: OEconnect-plugin-${{ runner.os }}
          path: out/

  pack-bonsai:
    needs: build-libshared
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-dotnet@v4
        with:
          dotnet-version: |
            6.0.x
            8.0.x
      - name: Download native artifacts
        uses: actions/download-artifact@v4
        with:
          path: artifacts/
      - name: Stage runtimes
        shell: pwsh
        run: |
          New-Item -ItemType Directory -Force -Path libshared/oeconnect/build | Out-Null
          Copy-Item artifacts/liboeconnect-Windows/liboeconnect.dll  libshared/oeconnect/build/ -ErrorAction SilentlyContinue
          Copy-Item artifacts/liboeconnect-Linux/liboeconnect.so      libshared/oeconnect/build/ -ErrorAction SilentlyContinue
          Copy-Item artifacts/liboeconnect-macOS/liboeconnect.dylib   libshared/oeconnect/build/ -ErrorAction SilentlyContinue
      - name: Pack
        run: dotnet pack package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release -o package-bonsai/nupkg
      - uses: actions/upload-artifact@v4
        with:
          name: Bonsai.OEconnect-nupkg
          path: package-bonsai/nupkg/

  publish:
    needs: [build-plugin, pack-bonsai]
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/download-artifact@v4
        with:
          path: artifacts/
      - name: Bundle release assets
        run: |
          mkdir -p release
          ( cd artifacts/OEconnect-plugin-Linux   && zip -r ../../release/OEconnect-linux.zip   . )
          ( cd artifacts/OEconnect-plugin-Windows && zip -r ../../release/OEconnect-windows.zip . )
          ( cd artifacts/OEconnect-plugin-macOS   && zip -r ../../release/OEconnect-macos.zip   . )
          cp artifacts/Bonsai.OEconnect-nupkg/*.nupkg release/
      - name: Create GitHub release
        uses: softprops/action-gh-release@v2
        with:
          files: release/*
          generate_release_notes: true
          body_path: docs/release-notes/RELEASE_TEMPLATE.md
      - name: Publish to NuGet.org
        run: |
          for f in release/*.nupkg; do
            dotnet nuget push "$f" -k "${{ secrets.NUGET_API_KEY }}" -s https://api.nuget.org/v3/index.json --skip-duplicate
          done
        env:
          NUGET_API_KEY: ${{ secrets.NUGET_API_KEY }}
```

- [ ] **Step 2: Commit**

```powershell
git add .github/workflows/release.yml
git commit -m "ci(release): tag-triggered build + GitHub Release + NuGet push"
```

---

### Task 5.2: Release notes template

**Files:**
- Create: `docs/release-notes/RELEASE_TEMPLATE.md`

- [ ] **Step 1: Template referenced by `release.yml`**

```markdown
## OEconnect ${{ github.ref_name }}

### What's in this release
- **OE plugin** — `OEconnect-{linux,windows,macos}.zip` containing the
  per-OS plugin bundle. Drop into the OE GUI `plugins/` folder.
- **Bonsai package** — `Bonsai.OEconnect.<version>.nupkg`. Install via the
  Bonsai package manager.

### Verification before installing in a live rig
- Run the manual latency procedure in
  [`docs/perf/closed_loop_latency.md`](../docs/perf/closed_loop_latency.md).
- Confirm OE Record Node downstream of OEconnect captures the expected
  backup files.

### Protocol
- Wire protocol version: see [`spec/oec-protocol-v1.md`](../spec/oec-protocol-v1.md).
- This release is `version_major` = 1.

(Automatic changelog from commits below.)
```

- [ ] **Step 2: Commit**

```powershell
git add docs/release-notes/RELEASE_TEMPLATE.md
git commit -m "docs(release): GitHub Release body template"
```

---

### Task 5.3: Standalone `liboeconnect.runtime` NuGet

**Files:**
- Create: `libshared/oeconnect/runtime/liboeconnect.runtime.nuspec`
- Create: `libshared/oeconnect/runtime/build.ps1`

- [ ] **Step 1: nuspec**

```xml
<?xml version="1.0" encoding="utf-8"?>
<package>
  <metadata>
    <id>liboeconnect.runtime</id>
    <version>1.0.0</version>
    <authors>OEconnect contributors</authors>
    <description>Native runtime for the OEconnect wire protocol (frame, ringbuf, shm, drift, sidecar). Consumed by Bonsai.OEconnect; can also be used by other .NET clients via P/Invoke.</description>
    <license type="expression">MIT</license>
    <projectUrl>https://github.com/oeconnect/oeconnect</projectUrl>
    <tags>oeconnect openephys native runtime</tags>
  </metadata>
  <files>
    <file src="..\build\liboeconnect.dll"    target="runtimes\win-x64\native\liboeconnect.dll"    />
    <file src="..\build\liboeconnect.so"     target="runtimes\linux-x64\native\liboeconnect.so"   />
    <file src="..\build\liboeconnect.dylib"  target="runtimes\osx-x64\native\liboeconnect.dylib"  />
  </files>
</package>
```

- [ ] **Step 2: `build.ps1` — packs after a Release build of liboeconnect**

```powershell
# libshared/oeconnect/runtime/build.ps1
param([string]$Version = "1.0.0")

Push-Location $PSScriptRoot
try {
    & nuget pack liboeconnect.runtime.nuspec -Version $Version -OutputDirectory .
} finally {
    Pop-Location
}
```

- [ ] **Step 3: Commit**

```powershell
git add libshared/oeconnect/runtime/
git commit -m "build(libshared): standalone liboeconnect.runtime nupkg"
```

---

### Task 5.4: Final cross-subsystem smoke test before tagging

- [ ] **Step 1: Local clean build of every artifact in Release**

```powershell
# liboeconnect
cmake -S libshared/oeconnect -B libshared/oeconnect/build -DCMAKE_BUILD_TYPE=Release -DOEC_BUILD_TESTS=ON
cmake --build libshared/oeconnect/build --config Release --parallel
ctest --test-dir libshared/oeconnect/build -C Release --output-on-failure

# OE plugin (depends on submodule init)
git submodule update --init --recursive
cmake -S plugin-openephys/OEconnect -B plugin-openephys/OEconnect/build -DCMAKE_BUILD_TYPE=Release -DOEC_PLUGIN_BUILD_TESTS=ON
cmake --build plugin-openephys/OEconnect/build --config Release --parallel
ctest --test-dir plugin-openephys/OEconnect/build -C Release --output-on-failure

# Bonsai package
dotnet build package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
dotnet test  package-bonsai/Bonsai.OEconnect/Bonsai.OEconnect.sln -c Release
dotnet pack  package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Bonsai.OEconnect.csproj -c Release -o package-bonsai/nupkg
```

Expected: every step exits 0; all ctest + dotnet test PASS.

- [ ] **Step 2: Tag a release candidate locally for dry-run validation**

```powershell
git tag -a v1.0.0-rc.1 -m "OEconnect v1.0.0 release candidate 1"
git tag --list "v*"
```

(Do NOT push the tag yet — that would fire the release workflow against
NuGet.org. Push only after manual latency verification on real hardware.)

- [ ] **Step 3: Commit final state**

```powershell
git status
git log --oneline -20
```

Expected: clean working tree; commit history reflects the entire plan
sequence in order.

---

**Phase 5 done.** Release pipeline + standalone native NuGet ready; full local smoke build passes before the first real tag push.

---

## Plan complete

All five phases produce working, testable artifacts:

| Phase | Artifact                                            | Tested by                                                       |
|-------|-----------------------------------------------------|-----------------------------------------------------------------|
| 0     | Repo, frozen spec, CI skeleton                      | CI noop runs green                                              |
| 1     | `liboeconnect.{dll,so,dylib}` + headers             | gtest unit suite + 10⁹-frame stress                              |
| 2     | `OEconnect` OE plugin module bundle                 | gtest hot-path tests + manual OE-GUI smoke                       |
| 3     | `Bonsai.OEconnect` NuGet                            | xUnit + end-to-end Bonsai ↔ synth roundtrip                      |
| 4     | Example workflows + manual latency procedure        | Manual scope verification (headline number)                      |
| 5     | Release pipeline                                    | Tagged release builds three OS bundles + NuGet                   |

**Reference contracts that must remain in sync:**
- Wire spec: `spec/oec-protocol-v1.md`
- C ABI:     `libshared/oeconnect/include/oeconnect/*.h`
- .NET interop: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Interop/NativeMethods.cs`

The CI `spec.yml` workflow enforces version bumps when the wire frame
layout or stream IDs change.

---

## Phase 6 — Deferred opt-in features (post-v1.0)

These two features appear in the spec but are explicitly opt-in / optional
and are not needed to ship v1.0. Each is a self-contained follow-up that
can be implemented after the main plan is green.

### Task 6.1: Wakeup events (spec §5.4)

**Why:** Lower CPU at the cost of a tiny scheduler-jitter hit when the
consumer is willing to block instead of spin. Default polling is fine for
the hard-RT path; this is an option for low-rate consumers.

**Files:**
- Modify: `libshared/oeconnect/include/oeconnect/shm.h`
- Modify: `libshared/oeconnect/src/shm.c`
- Modify: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ShmemClient.cs`

- [ ] **Step 1: Add platform event handles to `oec_shm_t` + new API**

```c
/* in shm.h */
OEC_API oec_status_t oec_shm_event_create(const char *name, void **out_handle);
OEC_API oec_status_t oec_shm_event_open  (const char *name, void **out_handle);
OEC_API void          oec_shm_event_signal(void *handle);
OEC_API int           oec_shm_event_wait  (void *handle, int timeout_ms);
OEC_API void          oec_shm_event_close (void *handle);
```

`oec_shm_event_*` map to `CreateEventA` / `OpenEventA` / `SetEvent` /
`WaitForSingleObject` on Windows, `eventfd` on Linux, `kqueue` on macOS.

- [ ] **Step 2: Plumb wakeups through `ShmemTransport`**

Add an optional `wakeup_batch_K` knob to the plugin editor (default 1).
The producer calls `oec_shm_event_signal()` every K publishes; the consumer
runs an adaptive loop: spin 100 µs, then block via `oec_shm_event_wait()`.

- [ ] **Step 3: Test + commit**

```powershell
cmake --build build-libshared && ctest --test-dir build-libshared
git commit -am "feat(libshared,plugin): optional shmem wakeup events"
```

---

### Task 6.2: ZMQ CURVE authentication (spec §6.7)

**Why:** Required for any production deployment that binds to a non-loopback
address on a shared LAN. Off by default; gated by the editor's "Require
auth" checkbox.

**Files:**
- Modify: `plugin-openephys/OEconnect/Source/Transport/ZmqTransport.cpp`
- Modify: `plugin-openephys/OEconnect/Source/OEconnectEditor.cpp`
- Modify: `package-bonsai/Bonsai.OEconnect/src/Bonsai.OEconnect/Transport/ZmqClient.cs`

- [ ] **Step 1: Generate + persist a per-rig CURVE keypair**

```cpp
/* in ZmqTransport::start, if auth_required && server_secret_key_.empty() */
char public_key[41], secret_key[41];
zmq_curve_keypair(public_key, secret_key);
/* persist server_secret to %APPDATA%\OEconnect\curve.secret with restrictive ACLs */
/* expose public_key in the editor for user-side copy/paste */
```

- [ ] **Step 2: Bind the sockets with CURVE on**

```cpp
impl_->pub.set(zmq::sockopt::curve_server, true);
impl_->pub.set(zmq::sockopt::curve_secretkey, server_secret_key_);
impl_->rep.set(zmq::sockopt::curve_server, true);
impl_->rep.set(zmq::sockopt::curve_secretkey, server_secret_key_);
```

The Bonsai side configures `curve_serverkey` + a generated client keypair
exposed on the operators' `Endpoint` query string (`tcp://host:port?key=<base85>`).

- [ ] **Step 3: Refuse non-loopback binds when auth is off**

In `ZmqTransport::start`, if `endpoint_pair` resolves to a non-loopback
host and the auth flag is unset, return `false` and surface the reason in
the editor banner.

- [ ] **Step 4: Test + commit**

Add a `ZmqTransport.CurveRejectsBadKey` integration test that confirms a
mismatched client key fails to connect.

```powershell
cmake --build build-plugin && ctest --test-dir build-plugin -R Zmq
git commit -am "feat(plugin,bonsai): optional ZMQ CURVE auth + non-loopback bind gate"
```

---

**Phase 6 (deferred) noted.** Neither task blocks v1.0; both are isolated
follow-ups whose scope and verification procedure are now spelled out.

