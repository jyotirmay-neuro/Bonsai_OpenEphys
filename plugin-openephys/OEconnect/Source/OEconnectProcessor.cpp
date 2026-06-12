#include "OEconnectProcessor.h"
#include "Transport/Frame.h"

#include <cstring>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

namespace {

#if defined(_WIN32)
uint64_t qpc_now() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t)c.QuadPart; }
#else
uint64_t qpc_now() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#endif

void writeRawBlock(ITransport& t, const ProcessorConfig& cfg,
                   const int16_t* input, uint64_t sample_index,
                   uint16_t stream_id, uint16_t flags)
{
    const uint32_t payload =
        (uint32_t)(sizeof(oec_block_subheader_t) +
                   (size_t)cfg.num_channels * (size_t)cfg.block_size * sizeof(int16_t));
    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/true);
    if (!slot) return;
    const size_t need = sizeof(oec_frame_header_t) + payload;
    if (cap < need) return;

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, stream_id, payload, sample_index, qpc_now(), flags);
    auto* sh = reinterpret_cast<oec_block_subheader_t*>(slot + sizeof(*h));
    sh->n_channels = (uint16_t)cfg.num_channels;
    sh->n_samples  = (uint16_t)cfg.block_size;
    sh->dtype      = OEC_DTYPE_INT16;
    sh->source_id  = 0;
    sh->reserved   = 0;
    std::memcpy(slot + sizeof(*h) + sizeof(*sh), input,
                (size_t)cfg.num_channels * cfg.block_size * sizeof(int16_t));
    t.publishData((uint32_t)need);
}

void emitAck(ITransport& t, uint32_t cookie, uint16_t status,
             uint64_t sample_index)
{
    uint32_t cap = 0;
    uint8_t* slot = t.acquireAckSlot(&cap);
    if (!slot) return;
    struct { uint32_t cookie; uint16_t status; } body{ cookie, status };
    const size_t need = sizeof(oec_frame_header_t) + sizeof(body);
    if (cap < need) return;
    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, OEC_STREAM_ACK, sizeof(body), sample_index, qpc_now(), 0);
    std::memcpy(slot + sizeof(*h), &body, sizeof(body));
    t.publishAck((uint32_t)need);
}

void drainCmdRing(ITransport& t, IBoardAdapter& board, const ProcessorConfig& cfg)
{
    for (;;) {
        uint32_t sz = 0;
        const uint8_t* frame = t.peekCmd(&sz);
        if (!frame) return;
        if (sz < sizeof(oec_frame_header_t) + 6) { t.consumeCmd(); continue; }

        const auto* h = reinterpret_cast<const oec_frame_header_t*>(frame);
        if (h->stream_id != OEC_STREAM_CMD) { t.consumeCmd(); continue; }

        const uint8_t* body = frame + sizeof(*h);
        uint16_t cmd_id = 0; uint32_t cookie = 0;
        std::memcpy(&cmd_id, body + 0, 2);
        std::memcpy(&cookie, body + 2, 4);

        switch (cmd_id) {
            case OEC_CMD_SET_TTL: {
                uint8_t line = body[6];
                uint8_t edge = body[7];
                uint64_t s = board.setTtl(line, edge != 0);
                if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
                emitAck(t, cookie, OEC_ACK_OK, s);
                break;
            }
            case OEC_CMD_PULSE_TTL: {
                uint8_t line = body[6];
                uint8_t edge = body[7];
                uint64_t s = board.setTtl(line, edge != 0);
                if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
                emitAck(t, cookie, OEC_ACK_OK, s);
                break;
            }
            case OEC_CMD_GET_STATE: {
                emitAck(t, cookie, OEC_ACK_OK, 0);
                break;
            }
            case OEC_CMD_START_RECORD:
            case OEC_CMD_STOP_RECORD:
            case OEC_CMD_START_ACQ:
            case OEC_CMD_STOP_ACQ:
                /* Slow commands - emit PENDING; real worker thread completes later. */
                emitAck(t, cookie, OEC_ACK_PENDING, 0);
                /* TODO(impl): forward to slow-cmd worker via a message queue. */
                break;
            default:
                emitAck(t, cookie, OEC_ACK_NOT_SUPPORTED, 0);
                break;
        }
        t.consumeCmd();
    }
}

void drainAckOutbox(ITransport& t, AckOutbox& outbox)
{
    AckEntry e{};
    while (outbox.tryPop(e)) {
        uint32_t cap = 0;
        uint8_t* slot = t.acquireAckSlot(&cap);
        if (!slot) break;
        const uint16_t stream = (e.cmd_id == OEC_STREAM_SYNC) ? OEC_STREAM_SYNC
                                                              : OEC_STREAM_ACK;
        struct { uint32_t cookie; uint16_t status; } body{ e.cookie, e.status };
        const uint32_t payload = (stream == OEC_STREAM_SYNC) ? 0u : (uint32_t)sizeof(body);
        const size_t need = sizeof(oec_frame_header_t) + payload;
        if (cap < need) break;
        auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
        oec_frame_init(h, stream, payload, e.sample_index, e.host_qpc_ticks, 0);
        if (payload) std::memcpy(slot + sizeof(*h), &body, sizeof(body));
        t.publishAck((uint32_t)need);
    }
}

}  // namespace

void processBlock(
    const int16_t* input,
    uint64_t sample_index,
    const ProcessorConfig& cfg,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox)
{
    drainCmdRing(transport, board, cfg);
    drainAckOutbox(transport, outbox);

    if (cfg.enable_raw) {
        writeRawBlock(transport, cfg, input, sample_index,
                      OEC_STREAM_RAW_BLOCK, /*flags=*/0);
    }
    if (cfg.enable_filtered) {
        writeRawBlock(transport, cfg, input, sample_index,
                      OEC_STREAM_FILTERED_BLOCK, /*flags=*/0);
    }
}

}  // namespace oec::plugin
