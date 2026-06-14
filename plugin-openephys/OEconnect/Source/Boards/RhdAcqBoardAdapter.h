#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

/** Minimum Rhythm FPGA firmware: v1.7, encoded (major<<16)|(minor<<8)|patch. */
inline constexpr uint32_t kMinRhythmFirmware = 0x0107'0000u;  /* v1.7 */

class RhdAcqBoardAdapter final : public IBoardAdapter {
public:
    /** `source_node` is the upstream Rhythm FPGA source-node pointer (opaque). */
    explicit RhdAcqBoardAdapter(void* source_node);
    std::string name() const override { return "Rhythm FPGA (Acq Board)"; }
    int  numTtlOutLines() const override { return 8; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

    const char* sdkVersionString() const override {
        // TODO(live-OE): query the Rhythm FPGA firmware version via the source
        // node and format it; placeholder until SDK wiring lands.
        return "Rhythm FPGA firmware (unknown)";
    }
    bool meetsMinimumSdk() const override {
        // TODO(live-OE): read the live firmware version and compare:
        //   return queriedFirmware() >= kMinRhythmFirmware;
        // Until the SDK query is wired, do not block hardware bring-up.
        return true;
    }

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
