#include "NeuropixelsAdapter.h"

namespace oec::plugin {

NeuropixelsAdapter::NeuropixelsAdapter(void* npx, std::unique_ptr<IBoardAdapter> del)
    : npx_source_node_(npx), delegate_(std::move(del)) {}

std::string NeuropixelsAdapter::name() const {
    return delegate_ ? ("Neuropixels (+ " + delegate_->name() + ")") : "Neuropixels (no TTL out)";
}
int NeuropixelsAdapter::numTtlOutLines() const {
    return delegate_ ? delegate_->numTtlOutLines() : 0;
}
uint64_t NeuropixelsAdapter::setTtl(uint8_t line, bool high) {
    return delegate_ ? delegate_->setTtl(line, high) : 0;
}
void NeuropixelsAdapter::onStartAcquisition(int blockSize, double sampleRate) {
    if (delegate_) delegate_->onStartAcquisition(blockSize, sampleRate);
}

}  // namespace oec::plugin
