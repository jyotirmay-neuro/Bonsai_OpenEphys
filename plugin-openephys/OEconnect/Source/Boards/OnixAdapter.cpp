#include "OnixAdapter.h"

/*
 * Implementation note:
 * ONIX exposes oni_write_reg() on a DIO peripheral. The Onix Source node
 * in the OE plugin-GUI wraps oni_ctx; from the source node we can either
 * use its public setOutput() if present, or grab the underlying oni_ctx_t
 * and call oni_write_reg(ctx, dev, register, value). Verify against
 * external/plugin-GUI/Plugins/OnixSource/ at impl time.
 */
namespace oec::plugin {

OnixAdapter::OnixAdapter(void* source_node) : source_node_(source_node) {}

void OnixAdapter::onStartAcquisition(int /*blockSize*/, double /*sampleRate*/) {
    last_sample_.store(0, std::memory_order_relaxed);
}

uint64_t OnixAdapter::setTtl(uint8_t /*line*/, bool /*high*/) {
    if (!source_node_) return 0;
    /* TODO(impl): wire to live ONIX DIO peripheral. */
    return last_sample_.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace oec::plugin
