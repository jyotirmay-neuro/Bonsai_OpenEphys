#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "oeconnect/ringbuf.h"
}

/*
 * Stress: producer writes monotonically increasing uint64_t into each slot;
 * consumer verifies in-order receipt. Default 1e9 frames; override with arg.
 * Drop-oldest disabled so any out-of-order delivery is a bug.
 */
int main(int argc, char **argv) {
    uint64_t kN = (argc > 1) ? std::stoull(argv[1]) : 1'000'000'000ULL;

    constexpr uint32_t slot_size = 64;
    constexpr uint32_t slot_count = 4096;
    std::vector<uint8_t> region(
        oec_region_size(slot_size, slot_count, 256, 4, 256, 4));
    if (oec_region_init(region.data(), region.size(),
                        slot_size, slot_count, 256, 4, 256, 4) != OEC_OK) {
        std::fputs("region init failed\n", stderr);
        return 1;
    }
    oec_ringbuf_t *prod = nullptr, *cons = nullptr;
    oec_ringbuf_attach(region.data(), OEC_RING_DATA, &prod);
    oec_ringbuf_attach(region.data(), OEC_RING_DATA, &cons);

    auto t0 = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (uint64_t i = 0; i < kN; ++i) {
            for (;;) {
                uint32_t sz = 0;
                void *slot = oec_ringbuf_acquire(prod, 0, &sz);
                if (slot) {
                    std::memcpy(slot, &i, sizeof(i));
                    oec_ringbuf_publish(prod);
                    break;
                }
                /* tight spin: this is the worst case the SPSC must handle */
            }
        }
    });

    uint64_t expected = 0;
    while (expected < kN) {
        uint32_t sz = 0;
        const void *slot = oec_ringbuf_peek(cons, &sz);
        if (!slot) continue;
        uint64_t v = 0;
        std::memcpy(&v, slot, sizeof(v));
        if (v != expected) {
            std::fprintf(stderr, "BAD: got %llu expected %llu at i=%llu\n",
                         (unsigned long long)v,
                         (unsigned long long)expected,
                         (unsigned long long)expected);
            producer.join();
            return 2;
        }
        ++expected;
        oec_ringbuf_consume(cons);
    }
    producer.join();

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    std::printf("stress OK: %llu frames in %.2f s = %.2f Mfps\n",
                (unsigned long long)kN, sec, (double)kN / sec / 1e6);
    return 0;
}
