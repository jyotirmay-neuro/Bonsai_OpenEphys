#pragma once
#include <cstdint>
#include <string>

namespace oec::plugin {

/**
 * Hardware abstraction over OE source nodes. Audio-thread-callable methods
 * are wait-free.
 */
class IBoardAdapter {
public:
    virtual ~IBoardAdapter() = default;
    virtual std::string name() const = 0;
    virtual int  numTtlOutLines() const = 0;

    /** Wait-free, called on audio thread. Returns the sample index at which
     *  the line will actually flip; 0 if the call was rejected. */
    virtual uint64_t setTtl(uint8_t line, bool high) = 0;

    virtual void onStartAcquisition(int blockSize, double sampleRate) { (void)blockSize; (void)sampleRate; }
    virtual void onStopAcquisition() {}
};

}  // namespace oec::plugin
