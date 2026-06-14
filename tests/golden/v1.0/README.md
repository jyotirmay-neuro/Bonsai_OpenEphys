# Golden wire corpus — protocol v1.0 streams

Canonical, byte-frozen frames for every wire stream. Both runtimes decode the
same files:

- C/gtest: `GoldenCorpus.EveryFrameDecodesAndValidates` (`libshared/oeconnect/tests/test_frame.cc`)
- .NET/xUnit: `InteropTests.GoldenCorpus_EveryFrameDecodesAndValidates`

Each file is `oec_frame_header` (32 B) + payload. The decode tests assert the
header passes `oec_frame_validate`, that `stream_id` matches the filename, and
that `payload_len` matches the on-disk byte count.

| File | stream_id | Payload |
|---|---|---|
| `raw_block.bin`   | `0x0001` RAW_BLOCK | block_subheader(8) + int16[4ch × 2smp] |
| `ttl_event.bin`   | `0x0004` TTL_EVENT | `{line=3, edge=1, board_id=0, pad=0}` |
| `spike.bin`       | `0x0003` SPIKE     | `{electrode=7, unit=1, threshold=50.0f, waveform[4]}` |
| `sync.bin`        | `0x0010` SYNC      | `{qpc_freq_hz=10e6, fpga_sample_rate_hz=30000.0}` |
| `cmd_set_ttl.bin` | `0x0020` CMD       | `{cmd_id=SET_TTL, cookie=42, line=3, edge=1}` |
| `ack_ok.bin`      | `0x0021` ACK       | `{cookie=42, status=OK}` |
| `hello.bin`       | `0x0030` HELLO     | see `regen_hello.cc` (owned separately) |

Fixed header inputs for all `regen_all` frames: `sample_index = 1000`,
`host_qpc_ticks = 2000`, `flags = 0`.

## Do NOT edit these files by hand

Regenerate via `regen_all.cc` (and `regen_hello.cc` for `hello.bin`) only as
part of a **deliberate** protocol change, and in the SAME commit:

```powershell
cmake --build build-libshared --target regen_all
./build-libshared/tests/Debug/regen_all.exe tests/golden/v1.0/
```

Then bump `OEC_VERSION_MINOR` (or `MAJOR`) in
`libshared/oeconnect/include/oeconnect/version.h` and update the matching hash
in `ci/known-good.txt`.
