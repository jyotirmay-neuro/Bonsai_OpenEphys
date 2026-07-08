#include "OEconnectProcessor.h"
#include "Transport/Frame.h"
#include "Util/SlowCmdWorker.h"
#include "Util/PulseScheduler.h"

#include <cstring>
#include <vector>

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

}  // namespace

void writeContinuousBlock(ITransport& t, const ProcessorConfig& cfg,
                          const int16_t* input,
                          uint16_t n_channels, uint16_t n_samples,
                          uint64_t sample_index, uint8_t source_id)
{
    if (!cfg.enable_continuous || n_channels == 0 || n_samples == 0) return;

    const size_t samples_bytes = (size_t)n_channels * (size_t)n_samples * sizeof(int16_t);
    const uint32_t payload = (uint32_t)(sizeof(oec_block_subheader_t) + samples_bytes);

    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/true);
    if (!slot) return;
    const size_t need = sizeof(oec_frame_header_t) + payload;

    if (cap < need) {
        /* Too big for one slot. Spec §4.3: span up to 4 consecutive slots with
         * BIT_CONTINUATION, and only then give up with ERROR(FRAME_TOO_LARGE).
         * That path cannot write in place, so build the subheader on the stack. */
        oec_frame_header_t h;
        oec_frame_init(&h, cfg.continuous_stream_id, payload, sample_index, qpc_now(),
                       t.consumePendingFlags());

        oec_block_subheader_t sh;
        sh.n_channels = n_channels;
        sh.n_samples  = n_samples;
        sh.dtype      = OEC_DTYPE_INT16;
        sh.source_id  = source_id;
        sh.reserved   = 0;

        /* The payload is subheader + samples, so stage it contiguously. Sized once
         * per geometry change; the fast path above never touches it. */
        static thread_local std::vector<uint8_t> staging;
        if (staging.size() < payload) staging.resize(payload);
        std::memcpy(staging.data(), &sh, sizeof(sh));
        std::memcpy(staging.data() + sizeof(sh), input, samples_bytes);

        if (!t.publishLargeFrame(h, staging.data(), payload)) {
            t.noteDropped();
            writeErrorFrame(t, OEC_ERR_FRAME_TOO_LARGE,
                            "continuous block exceeds the maximum continuation span; "
                            "raise the ring slot size");
        }
        return;
    }

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    /* Tell the consumer about any gap that opened since the last frame (spec §4.3). */
    oec_frame_init(h, cfg.continuous_stream_id, payload, sample_index, qpc_now(),
                   t.consumePendingFlags());
    auto* sh = reinterpret_cast<oec_block_subheader_t*>(slot + sizeof(*h));
    sh->n_channels = n_channels;
    sh->n_samples  = n_samples;
    sh->dtype      = OEC_DTYPE_INT16;
    sh->source_id  = source_id;
    sh->reserved   = 0;
    std::memcpy(slot + sizeof(*h) + sizeof(*sh), input, samples_bytes);
    t.publishData((uint32_t)need);
}

