#include "RhdAcqBoardAdapter.h"

/*
 * Implementation note (read before editing):
 * The OE Rhythm source node exposes a method along the lines of
 *   void setTTLOutputBit(int channel, bool state);
 * but the exact signature varies across plugin-GUI minor versions.
 * Look in external/plugin-GUI/Plugins/RhythmNode/ for the live API.
 * Until the live SDK is wired, this adapter no-ops and reports sample 0.
 */
namespace oec::plugin {

RhdAcqBoardAdapter::RhdAcqBoardAdapter(void* source_node) : source_node_(source_node) {}

void RhdAcqBoardAdapter::onStartAcquisition(int /*blockSize*/, double /*sampleRate*/) {
    last_sample_.store(0, std::memory_order_relaxed);
}

uint64_t RhdAcqBoardAdapter::setTtl(uint8_t /*line*/, bool /*high*/) {
    if (!source_node_) return 0;
    /* TODO(impl): cast to the real Rhythm source-node type discovered from
       external/plugin-GUI/ and call its setTTLOutputBit(line, high). */
    return last_sample_.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace oec::plugin
