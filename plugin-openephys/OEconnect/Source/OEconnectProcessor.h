#pragma once
#include "Transport/ITransport.h"
#include "Boards/IBoardAdapter.h"
#include "Sync/DriftEmitter.h"
#include "Util/AckOutbox.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace oec::plugin {

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
    std::string zmq_endpoint = "tcp://*:5557|tcp://*:5558";
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
    AckOutbox& outbox);

}  // namespace oec::plugin
