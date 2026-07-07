#pragma once
#include "Transport/ITransport.h"
#include "Boards/IBoardAdapter.h"
#include "Sync/DriftEmitter.h"
#include "Util/AckOutbox.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace oec::plugin {

struct SlowCmdRequest;   /* fwd; full def in Util/SlowCmdWorker.h */
class PulseScheduler;    /* fwd; full def in Util/PulseScheduler.h */

/**
 * Processor configuration owned by the editor (UI).
 */
struct ProcessorConfig {
    bool   enable_raw      = true;
    bool   enable_filtered = false;
    bool   enable_spikes   = true;
    bool   enable_ttl      = true;
    int    block_size      = 32;
    int    num_channels    = 256;
    double sample_rate_hz  = 30000.0;
    std::string transport_mode = "Auto";           /* "Auto" | "SharedMem" | "Zmq" */
    std::string shm_name;                          /* set by start() */
    /* Loopback by default — no unauthenticated exposure beyond this host.
     * Binding to a routable address (e.g. "tcp://0.0.0.0:5557") requires a
     * CURVE server key (OEC_ZMQ_CURVE_SECRET); the transport refuses an
     * unauthenticated non-loopback bind (spec §5.7). */
    std::string zmq_endpoint = "tcp://127.0.0.1:5557|tcp://127.0.0.1:5558";

    /** Called from the audio thread whenever a Bonsai-issued TTL fires.
     *  JUCE wrapper hooks GenericProcessor::addEvent() here so OE's Record
     *  Node captures the same edge. Leave null in unit tests. */
    std::function<void(uint8_t line, uint8_t edge, uint64_t sample_index)>
        on_ttl_emit;

    /** Forward slow commands (START/STOP record/acq) to a worker thread.
     *  Wait-free push; worker emits ACK(COMPLETED) via the AckOutbox. */
    std::function<void(SlowCmdRequest)> slow_enqueue;
};

/**
 * Hot-path inner loop (pure C++, no JUCE deps). One call ~= one process() tick.
 */
void processBlock(
    const int16_t* input,
    uint64_t sample_index,
    const ProcessorConfig& config,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox,
    PulseScheduler* pulses = nullptr);

}  // namespace oec::plugin
