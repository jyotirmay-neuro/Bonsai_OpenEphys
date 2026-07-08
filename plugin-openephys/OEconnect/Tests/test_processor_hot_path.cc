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
#include "oeconnect/hello.h"
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
    cfg.enable_continuous = true;
    cfg.continuous_stream_id = OEC_STREAM_RAW_BLOCK;
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

/* The node publishes its input ONCE, under whichever stream id it is labelled
 * with. Labelling it Filtered must not also emit a RAW_BLOCK carrying the same
 * (already filtered) bytes. */
TEST(HotPath, StreamLabelChoosesStreamIdAndEmitsExactlyOneFrame) {
    ShmemTransport t;
    std::string name = uniq() + ".label";
    ASSERT_TRUE(t.start(name));

    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    ProcessorConfig cfg;
    cfg.num_channels = 2;
    cfg.block_size   = 4;
    cfg.enable_continuous = true;
    cfg.continuous_stream_id = OEC_STREAM_FILTERED_BLOCK;
    cfg.source_id = 42;
    cfg.enable_spikes = cfg.enable_ttl = false;

    std::vector<int16_t> input(cfg.num_channels * cfg.block_size, 7);
    processBlock(input.data(), 0, cfg, t, board, outbox);

    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);

    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);
    const auto* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->stream_id, OEC_STREAM_FILTERED_BLOCK);
    const auto* sh = (const oec_block_subheader_t*)((const uint8_t*)frame + sizeof(*fh));
    EXPECT_EQ(sh->source_id, 42);

    /* Exactly one frame: consume it and the ring must be empty. */
    oec_ringbuf_consume(rb);
    EXPECT_EQ(oec_ringbuf_peek(rb, &sz), nullptr);

    oec_ringbuf_detach(rb);
    oec_shm_close(h);
    t.stop();
    oec_shm_unlink(name.c_str());
}

/* A block too large for one ring slot used to vanish with no diagnostic. */
TEST(HotPath, OversizedBlockIsCountedAsDropped) {
    ShmemTransport t;
    std::string name = uniq() + ".oversize";
    ASSERT_TRUE(t.start(name));

    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    ProcessorConfig cfg;
    /* 1024 ch x 64 samp x 2 B = 128 KiB payload, well over the 64 KiB slot. */
    cfg.num_channels = 1024;
    cfg.block_size   = 64;
    cfg.enable_continuous = true;
    cfg.enable_spikes = cfg.enable_ttl = false;

    std::vector<int16_t> input((size_t)cfg.num_channels * cfg.block_size, 0);
    ASSERT_EQ(t.totalDropped(), 0u);
    processBlock(input.data(), 0, cfg, t, board, outbox);

    EXPECT_EQ(t.totalDropped(), 1u) << "oversized frame must be counted, not silently dropped";

    /* And nothing was published. */
    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    EXPECT_EQ(oec_ringbuf_peek(rb, &sz), nullptr);
    oec_ringbuf_detach(rb);
    oec_shm_close(h);

    t.stop();
    oec_shm_unlink(name.c_str());
}

/* Spec §4.3: after data is lost, the producer sets BIT_LOST_DATA on the next frame
 * it publishes, exactly once. Without this the consumer cannot see gaps at all. */
