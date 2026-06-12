#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

class RhdAcqBoardAdapter final : public IBoardAdapter {
public:
    /** `source_node` is the upstream Rhythm FPGA source-node pointer (opaque). */
    explicit RhdAcqBoardAdapter(void* source_node);
    std::string name() const override { return "Rhythm FPGA (Acq Board)"; }
    int  numTtlOutLines() const override { return 8; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
