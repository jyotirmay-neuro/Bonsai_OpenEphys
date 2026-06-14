#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

extern "C" {
#include "oeconnect/ringbuf.h"
#include "oeconnect/shm.h"
#include "oeconnect/frame.h"
}

static std::atomic<bool> g_run{true};
static void on_sigint(int) { g_run.store(false); }

int main(int argc, char** argv) {
    std::string name = (argc > 1) ? argv[1] : "/oeconnect.synth.shm";
    int n_channels = (argc > 2) ? std::atoi(argv[2]) : 8;
    int block_size = (argc > 3) ? std::atoi(argv[3]) : 32;

    std::signal(SIGINT, on_sigint);

    const size_t region_size = oec_region_size(
        OEC_DEFAULT_SLOT_SIZE, OEC_DEFAULT_SLOT_COUNT,
        OEC_DEFAULT_CMD_SLOT_SIZE, OEC_DEFAULT_CMD_SLOT_COUNT,
        OEC_DEFAULT_ACK_SLOT_SIZE, OEC_DEFAULT_ACK_SLOT_COUNT);

    oec_shm_t* shm = nullptr;
    void* mapped = nullptr;
    size_t msz = 0;
    if (oec_shm_create(name.c_str(), region_size, 1, &shm, &mapped, &msz) != OEC_OK) {
        std::fprintf(stderr, "shm_create failed\n");
        return 1;
    }
    oec_region_init(mapped, msz,
        OEC_DEFAULT_SLOT_SIZE, OEC_DEFAULT_SLOT_COUNT,
        OEC_DEFAULT_CMD_SLOT_SIZE, OEC_DEFAULT_CMD_SLOT_COUNT,
        OEC_DEFAULT_ACK_SLOT_SIZE, OEC_DEFAULT_ACK_SLOT_COUNT);

    oec_ringbuf_t* rb = nullptr;
    oec_ringbuf_attach(mapped, OEC_RING_DATA, &rb);

    std::printf("synth producer running: %s ch=%d block=%d (Ctrl-C to stop)\n",
                name.c_str(), n_channels, block_size);
    std::fflush(stdout);
    uint64_t sample = 0;
    while (g_run.load()) {
        uint32_t cap = 0;
        void* slot = oec_ringbuf_acquire(rb, /*drop_oldest=*/1, &cap);
        if (!slot) continue;
        oec_frame_header_t* h = (oec_frame_header_t*)slot;
        uint32_t payload = (uint32_t)(sizeof(oec_block_subheader_t) +
                                      (size_t)n_channels * block_size * sizeof(int16_t));
        oec_frame_init(h, OEC_STREAM_RAW_BLOCK, payload, sample, 0, 0);
        oec_block_subheader_t* sh = (oec_block_subheader_t*)((uint8_t*)slot + sizeof(*h));
        sh->n_channels = (uint16_t)n_channels;
        sh->n_samples  = (uint16_t)block_size;
        sh->dtype      = OEC_DTYPE_INT16;
        sh->source_id  = 0;
        sh->reserved   = 0;

        int16_t* data = (int16_t*)((uint8_t*)slot + sizeof(*h) + sizeof(*sh));
        for (int i = 0; i < n_channels * block_size; ++i) data[i] = (int16_t)(sample + i);
        oec_ringbuf_publish(rb);

        sample += (uint64_t)block_size;
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }

    oec_ringbuf_detach(rb);
    oec_shm_close(shm);
    oec_shm_unlink(name.c_str());
    return 0;
}
