#include <gtest/gtest.h>
#include <cstring>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include "Transport/ShmemTransport.h"
#include "Transport/Frame.h"

extern "C" {
#include "oeconnect/shm.h"
#include "oeconnect/version.h"
#include "oeconnect/ringbuf.h"
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

    ShmemTransport cons;
    ASSERT_TRUE(cons.start(name));
    uint32_t size = 0;
    const uint8_t* peeked = cons.peekCmd(&size);
    EXPECT_EQ(peeked, nullptr) << "cmd ring should be empty";

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