namespace {

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

/* Parse START_RECORD args {dir_len_u16, dir[], prefix_len_u16, prefix[]} from
 * `p` of length `len`. Returns false (rejecting the command) if any declared
 * length runs past the buffer. */
bool parseStartRecordArgs(const uint8_t* p, size_t len, SlowCmdRequest& req)
{
    if (len < 2) return false;
    uint16_t dlen = 0;
    std::memcpy(&dlen, p, 2);
    if ((size_t)2 + dlen + 2 > len) return false;      /* dir + prefix_len field */
    req.arg1.assign((const char*)(p + 2), dlen);

    const uint8_t* p2 = p + 2 + dlen;
    uint16_t plen = 0;
    std::memcpy(&plen, p2, 2);
    if ((size_t)(p2 - p) + 2 + plen > len) return false;
    req.arg2.assign((const char*)(p2 + 2), plen);
    return true;
}

void drainCmdRing(ITransport& t, IBoardAdapter& board, const ProcessorConfig& cfg,
                  uint64_t block_sample, PulseScheduler* pulses)
{
    constexpr size_t kHdr = sizeof(oec_frame_header_t);
    for (;;) {
        uint32_t sz = 0;
        const uint8_t* frame = t.peekCmd(&sz);
        if (!frame) return;
        /* Header + {cmd_id_u16, cookie_u32} is the minimum a CMD frame can be.
         * Every field access below is bounds-checked against `sz` before the
         * read — command frames arrive from an untrusted transport. */
        if (sz < kHdr + 6) { t.consumeCmd(); continue; }

        const auto* h = reinterpret_cast<const oec_frame_header_t*>(frame);

        /* Spec §2.6 / §8.4: check magic and version_major before trusting any
         * field. A wrong major means the sender's frame layout may differ, so the
         * body cannot even be parsed to recover a cookie for an ACK -- report via
         * an ERROR frame and refuse the command. */
        const oec_status_t v = oec_frame_validate(h);
        if (v != OEC_OK) {
            if (v == OEC_E_VERSION_MISMATCH) {
                writeErrorFrame(t, OEC_ERR_PROTOCOL_VERSION_MISMATCH,
                                "CMD frame protocol major mismatch; command refused");
            } else {
                writeErrorFrame(t, OEC_ERR_BAD_MAGIC, "CMD frame failed magic check");
            }
            t.consumeCmd();
            continue;
        }

        if (h->stream_id != OEC_STREAM_CMD) { t.consumeCmd(); continue; }

        const uint8_t* body = frame + kHdr;
        const size_t body_len = sz - kHdr;
        uint16_t cmd_id = 0; uint32_t cookie = 0;
        std::memcpy(&cmd_id, body + 0, 2);
        std::memcpy(&cookie, body + 2, 4);

        switch (cmd_id) {
            case OEC_CMD_SET_TTL: {
                if (body_len < 8) { emitAck(t, cookie, OEC_ACK_BAD_ARG, 0); break; }
                uint8_t line = body[6];
                uint8_t edge = body[7];
                /* Commands are drained at the top of the block, so the edge is
                 * timestamped at sample offset 0. A latch has no duration, so
                 * there is no direct-trigger path for it -- the event bus (plus a
                 * downstream output plugin) is the only way it reaches hardware. */
                uint64_t s = board.setTtl(line, edge != 0, /*sample_in_block=*/0);
                if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
                emitAck(t, cookie, OEC_ACK_OK, s);
                break;
            }
            case OEC_CMD_PULSE_TTL: {
                /* Body: {line_u8, edge_u8, width_us_u32}. */
                if (body_len < 12) { emitAck(t, cookie, OEC_ACK_BAD_ARG, 0); break; }
                uint8_t line = body[6];
                uint8_t edge = body[7];
                uint32_t width_us = 0;
                std::memcpy(&width_us, body + 8, 4);
                uint64_t s;
                if (pulses) {
                    pulses->arm(board, line, edge != 0, width_us,
                                block_sample, cfg.sample_rate_hz, cfg.on_ttl_emit);
                    s = block_sample;
                } else {
                    /* No scheduler (e.g. unit test): assert only. */
                    s = board.setTtl(line, edge != 0, /*sample_in_block=*/0);
                    if (cfg.on_ttl_emit) cfg.on_ttl_emit(line, edge, s);
                }
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
            case OEC_CMD_STOP_ACQ: {
                SlowCmdRequest req;
                req.cmd_id = cmd_id;
                req.cookie = cookie;
                if (cmd_id == OEC_CMD_START_RECORD) {
                    /* Body: {dir_len_u16, dir[], prefix_len_u16, prefix[]}.
                     * Validate every length against the remaining bytes before
                     * copying — a hostile frame must not over-read the slot. */
                    if (!parseStartRecordArgs(body + 6, body_len - 6, req)) {
                        emitAck(t, cookie, OEC_ACK_BAD_ARG, 0);
                        break;
                    }
                }
                /* Spec §5.5: one slow command in flight at a time. A second one
                 * arriving mid-flight is refused with BUSY rather than queued --
                 * silently stacking START_RECORD behind STOP_RECORD would produce
                 * an ordering the caller never asked for. */
                if (!cfg.slow_enqueue) {
                    emitAck(t, cookie, OEC_ACK_NOT_SUPPORTED, 0);
                } else if (cfg.slow_enqueue(std::move(req))) {
                    emitAck(t, cookie, OEC_ACK_PENDING, 0);
                } else {
                    emitAck(t, cookie, OEC_ACK_BUSY, 0);
                }
                break;
            }
            default:
                emitAck(t, cookie, OEC_ACK_NOT_SUPPORTED, 0);
                break;
        }
        t.consumeCmd();
    }
}

void drainAckOutbox(ITransport& t, AckOutbox& outbox, const ProcessorConfig& cfg)
{
    AckEntry e{};
    while (outbox.tryPop(e)) {
        if (e.cmd_id == OEC_STREAM_SYNC) {
            /* SYNC belongs on the DATA ring (spec §4.5), not the ack ring. The head
             * carries the clock mapping; the trailing stream_meta[] lets a
             * late-joining subscriber reconstruct every stream's channel count and
             * sample rate without waiting for a RAW frame (spec §3.1/§3.2). */
            const int n = (cfg.n_streams > OEC_MAX_STREAMS) ? OEC_MAX_STREAMS : cfg.n_streams;
            const size_t payload =
                sizeof(oec_sync_head_t) + (size_t)n * sizeof(oec_stream_meta_t);
            const size_t need = sizeof(oec_frame_header_t) + payload;

            uint32_t cap = 0;
            uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/false);
            if (!slot) continue;   /* data ring momentarily full; skip this tick */
            if (cap < need) continue;

            auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
            oec_frame_init(h, OEC_STREAM_SYNC, (uint32_t)payload,
                           e.sample_index, e.host_qpc_ticks, t.consumePendingFlags());

            oec_sync_head_t head{ e.qpc_freq_hz, e.fpga_sample_rate_hz };
            std::memcpy(slot + sizeof(*h), &head, sizeof(head));
            if (n > 0) {
                std::memcpy(slot + sizeof(*h) + sizeof(head), cfg.streams,
                            (size_t)n * sizeof(oec_stream_meta_t));
            }
            t.publishData((uint32_t)need);
            continue;
        }
        uint32_t cap = 0;
        uint8_t* slot = t.acquireAckSlot(&cap);
        if (!slot) break;
        struct { uint32_t cookie; uint16_t status; } body{ e.cookie, e.status };
        const uint32_t payload = (uint32_t)sizeof(body);
        const size_t need = sizeof(oec_frame_header_t) + payload;
        if (cap < need) break;
        auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
        oec_frame_init(h, OEC_STREAM_ACK, payload, e.sample_index, e.host_qpc_ticks, 0);
        std::memcpy(slot + sizeof(*h), &body, sizeof(body));
        t.publishAck((uint32_t)need);
    }
}

}  // namespace

