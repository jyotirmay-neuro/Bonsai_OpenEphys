#include "ShmemTransport.h"

namespace oec::plugin {

ShmemTransport::ShmemTransport() = default;
ShmemTransport::~ShmemTransport() { stop(); }

bool ShmemTransport::start(const std::string&) { return false; }
void ShmemTransport::stop() {}

uint8_t* ShmemTransport::acquireDataSlot(uint32_t*, bool) { return nullptr; }
void ShmemTransport::publishData(uint32_t) {}
uint8_t* ShmemTransport::acquireAckSlot(uint32_t*) { return nullptr; }
void ShmemTransport::publishAck(uint32_t) {}
const uint8_t* ShmemTransport::peekCmd(uint32_t*) { return nullptr; }
void ShmemTransport::consumeCmd() {}

}  // namespace oec::plugin
