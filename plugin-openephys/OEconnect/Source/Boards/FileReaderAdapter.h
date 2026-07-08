#pragma once
#include "IBoardAdapter.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace oec::plugin {

/**
 * Test / CI double. Logs every requested edge to CSV instead of touching
 * hardware or OE's event bus, so the hot path can be exercised without a JUCE
 * host. Not used by the shipped plugin -- see EventBusTtlAdapter.
 */
class FileReaderAdapter final : public IBoardAdapter {
public:
    explicit FileReaderAdapter(std::string log_path);
    ~FileReaderAdapter() override;

    std::string name() const override { return "FileReader"; }
    int numTtlOutLines() const override { return 8; }
    const char* sdkVersionString() const override { return "FileReader"; }
    bool meetsMinimumSdk() const override { return true; }  // no SDK to gate on
    uint64_t setTtl(uint8_t line, bool high, int sample_in_block) override;

    void onStartAcquisition(int blockSize, double sampleRate) override;
    void onStopAcquisition() override;

private:
    std::string log_path_;
    std::FILE*  fp_ = nullptr;
    std::mutex  mu_;
    std::atomic<uint64_t> sample_index_{0};
    double sample_rate_ = 30000.0;
};

}  // namespace oec::plugin
