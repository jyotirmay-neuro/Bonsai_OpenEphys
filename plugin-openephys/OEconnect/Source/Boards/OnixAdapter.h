#pragma once
#include "IBoardAdapter.h"
#include <atomic>

namespace oec::plugin {

/** Minimum ONIX firmware: v2.0, encoded (major<<16)|(minor<<8)|patch. */
inline constexpr uint32_t kMinOnixFirmware = 0x0200'0000u;  /* v2.0 */

class OnixAdapter final : public IBoardAdapter {
public:
    explicit OnixAdapter(void* source_node);
    std::string name() const override { return "ONIX"; }
    int  numTtlOutLines() const override { return 16; }
    uint64_t setTtl(uint8_t line, bool high) override;
    void onStartAcquisition(int blockSize, double sampleRate) override;

    const char* sdkVersionString() const override {
        // TODO(live-OE): query ONIX hub/device firmware version.
        return "ONIX firmware (unknown)";
    }
    bool meetsMinimumSdk() const override {
        // TODO(live-OE): return queriedFirmware() >= kMinOnixFirmware;
        return true;
    }

private:
    void* source_node_;
    std::atomic<uint64_t> last_sample_{0};
};

}  // namespace oec::plugin
