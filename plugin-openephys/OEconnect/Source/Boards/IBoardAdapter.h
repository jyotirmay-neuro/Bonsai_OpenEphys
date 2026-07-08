#pragma once
#include <cstdint>
#include <string>

namespace oec::plugin {

/**
 * Destination for TTL edges the bridge issues.
 *
 * There is no cross-board API in the Open Ephys GUI for driving a digital output
 * line directly -- `setTTLOutputBit` and friends do not exist. The supported,
 * board-agnostic mechanism is to publish a TTL **event** onto OE's event bus
 * (`GenericProcessor::addTTLChannel` + `setTTLState`) and let a downstream output
 * plugin -- Acq Board Output, Arduino Output, Pulse Pal, ONIX -- translate that
 * event into a physical line. This is how Crossing Detector and Ripple Detector
 * work, and it is what `EventBusTtlAdapter` implements.
 *
 * Publishing the event is also what keeps the OE recording a complete copy of
 * every edge the bridge issued (architecture rule 1), so it is never optional.
 *
 * Implementations must be wait-free: `setTtl` is called from the audio thread.
 */
class IBoardAdapter {
public:
    virtual ~IBoardAdapter() = default;
    virtual std::string name() const = 0;
    virtual int  numTtlOutLines() const = 0;

    /** Human-readable board SDK / firmware version (diagnostics + the
     *  minimum-support refusal message). */
    virtual const char* sdkVersionString() const = 0;

    /** False if the live board SDK / firmware is older than the minimum this
     *  OEconnect release supports. Checked at startAcquisition; a false result
     *  refuses to start rather than risk silently corrupting data. */
    virtual bool meetsMinimumSdk() const = 0;

    /**
     * Drive `line` to `high`, at `sample_in_block` samples past the start of the
     * current process() block. Returns the absolute FPGA sample index at which
     * the edge is timestamped, or 0 if the call was rejected.
     *
     * Wait-free; called on the audio thread.
     */
    virtual uint64_t setTtl(uint8_t line, bool high, int sample_in_block) = 0;

    /**
     * Optional low-latency direct drive for a *timed pulse*, bypassing the
     * downstream output plugin. Returns false if unsupported, in which case the
     * event-bus edge (already emitted) is the only path to hardware.
     *
     * Only pulses can be expressed this way: the acquisition board's broadcast
     * grammar takes a duration and cannot latch a line indefinitely.
     */
    virtual bool triggerPulse(uint8_t /*line*/, uint32_t /*width_us*/) { return false; }

    virtual void onStartAcquisition(int blockSize, double sampleRate) { (void)blockSize; (void)sampleRate; }
    virtual void onStopAcquisition() {}
};

}  // namespace oec::plugin
