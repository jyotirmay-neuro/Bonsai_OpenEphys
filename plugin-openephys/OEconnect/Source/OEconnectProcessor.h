#pragma once
#include "Transport/ITransport.h"
#include "Boards/IBoardAdapter.h"
#include "Sync/DriftEmitter.h"
#include "Util/AckOutbox.h"

extern "C" {
#include "oeconnect/ringbuf.h"   /* OEC_DEFAULT_SLOT_SIZE / _SLOT_COUNT */
}

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
    /* This node sees exactly one continuous buffer: whatever the upstream OE chain
     * handed it. There is no second, unfiltered copy to publish. So the choice is
     * not "raw and/or filtered" but "which stream id do I stamp on my input".
     *
     * To get both in Bonsai, branch the OE chain and run two OEconnect nodes:
     *   [Source] -> [OEconnect: Raw]
     *   [Source] -> [Bandpass] -> [OEconnect: Filtered]
     * Each owns its own shared-memory region (scoped by node id). */
    bool     enable_continuous    = true;
    uint16_t continuous_stream_id = OEC_STREAM_RAW_BLOCK;  /* or OEC_STREAM_FILTERED_BLOCK */
    uint8_t  source_id            = 0;  /* OE node id, stamped into the subheader */

    bool   enable_spikes   = true;
    bool   enable_ttl      = true;

    /* Shared-memory ring geometry (spec §4.7). slot_size caps the largest frame
     * this node can publish; slot_count is how long the consumer may stall before
     * data is overwritten. slot_count must be a power of two. */
    uint32_t slot_size  = OEC_DEFAULT_SLOT_SIZE;
    uint32_t slot_count = OEC_DEFAULT_SLOT_COUNT;

    /* Snapshot of the incoming DataStreams, refreshed in updateSettings().
     * `source_id` is the index into this table and is stamped on every block, so a
     * consumer can map a block back to its stream's channel count and sample rate
     * via the SYNC frame's stream_meta[] (spec §3.1, §3.2). One RAW_BLOCK is
     * published per stream per callback: streams advance at different rates
     * (Neuropixels AP 30 kHz vs LFP 2.5 kHz), so they cannot share a block. */
    oec_stream_meta_t streams[OEC_MAX_STREAMS] = {};
    int               n_streams = 0;
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
     *  Wait-free push; worker emits ACK(COMPLETED) via the AckOutbox.
     *  Returns false when a slow command is already in flight, so the caller
     *  answers ACK(BUSY) rather than stacking commands (spec §5.5). */
    std::function<bool(SlowCmdRequest)> slow_enqueue;
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

/**
 * Publish one continuous block for a single DataStream.
 *
 * `input` is `n_channels * n_samples` int16 ADC counts, channel-major.
 * `source_id` indexes ProcessorConfig::streams and is stamped into the subheader.
 * `sample_index` is that stream's own running sample counter — streams advance at
 * different rates, so a shared counter would misdate everything but the fastest.
 *
 * Wait-free; audio thread. Frames too large for one ring slot are dropped and
 * counted (see ITransport::noteDropped).
 */
void writeContinuousBlock(ITransport& transport, const ProcessorConfig& config,
                          const int16_t* input,
                          uint16_t n_channels, uint16_t n_samples,
                          uint64_t sample_index, uint8_t source_id);

/**
 * Control-plane half of a process() tick: service due TTL pulses, drain the command
 * ring, drain the ack outbox. Split out from processBlock() so the JUCE processor
 * can run it once per callback and then publish one block per DataStream.
 */
void processControl(uint64_t sample_index,
                    const ProcessorConfig& config,
                    ITransport& transport,
                    IBoardAdapter& board,
                    AckOutbox& outbox,
                    PulseScheduler* pulses = nullptr);

/**
 * Publish one ERROR frame (spec §3.1: {code_u16, utf8_len_u16, utf8_msg[]}).
 * `code` is one of the OEC_ERR_* values. Best-effort: dropped if the ring is full.
 */
void writeErrorFrame(ITransport& transport, uint16_t code, const char* msg);

/**
 * Publish one TTL_EVENT frame (spec §3.1: {line_u8, edge_u8, board_id_u8, _pad}).
 * Wait-free; called from the audio thread while draining OE's event buffer.
 */
void writeTtlEventFrame(ITransport& transport,
                        uint8_t line, uint8_t edge, uint8_t board_id,
                        uint64_t sample_index);

/**
 * Publish one SPIKE frame (spec §3.1:
 * {electrode_u16, unit_u16, threshold_f32, waveform_int16[n]}).
 * `waveform` holds `n_samples` int16 ADC counts, channel-major.
 * Wait-free; called from the audio thread.
 */
void writeSpikeFrame(ITransport& transport,
                     uint16_t electrode, uint16_t unit, float threshold,
                     const int16_t* waveform, uint32_t n_samples,
                     uint64_t sample_index);

}  // namespace oec::plugin
