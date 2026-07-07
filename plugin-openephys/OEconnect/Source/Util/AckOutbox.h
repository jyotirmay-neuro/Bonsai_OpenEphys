#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace oec::plugin {

struct AckEntry {
    uint32_t cookie;
    uint16_t status;
    uint16_t cmd_id;              /* repurposed as stream id for SYNC entries */
    uint64_t sample_index;
    uint64_t host_qpc_ticks;
    /* SYNC-only payload (spec §3.1 SYNC body). Ignored for ACK entries. */
    uint64_t qpc_freq_hz;
    double   fpga_sample_rate_hz;
};

/**
 * Bounded lock-free MPSC queue keyed by sequence numbers (Vyukov MPMC variant
 * restricted to single consumer). Capacity must be a power of two.
 */
class AckOutbox {
public:
    explicit AckOutbox(size_t capacity)
        : capacity_(capacity), mask_(capacity - 1),
          cells_(capacity), enqueue_pos_(0), dequeue_pos_(0)
    {
        for (size_t i = 0; i < capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool tryPush(const AckEntry& e) noexcept {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells_[pos & mask_];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = e;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; /* full */
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(AckEntry& out) noexcept {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell& cell = cells_[pos & mask_];
        size_t seq = cell.sequence.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        if (diff == 0) {
            out = cell.data;
            cell.sequence.store(pos + mask_ + 1, std::memory_order_release);
            dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

private:
    struct Cell {
        std::atomic<size_t> sequence;
        AckEntry data;
    };
    const size_t capacity_;
    const size_t mask_;
    std::vector<Cell> cells_;
    alignas(64) std::atomic<size_t> enqueue_pos_;
    alignas(64) std::atomic<size_t> dequeue_pos_;
};

}  // namespace oec::plugin