TEST(HotPath, DropArmsLostDataFlagOnNextFrameExactlyOnce) {
    ShmemTransport t;
    std::string name = uniq() + ".lostflag";
    ASSERT_TRUE(t.start(name));

    FileReaderAdapter board("ttl_out_log.csv");
    AckOutbox outbox(64);

    ProcessorConfig cfg;
    cfg.num_channels = 2;
    cfg.block_size   = 4;
    cfg.enable_continuous = true;
    cfg.enable_spikes = cfg.enable_ttl = false;
    std::vector<int16_t> input(cfg.num_channels * cfg.block_size, 1);

    /* No drop yet: the first frame must be clean. */
    processBlock(input.data(), 0, cfg, t, board, outbox);
    ASSERT_EQ(t.totalDropped(), 0u);

    /* Force a drop, then publish two more frames. */
    t.noteDropped();
    processBlock(input.data(), 4, cfg, t, board, outbox);
    processBlock(input.data(), 8, cfg, t, board, outbox);

    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);

    uint32_t sz = 0;
    const void* f0 = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(f0, nullptr);
    EXPECT_EQ(((const oec_frame_header_t*)f0)->flags & OEC_FLAG_LOST_DATA, 0u)
        << "frame published before any drop must be clean";
    oec_ringbuf_consume(rb);

    const void* f1 = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(((const oec_frame_header_t*)f1)->flags & OEC_FLAG_LOST_DATA, OEC_FLAG_LOST_DATA)
        << "first frame after a drop must carry LOST_DATA";
    oec_ringbuf_consume(rb);

    const void* f2 = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(((const oec_frame_header_t*)f2)->flags & OEC_FLAG_LOST_DATA, 0u)
        << "flag must be consumed, not sticky";

    oec_ringbuf_detach(rb);
    oec_shm_close(h);
    t.stop();
    oec_shm_unlink(name.c_str());
}

/* A ring-full eviction is a real data loss, and used to be invisible: with
 * dropOldest the ring returns a slot rather than nullptr. */
TEST(HotPath, RingFullEvictionIsCountedAsDropped) {
    ShmemTransport t;
    std::string name = uniq() + ".evict";
    ASSERT_TRUE(t.start(name));

    uint32_t cap = 0;
    /* Default geometry is 256 slots: fill it, then overflow by three. */
    for (int i = 0; i < OEC_DEFAULT_SLOT_COUNT; ++i) {
        ASSERT_NE(t.acquireDataSlot(&cap, /*dropOldest=*/true), nullptr);
        t.publishData(64);
    }
    EXPECT_EQ(t.totalDropped(), 0u) << "filling the ring exactly is not a drop";

    for (int i = 1; i <= 3; ++i) {
        ASSERT_NE(t.acquireDataSlot(&cap, /*dropOldest=*/true), nullptr);
        t.publishData(64);
        EXPECT_EQ(t.totalDropped(), (uint64_t)i);
    }

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
    cfg.enable_continuous = false;
    cfg.enable_spikes = cfg.enable_ttl = false;
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

TEST(HotPath, HelloFrameRoundtripsThroughShmem) {
    /* Producer-side: write a HELLO frame onto the data ring via raw libshared
     * primitives, then re-attach as a consumer and decode it. Mirrors how
     * Bonsai's Session.ReaderLoop will see the announcement at startup. */
    ShmemTransport t;
    std::string name = uniq() + ".hello";
    ASSERT_TRUE(t.start(name));

    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/false);
    ASSERT_NE(slot, nullptr);
    const uint32_t plugin_ver = 0x01000200u;
    const uint32_t lib_ver    = 0x01010000u;
    size_t n = oec_hello_emit(slot, cap, plugin_ver, lib_ver);
    ASSERT_GT(n, 0u);
    t.publishData((uint32_t)n);

    /* Consumer side. */
    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);
    const oec_frame_header_t* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->stream_id, OEC_STREAM_HELLO);
    EXPECT_EQ(fh->payload_len, sizeof(oec_hello_body_t));
    const oec_hello_body_t* body =
        (const oec_hello_body_t*)((const uint8_t*)frame + sizeof(*fh));
    EXPECT_EQ(body->protocol_major, OEC_PROTOCOL_VERSION_MAJOR);
    EXPECT_EQ(body->protocol_minor, OEC_PROTOCOL_VERSION_MINOR);
    EXPECT_EQ(body->plugin_version, plugin_ver);
    EXPECT_EQ(body->lib_version,    lib_ver);
    EXPECT_EQ(body->reserved,       0u);
    oec_ringbuf_detach(rb);
    oec_shm_close(h);

    t.stop();
    oec_shm_unlink(name.c_str());
}

