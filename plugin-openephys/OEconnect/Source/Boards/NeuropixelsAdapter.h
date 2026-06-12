#pragma once
#include "IBoardAdapter.h"
#include <memory>

namespace oec::plugin {

/**
 * NPX-PXI source has limited TTL out. If a paired Acq Board sits in the same
 * signal chain, this adapter delegates to its RhdAcqBoardAdapter; otherwise
 * setTtl returns 0 (NOT_SUPPORTED).
 */
class NeuropixelsAdapter final : public IBoardAdapter {
public:
    NeuropixelsAdapter(void* npx_source_node, std::unique_ptr<IBoardAdapter> delegate);
    std::string name() const override;
    int  numTtlOutLines() const override;
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

private:
    void* npx_source_node_;
    std::unique_ptr<IBoardAdapter> delegate_;
};

}  // namespace oec::plugin
