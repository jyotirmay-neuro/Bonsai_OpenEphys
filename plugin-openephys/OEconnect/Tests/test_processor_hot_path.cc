#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include "OEconnectProcessor.h"
#include "Transport/ShmemTransport.h"
#include "Boards/FileReaderAdapter.h"

extern "C" {
#include "oeconnect/ringbuf.h"
#include "oeconnect/shm.h"
#include "oeconnect/version.h"
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
