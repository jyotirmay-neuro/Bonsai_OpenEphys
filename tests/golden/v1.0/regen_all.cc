/* tests/golden/v1.0/regen_all.cc
 *
 * One-shot encoder that regenerates the v1.0 golden wire corpus (every stream
 * except HELLO, which is owned by regen_hello.cc). Linked against liboeconnect
 * so the 32-byte header is produced by the real `oec_frame_init`.
 *
 * Canonical fixed inputs (so the bytes are reproducible): sample_index = 1000,
 * host_qpc_ticks = 2000, flags = 0, line = 3, edge = 1.
 *
 * Build + run (wired into CMake as target `regen_all`):
 *
 *   cmake --build build-libshared --target regen_all
 *   ./build-libshared/Debug/regen_all.exe tests/golden/v1.0/
 *
 * Future intentional wire changes regenerate in the SAME commit that bumps
 * OEC_VERSION_MINOR / _MAJOR in version.h and updates ci/known-good.txt.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "oeconnect/frame.h"
}

namespace {

constexpr uint64_t kSampleIndex = 1000;
constexpr uint64_t kHostQpcTicks = 2000;

/* Append `len` bytes of `src` to `buf` at `*off`. */
void put(uint8_t *buf, size_t *off, const void *src, size_t len) {
    std::memcpy(buf + *off, src, len);
    *off += len;
}

/* Write header(32B) + body to <dir>/<name>. Returns false on I/O error. */
bool emit(const std::string &dir, const char *name, uint16_t stream_id,
          const uint8_t *body, size_t body_len) {
    oec_frame_header_t h;
    oec_frame_init(&h, stream_id, static_cast<uint32_t>(body_len),
                   kSampleIndex, kHostQpcTicks, 0);

    std::string path = dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += name;

    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    std::fwrite(&h, 1, sizeof(h), f);
    if (body_len) std::fwrite(body, 1, body_len, f);
    std::fclose(f);
    std::printf("wrote %s (%zu B payload)\n", name, body_len);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    const std::string dir = (argc > 1) ? argv[1] : ".";
    bool ok = true;
    uint8_t buf[256];
    size_t n;

    /* RAW_BLOCK (0x0001): block_subheader(8) + int16[n_samples * n_channels] chan-major. */
    {
        n = 0;
        oec_block_subheader_t sub;
        std::memset(&sub, 0, sizeof(sub));
        sub.n_channels = 4;
        sub.n_samples  = 2;
        sub.dtype      = OEC_DTYPE_INT16;
        sub.source_id  = 0;
        sub.reserved   = 0;
        put(buf, &n, &sub, sizeof(sub));
        for (uint16_t c = 0; c < sub.n_channels; ++c)
            for (uint16_t s = 0; s < sub.n_samples; ++s) {
                int16_t v = static_cast<int16_t>(c * 10 + s);
                put(buf, &n, &v, sizeof(v));
            }
        ok &= emit(dir, "raw_block.bin", OEC_STREAM_RAW_BLOCK, buf, n);
    }

    /* TTL_EVENT (0x0004): {line_u8, edge_u8, board_id_u8, _pad_u8}. */
    {
        n = 0;
        uint8_t line = 3, edge = 1, board_id = 0, pad = 0;
        put(buf, &n, &line, 1); put(buf, &n, &edge, 1);
        put(buf, &n, &board_id, 1); put(buf, &n, &pad, 1);
        ok &= emit(dir, "ttl_event.bin", OEC_STREAM_TTL_EVENT, buf, n);
    }

    /* SPIKE (0x0003): {electrode_u16, unit_u16, threshold_f32, waveform_int16[N]}. */
    {
        n = 0;
        uint16_t electrode = 7, unit = 1;
        float threshold = 50.0f;
        put(buf, &n, &electrode, 2); put(buf, &n, &unit, 2);
        put(buf, &n, &threshold, 4);
        for (int16_t w = 1; w <= 4; ++w) { int16_t v = static_cast<int16_t>(w * 10); put(buf, &n, &v, 2); }
        ok &= emit(dir, "spike.bin", OEC_STREAM_SPIKE, buf, n);
    }

    /* SYNC (0x0010): {qpc_freq_hz_u64, fpga_sample_rate_hz_f64, stream_meta[0]}. */
    {
        n = 0;
        uint64_t qpc_freq_hz = 10000000ull;
        double fpga_sample_rate_hz = 30000.0;
        put(buf, &n, &qpc_freq_hz, 8); put(buf, &n, &fpga_sample_rate_hz, 8);
        ok &= emit(dir, "sync.bin", OEC_STREAM_SYNC, buf, n);
    }

    /* CMD SET_TTL (0x0020): {cmd_id_u16, cookie_u32, body={line_u8, edge_u8}}. */
    {
        n = 0;
        uint16_t cmd_id = OEC_CMD_SET_TTL;
        uint32_t cookie = 42;
        uint8_t line = 3, edge = 1;
        put(buf, &n, &cmd_id, 2); put(buf, &n, &cookie, 4);
        put(buf, &n, &line, 1); put(buf, &n, &edge, 1);
        ok &= emit(dir, "cmd_set_ttl.bin", OEC_STREAM_CMD, buf, n);
    }

    /* ACK OK (0x0021): {cookie_u32, status_u16}. */
    {
        n = 0;
        uint32_t cookie = 42;
        uint16_t status = OEC_ACK_OK;
        put(buf, &n, &cookie, 4); put(buf, &n, &status, 2);
        ok &= emit(dir, "ack_ok.bin", OEC_STREAM_ACK, buf, n);
    }

    return ok ? 0 : 1;
}
