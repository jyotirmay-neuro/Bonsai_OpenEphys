#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <zmq.hpp>

#include "Transport/ZmqTransport.h"
#include "Transport/Frame.h"

extern "C" {
#include "oeconnect/version.h"
}

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
