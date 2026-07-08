#pragma once
#include "IBoardAdapter.h"

#include <atomic>
#include <string>

namespace oec::plugin {

/**
 * Escape hatch onto OpenEphys' event bus.
 *
 * `GenericProcessor::setTTLState` and `broadcastMessage` are *protected*: only the
 * processor subclass may call them. OEconnectJuceProcessor implements this
 * interface and forwards, which also keeps this header free of JUCE includes so
 * the unit-test build can exercise the adapter without a GUI toolchain.
 */
class ITtlEventEmitter {
public:
    virtual ~ITtlEventEmitter() = default;

    /** Publish a TTL state change `sample_in_block` samples into the current
     *  process() block. Wait-free: a fixed-size lock-free push. */
    virtual void emitTtlEdge(int sample_in_block, int line, bool state) = 0;

    /** Broadcast the acquisition board's remote-control pulse command.
     *  No-op on boards that do not implement handleBroadcastMessage(). */
    virtual void sendBoardTrigger(int line, int width_ms) = 0;
};

/**
 * Publishes TTL edges onto OpenEphys' event bus, which is the only board-agnostic
 * way for a plugin to drive a digital output.
 *
 *   [Acquisition Board] -> [OEconnect] -> [Acq Board Output]
 *                              |                  |
 *                        emits TTL event    event -> physical line
 *
 * Any board with an output companion plugin (Acq Board Output, Arduino Output,
 * Pulse Pal, ONIX) works with no board-specific code here. Downstream Record Nodes
 * also capture the event, which is what keeps the OE recording a complete copy of
 * every edge the bridge issued (architecture rule 1).
 *
 * With `direct_trigger` enabled, a *pulse* additionally broadcasts the acquisition
 * board's documented remote-control command so the board fires it directly,
 * skipping the downstream output plugin's response time.
 */
class EventBusTtlAdapter final : public IBoardAdapter {
public:
    EventBusTtlAdapter(ITtlEventEmitter* emitter, std::string board_name)
        : emitter_(emitter), board_name_(std::move(board_name)) {}

    std::string name() const override { return board_name_; }
    int numTtlOutLines() const override { return 8; }

    const char* sdkVersionString() const override { return "OE event bus"; }
    /* Nothing board-specific is linked, so there is no SDK to gate on. */
    bool meetsMinimumSdk() const override { return true; }

    uint64_t setTtl(uint8_t line, bool high, int sample_in_block) override {
        if (!emitter_) return 0;
        if (sample_in_block < 0) sample_in_block = 0;
        emitter_->emitTtlEdge(sample_in_block, (int)line, high);
        return block_start_sample_.load(std::memory_order_relaxed) + (uint64_t)sample_in_block;
    }

    /** Direct drive via `ACQBOARD TRIGGER <line> <duration_ms>`. Only delivered
     *  while acquisition is active, and only expressible as a timed pulse -- the
     *  grammar has no latch form, so SetTtl never takes this path. */
    bool triggerPulse(uint8_t line, uint32_t width_us) override {
        if (!emitter_ || !direct_trigger_) return false;
        int width_ms = (int)((width_us + 999u) / 1000u);   /* round up */
        if (width_ms < 1) width_ms = 1;
        emitter_->sendBoardTrigger((int)line, width_ms);
        return true;
    }

    void setDirectTrigger(bool on) { direct_trigger_ = on; }

    /** Called by the processor at the top of each process() tick so emitted edges
     *  can report an absolute sample index. */
    void setBlockStartSample(uint64_t s) { block_start_sample_.store(s, std::memory_order_relaxed); }

private:
    ITtlEventEmitter*     emitter_ = nullptr;
    std::string           board_name_;
    bool                  direct_trigger_ = false;
    std::atomic<uint64_t> block_start_sample_{0};
};

}  // namespace oec::plugin
