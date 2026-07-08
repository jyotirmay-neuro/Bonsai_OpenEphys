#include "ZmqTransport.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
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

/* A bind target is loopback-only if it stays on this host: TCP to 127.x /
 * ::1 / localhost, or the kernel-local ipc:// and inproc:// schemes. Anything
 * else (0.0.0.0, *, a routable IP) is externally reachable and, per spec §5.7,
 * must be CURVE-authenticated. */
bool is_loopback_endpoint(const std::string& ep) {
    if (ep.rfind("ipc://", 0) == 0 || ep.rfind("inproc://", 0) == 0) return true;
    if (ep.find("127.0.0.1") != std::string::npos) return true;
    if (ep.find("[::1]") != std::string::npos || ep.find("::1") != std::string::npos) return true;
    if (ep.find("localhost") != std::string::npos) return true;
    return false;
}

/* Simple bounded SPSC vector ring used as the in-process audio->shipper buffer. */
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
    /** `*evicted` is set when the oldest unread slot had to be discarded. */
    uint8_t* acquire(uint32_t* cap, bool dropOldest, bool* evicted) {
        if (evicted) *evicted = false;
        size_t p = prod.load(std::memory_order_relaxed);
        size_t c = cons.load(std::memory_order_acquire);
        if (p - c >= slots.size()) {
            if (!dropOldest) return nullptr;
            cons.store(c + 1, std::memory_order_release);
            if (evicted) *evicted = true;
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

    /* REP socket is stateful; track whether a req is outstanding awaiting reply. */
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

    /* Refuse an unauthenticated bind to any externally reachable address.
     * CURVE server key comes from OEC_ZMQ_CURVE_SECRET (Z85, 40 chars). */
    const bool loopback = is_loopback_endpoint(pub_ep) && is_loopback_endpoint(rep_ep);
    const char* curve_secret = std::getenv("OEC_ZMQ_CURVE_SECRET");
    if (!loopback && (!curve_secret || std::strlen(curve_secret) != 40)) {
        /* Non-loopback bind without a valid CURVE key: fail closed rather than
         * expose the neural data + control channel to the network. */
        return false;
    }

    try {
        if (!loopback && curve_secret) {
            /* Both sockets act as CURVE servers; clients must present the
             * matching public key. */
            impl_->pub.set(zmq::sockopt::curve_server, true);
            impl_->pub.set(zmq::sockopt::curve_secretkey, std::string(curve_secret));
            impl_->rep.set(zmq::sockopt::curve_server, true);
            impl_->rep.set(zmq::sockopt::curve_secretkey, std::string(curve_secret));
        }
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
}

uint8_t* ZmqTransport::acquireDataSlot(uint32_t* out_cap, bool dropOldest) {
    bool evicted = false;
    uint8_t* slot = impl_->data.acquire(out_cap, dropOldest, &evicted);
    /* Route through noteDropped() so the LOST_DATA flag is armed too, rather than
     * bumping the counter behind its back. */
    if (evicted) noteDropped();
    return slot;
}
void ZmqTransport::publishData(uint32_t bytes_written) {
    impl_->data.publish(bytes_written);
}
uint8_t* ZmqTransport::acquireAckSlot(uint32_t* out_cap) {
    /* dropOldest=false: the ack ring returns nullptr when full rather than evicting. */
    return impl_->ack.acquire(out_cap, /*dropOldest=*/false, /*evicted=*/nullptr);
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
        /* Drain data ring -> PUB */
        const uint8_t* slot = nullptr;
        uint32_t sz = 0;
        while (impl_->data.peek(&slot, &sz)) {
            const auto* h = reinterpret_cast<const oec_frame_header_t*>(slot);
            uint16_t topic = h->stream_id;
            impl_->pub.send(zmq::buffer(&topic, sizeof(topic)), zmq::send_flags::sndmore);
            impl_->pub.send(zmq::buffer(slot, sz), zmq::send_flags::none);
            impl_->data.consume_one();
        }

        /* Drain ack ring -> REP reply (if a request is outstanding) */
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
