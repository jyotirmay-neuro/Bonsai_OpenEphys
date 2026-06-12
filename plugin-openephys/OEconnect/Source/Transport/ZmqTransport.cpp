#include "ZmqTransport.h"

namespace oec::plugin {
struct ZmqTransport::Impl {};
ZmqTransport::ZmqTransport() : impl_(std::make_unique<Impl>()) {}
ZmqTransport::~ZmqTransport() = default;
bool ZmqTransport::start(const std::string&) { return false; }
void ZmqTransport::stop() {}
uint8_t* ZmqTransport::acquireDataSlot(uint32_t*, bool) { return nullptr; }
void ZmqTransport::publishData(uint32_t) {}
uint8_t* ZmqTransport::acquireAckSlot(uint32_t*) { return nullptr; }
void ZmqTransport::publishAck(uint32_t) {}
const uint8_t* ZmqTransport::peekCmd(uint32_t*) { return nullptr; }
void ZmqTransport::consumeCmd() {}
void ZmqTransport::shipperLoop() {}
}  // namespace oec::plugin
