#pragma once
#include "Boards/IBoardAdapter.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace oec::plugin {

/**
 * Tracks TTL pulses that must be auto-cleared after a fixed width. The audio
 * thread cannot sleep, so PULSE_TTL asserts the line immediately and records a
 * pending clear keyed by the FPGA sample index at which the line must flip
 * back. `serviceDue()` is called at the top of every process() tick and clears
 * any pulse whose deadline the block has reached.
 *
 * Not thread-safe: only ever touched from the audio thread.
 */
class PulseScheduler {
public:
    /** Assert `line` now (edge `high`) and schedule the opposite edge after
     *  `width_us`. `now_sample` is the block's starting sample index;
     *  `sample_rate_hz` converts the width into a sample count. */
    void arm(IBoardAdapter& board, uint8_t line, bool high,
             uint32_t width_us, uint64_t now_sample, double sample_rate_hz,
             const std::function<void(uint8_t, uint8_t, uint64_t)>& on_edge)
    {
        uint64_t s = board.setTtl(line, high);
        if (on_edge) on_edge(line, high ? 1 : 0, s);

        uint64_t width_samples =
            (sample_rate_hz > 0.0)
                ? (uint64_t)((double)width_us * sample_rate_hz / 1'000'000.0 + 0.5)
                : 0;
        if (width_samples == 0) width_samples = 1;  /* never zero-width */
        pending_.push_back(Pending{ line, !high, now_sample + width_samples });
    }

    /** Clear any pulse whose deadline `<= block_end_sample`. */
    void serviceDue(uint64_t block_end_sample, IBoardAdapter& board,
                    const std::function<void(uint8_t, uint8_t, uint64_t)>& on_edge)
    {
        for (size_t i = 0; i < pending_.size();) {
            if (pending_[i].clear_at_sample <= block_end_sample) {
                uint64_t s = board.setTtl(pending_[i].line, pending_[i].clear_high);
                if (on_edge) on_edge(pending_[i].line,
                                     pending_[i].clear_high ? 1 : 0, s);
                pending_[i] = pending_.back();
                pending_.pop_back();
            } else {
                ++i;
            }
        }
    }

    bool empty() const { return pending_.empty(); }

private:
    struct Pending {
        uint8_t  line;
        bool     clear_high;         /* edge to drive at the deadline */
        uint64_t clear_at_sample;
    };
    std::vector<Pending> pending_;
};

}  // namespace oec::plugin
