#pragma once
#include <cstdint>
#include <string>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

/**
 * Transport contract — the OEconnect plugin uses this to publish data frames
 * and consume command frames. One implementation per transport mode.
 *
 * All methods except start()/stop() may be called from the audio thread;
 * implementations must be wait-free on those.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool start(const std::string& endpoint) = 0;
    virtual void stop() = 0;

    /**
     * Acquire a slot for the next outgoing data frame.
     * Returns a writable pointer of size `out_cap`, or nullptr if the
     * ring/buffer is full and `dropOldest=false`. When `dropOldest=true`
     * the oldest unconsumed slot is discarded to make room.
     */
    virtual uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) = 0;

    /** Publish the slot most recently returned by acquireDataSlot. */
    virtual void publishData(uint32_t bytes_written) = 0;

    /** Same pair for the ACK ring. */
    virtual uint8_t* acquireAckSlot(uint32_t* out_cap) = 0;
    virtual void publishAck(uint32_t bytes_written) = 0;

    /**
     * Try to read one pending command frame. Returns nullptr if none.
     * The returned pointer is valid until the next call to peekCmd().
     */
    virtual const uint8_t* peekCmd(uint32_t* out_size) = 0;
    virtual void consumeCmd() = 0;

    /** Record a frame the producer could not publish (e.g. it exceeds one ring
     *  slot). Surfaces via totalDropped(); wait-free. */
    virtual void noteDropped() = 0;

    /** Diagnostics. */
    virtual uint64_t totalDropped() const = 0;
    virtual std::string name() const = 0;
};

}  // namespace oec::plugin
