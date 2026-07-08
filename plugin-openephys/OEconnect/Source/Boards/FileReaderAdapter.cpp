#include "FileReaderAdapter.h"
#include <chrono>

namespace oec::plugin {

FileReaderAdapter::FileReaderAdapter(std::string log_path) : log_path_(std::move(log_path)) {}
FileReaderAdapter::~FileReaderAdapter() { onStopAcquisition(); }

void FileReaderAdapter::onStartAcquisition(int /*blockSize*/, double sampleRate) {
    std::lock_guard<std::mutex> lk(mu_);
    sample_rate_ = sampleRate;
    fp_ = std::fopen(log_path_.c_str(), "w");
    if (fp_) std::fprintf(fp_, "sample_index,line,edge\n");
}
void FileReaderAdapter::onStopAcquisition() {
    std::lock_guard<std::mutex> lk(mu_);
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

uint64_t FileReaderAdapter::setTtl(uint8_t line, bool high, int /*sample_in_block*/) {
    /* No hardware. Bump a counter to give Bonsai something coherent to align with. */
    const uint64_t s = sample_index_.fetch_add(1, std::memory_order_relaxed) + 1;
    /* fprintf under mutex is NOT wait-free; this adapter is the dev/CI double,
       not the hot path. EventBusTtlAdapter (the shipped one) is wait-free. */
    std::lock_guard<std::mutex> lk(mu_);
    if (fp_) std::fprintf(fp_, "%llu,%u,%u\n",
                          (unsigned long long)s, (unsigned)line, (unsigned)(high ? 1 : 0));
    return s;
}

}  // namespace oec::plugin
