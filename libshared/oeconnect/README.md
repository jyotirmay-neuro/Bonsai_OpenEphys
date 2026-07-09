# liboeconnect

C ABI shared library implementing the OEconnect wire protocol: frame header
encoding/decoding, shared-memory SPSC ring buffers, the dual-clock drift fit,
session sidecar discovery, and the HELLO handshake.

Linked **statically** into the OE plugin; consumed via **P/Invoke** by
`Bonsai.OEconnect`, which ships a copy of the shared library inside its NuGet
package.

Authoritative wire format: the frozen OEconnect v1 protocol
(`OEC_PROTOCOL_VERSION_{MAJOR,MINOR}` in `include/oeconnect/version.h`).

## Build

```bash
cmake -S . -B build -DOEC_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Outputs (Windows): `build/Release/oeconnect.dll` plus `oeconnect.lib`.
Outputs (POSIX): `build/liboeconnect.{so,dylib}` plus a static archive.

`Bonsai.OEconnect.csproj` reads the DLL from `build/Release/oeconnect.dll`, so
build into that default directory before packing the Bonsai package.

Options:

| Option | Default | Meaning |
|---|---|---|
| `OEC_BUILD_TESTS` | `OFF` | Build the GoogleTest suite (33 tests) |
| `OEC_BUILD_SHARED` | `ON` | Build a shared library; `OFF` yields static only |

Compiled with `/W4 /WX` (MSVC) or `-Wall -Wextra -Werror -Wpedantic`.

## API surface

Headers under `include/oeconnect/`. C11.

| Header | Provides |
|---|---|
| `frame.h` | Frame header layout, stream/command/ack ids, CRC-16 |
| `ringbuf.h` | Region layout, SPSC ring attach/acquire/publish/peek/consume |
| `shm.h` | Named shared-memory create/open/close, cross-platform |
| `drift.h` | 60-point least-squares sample↔clock fit |
| `sidecar.h` | Session-discovery JSON read/write |
| `hello.h` | Protocol version handshake |
| `types.h`, `version.h` | Status codes, protocol/library versions |

## Dependencies

- libc, and on POSIX: `librt` for `shm_open` and **`pthread`** — the drift fit
  is mutex-guarded because the reader thread adds points while operator threads
  read the fit. On Windows this uses `CRITICAL_SECTION` and needs no extra link.

## Notes on the ring buffer

Single-producer / single-consumer, Vyukov-style, wait-free on the hot path. The
producer may evict the oldest slot when the ring is full (`drop_oldest`); that
eviction uses a compare-and-swap so it cannot lose-update a concurrent consumer.

`oec_region_open()` validates that the header's advertised slot geometry actually
fits the mapping before any ring is attached — a stale or hostile region cannot
hand out out-of-bounds offsets.
