#pragma once
#include "IBoardAdapter.h"
#include <memory>

namespace oec::plugin {

/** Minimum NPX-PXI API: v0.7, encoded (major<<16)|(minor<<8)|patch. */
inline constexpr uint32_t kMinNeuropixelsApi = 0x0007'0000u;  /* v0.7 */

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

    const char* sdkVersionString() const override {
        // TODO(live-OE): query the NPX-PXI API version.
        return "Neuropixels PXI API (unknown)";
    }
    bool meetsMinimumSdk() const override {
        // TODO(live-OE): return queriedApi() >= kMinNeuropixelsApi; a paired Acq
        // Board delegate must also meet its own floor.
        return delegate_ ? delegate_->meetsMinimumSdk() : true;
    }

private:
    void* npx_source_node_;
    std::unique_ptr<IBoardAdapter> delegate_;
};

}  // namespace oec::plugin
