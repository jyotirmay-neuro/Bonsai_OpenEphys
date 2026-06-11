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
