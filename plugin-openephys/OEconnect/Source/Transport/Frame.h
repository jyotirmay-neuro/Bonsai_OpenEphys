#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

/**
 * Lightweight helper: serialises a header + payload into a contiguous buffer.
 * No allocation — caller supplies the destination.
 */
struct FrameWriter {
    static size_t write(uint8_t* dest, size_t dest_cap,
                        uint16_t stream_id,
                        uint64_t sample_index,
                        uint64_t host_qpc_ticks,
                        uint16_t flags,
                        const void* payload, uint32_t payload_len)
    {
        const size_t total = sizeof(oec_frame_header_t) + payload_len;
        if (total > dest_cap) return 0;
        oec_frame_header_t* h = reinterpret_cast<oec_frame_header_t*>(dest);
        oec_frame_init(h, stream_id, payload_len,
                       sample_index, host_qpc_ticks, flags);
        if (payload && payload_len) {
            std::memcpy(dest + sizeof(*h), payload, payload_len);
        }
        return total;
    }
};

}  // namespace oec::plugin