/* Publish one ERROR frame (spec §3.1: {code_u16, utf8_len_u16, utf8_msg[]}).
 * Best-effort: if the data ring cannot take it, the error is simply not reported. */
void writeErrorFrame(ITransport& t, uint16_t code, const char* msg)
{
    const uint16_t textlen = (uint16_t)std::strlen(msg);
    const size_t payload = sizeof(code) + sizeof(textlen) + textlen;
    const size_t need = sizeof(oec_frame_header_t) + payload;

    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/false);
    if (!slot || cap < need) return;

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, OEC_STREAM_ERROR, (uint32_t)payload, 0, qpc_now(),
                   t.consumePendingFlags());
    uint8_t* body = slot + sizeof(*h);
    std::memcpy(body + 0, &code, sizeof(code));
    std::memcpy(body + 2, &textlen, sizeof(textlen));
    std::memcpy(body + 4, msg, textlen);
    t.publishData((uint32_t)need);
}

void writeTtlEventFrame(ITransport& t,
                        uint8_t line, uint8_t edge, uint8_t board_id,
                        uint64_t sample_index)
{
    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/true);
    if (!slot) return;

    const uint8_t body[4] = { line, edge, board_id, 0 };
    const size_t need = sizeof(oec_frame_header_t) + sizeof(body);
    if (cap < need) return;

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, OEC_STREAM_TTL_EVENT, (uint32_t)sizeof(body),
                   sample_index, qpc_now(), t.consumePendingFlags());
    std::memcpy(slot + sizeof(*h), body, sizeof(body));
    t.publishData((uint32_t)need);
}

void writeSpikeFrame(ITransport& t,
                     uint16_t electrode, uint16_t unit, float threshold,
                     const int16_t* waveform, uint32_t n_samples,
                     uint64_t sample_index)
{
    /* Body: electrode_u16 + unit_u16 + threshold_f32 + int16[n_samples]. */
    constexpr size_t kSpikeHead = 2 + 2 + 4;
    const size_t payload = kSpikeHead + (size_t)n_samples * sizeof(int16_t);
    const size_t need = sizeof(oec_frame_header_t) + payload;

    uint32_t cap = 0;
    uint8_t* slot = t.acquireDataSlot(&cap, /*dropOldest=*/true);
    if (!slot) return;
    if (cap < need) return;   /* waveform too large for one slot; drop it */

    auto* h = reinterpret_cast<oec_frame_header_t*>(slot);
    oec_frame_init(h, OEC_STREAM_SPIKE, (uint32_t)payload,
                   sample_index, qpc_now(), t.consumePendingFlags());

    uint8_t* body = slot + sizeof(*h);
    std::memcpy(body + 0, &electrode, 2);
    std::memcpy(body + 2, &unit, 2);
    std::memcpy(body + 4, &threshold, 4);
    if (n_samples && waveform)
        std::memcpy(body + kSpikeHead, waveform, (size_t)n_samples * sizeof(int16_t));

    t.publishData((uint32_t)need);
}

void processControl(
    uint64_t sample_index,
    const ProcessorConfig& cfg,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox,
    PulseScheduler* pulses)
{
    /* Clear any TTL pulses whose width has elapsed by the end of this block. */
    if (pulses) {
        pulses->serviceDue(sample_index, cfg.block_size, board, cfg.on_ttl_emit);
    }

    drainCmdRing(transport, board, cfg, sample_index, pulses);
    drainAckOutbox(transport, outbox, cfg);
}

void processBlock(
    const int16_t* input,
    uint64_t sample_index,
    const ProcessorConfig& cfg,
    ITransport& transport,
    IBoardAdapter& board,
    AckOutbox& outbox,
    PulseScheduler* pulses)
{
    processControl(sample_index, cfg, transport, board, outbox, pulses);

    /* Single-stream convenience path: publish this node's one buffer, stamped with
     * whichever stream id it is labelled with. The JUCE processor does not use this
     * -- it publishes one block per DataStream via writeContinuousBlock(). */
    writeContinuousBlock(transport, cfg, input,
                         (uint16_t)cfg.num_channels, (uint16_t)cfg.block_size,
                         sample_index, cfg.source_id);
}

}  // namespace oec::plugin
