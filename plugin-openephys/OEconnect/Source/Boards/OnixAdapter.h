#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

class OnixAdapter final : public IBoardAdapter {
public:
    explicit OnixAdapter(void* source_node);
    std::string name() const override { return "ONIX"; }
    int  numTtlOutLines() const override { return 16; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
