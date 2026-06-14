#pragma once
#include "IBoardAdapter.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace oec::plugin {

class FileReaderAdapter final : public IBoardAdapter {
public:
    explicit FileReaderAdapter(std::string log_path);
    ~FileReaderAdapter() override;

    std::string name() const override { return "FileReader"; }
    int numTtlOutLines() const override { return 8; }
    const char* sdkVersionString() const override { return "FileReader"; }
    bool meetsMinimumSdk() const override { return true; }  // no SDK to gate on
    uint64_t setTtl(uint8_t line, bool high) override;

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