TEST(HotPath, TtlEventFrameRoundtripsThroughShmem) {
    ShmemTransport t;
    std::string name = uniq() + ".ttlevt";
    ASSERT_TRUE(t.start(name));

    writeTtlEventFrame(t, /*line=*/5, /*edge=*/1, /*board_id=*/0,
                       /*sample_index=*/12345);

    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);

    const auto* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->stream_id, OEC_STREAM_TTL_EVENT);
    EXPECT_EQ(fh->payload_len, 4u);
    EXPECT_EQ(fh->sample_index, 12345u);
    const uint8_t* body = (const uint8_t*)frame + sizeof(*fh);
    EXPECT_EQ(body[0], 5);   /* line */
    EXPECT_EQ(body[1], 1);   /* edge */
    EXPECT_EQ(body[2], 0);   /* board_id */

    oec_ringbuf_detach(rb);
    oec_shm_close(h);
    t.stop();
    oec_shm_unlink(name.c_str());
}

TEST(HotPath, SpikeFrameCarriesWaveformAndMetadata) {
    ShmemTransport t;
    std::string name = uniq() + ".spike";
    ASSERT_TRUE(t.start(name));

    const int16_t wf[8] = { 1, -2, 3, -4, 5, -6, 7, -8 };
    writeSpikeFrame(t, /*electrode=*/7, /*unit=*/2, /*threshold=*/-42.5f,
                    wf, /*n_samples=*/8, /*sample_index=*/999);

    oec_shm_t* h = nullptr; void* mapped = nullptr; size_t msz = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 0, &h, &mapped, &msz), OEC_OK);
    oec_ringbuf_t* rb = nullptr;
    ASSERT_EQ(oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb), OEC_OK);
    uint32_t sz = 0;
    const void* frame = oec_ringbuf_peek(rb, &sz);
    ASSERT_NE(frame, nullptr);

    const auto* fh = (const oec_frame_header_t*)frame;
    EXPECT_EQ(fh->stream_id, OEC_STREAM_SPIKE);
    EXPECT_EQ(fh->payload_len, 8u + 8u * sizeof(int16_t));
    EXPECT_EQ(fh->sample_index, 999u);

    const uint8_t* body = (const uint8_t*)frame + sizeof(*fh);
    uint16_t electrode = 0, unit = 0; float thr = 0.f;
    std::memcpy(&electrode, body + 0, 2);
    std::memcpy(&unit, body + 2, 2);
    std::memcpy(&thr, body + 4, 4);
    EXPECT_EQ(electrode, 7);
    EXPECT_EQ(unit, 2);
    EXPECT_FLOAT_EQ(thr, -42.5f);

    int16_t got[8] = {};
    std::memcpy(got, body + 8, sizeof(got));
    for (int i = 0; i < 8; ++i) EXPECT_EQ(got[i], wf[i]) << "sample " << i;

    oec_ringbuf_detach(rb);
    oec_shm_close(h);
    t.stop();
    oec_shm_unlink(name.c_str());
}

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
    /* Hand-packed CMD body (no struct padding): [cmd_id:2][cookie:4][line:1][edge:1] */
    uint8_t cmd_body[8] = {};
    const uint16_t cid = OEC_CMD_SET_TTL;
    const uint32_t ck  = 0x111u;
    std::memcpy(cmd_body + 0, &cid, 2);
    std::memcpy(cmd_body + 2, &ck,  4);
    cmd_body[6] = 5;  /* line */
    cmd_body[7] = 1;  /* edge */
    std::memcpy((uint8_t*)slot + sizeof(hdr), cmd_body, sizeof(cmd_body));
    oec_ringbuf_publish(cmd_w);
    oec_ringbuf_detach(cmd_w);
    oec_shm_close(h);

    int callback_hits = 0;
    uint8_t observed_line = 0, observed_edge = 0;
    ProcessorConfig cfg;
    cfg.num_channels = 1; cfg.block_size = 1;
    cfg.enable_continuous = false;
    cfg.enable_spikes = cfg.enable_ttl = false;
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
