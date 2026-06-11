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
