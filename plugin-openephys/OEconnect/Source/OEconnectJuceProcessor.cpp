#include "OEconnectJuceProcessor.h"

#include "Boards/FileReaderAdapter.h"
#include "Boards/RhdAcqBoardAdapter.h"
#include "Boards/OnixAdapter.h"
#include "Boards/NeuropixelsAdapter.h"
#include "Transport/ShmemTransport.h"
#include "Transport/ZmqTransport.h"

#include <cstring>

extern "C" {
#include "oeconnect/sidecar.h"
#include "oeconnect/shm.h"
#include "oeconnect/hello.h"
#include "oeconnect/frame.h"
#include "oeconnect/version.h"
}

#ifndef OEC_PLUGIN_VERSION_MAJOR
#  define OEC_PLUGIN_VERSION_MAJOR 1
#endif
#ifndef OEC_PLUGIN_VERSION_MINOR
#  define OEC_PLUGIN_VERSION_MINOR 0
#endif
#ifndef OEC_PLUGIN_VERSION_PATCH
#  define OEC_PLUGIN_VERSION_PATCH 0
#endif

#if defined(_WIN32)
  #include <process.h>
  #define getpid _getpid
#else
  #include <unistd.h>
#endif

namespace oec::plugin {

OEconnectJuceProcessor::OEconnectJuceProcessor() : GenericProcessor("OEconnect") {}
OEconnectJuceProcessor::~OEconnectJuceProcessor() {
    if (transport_) transport_->stop();
}

AudioProcessorEditor* OEconnectJuceProcessor::createEditor() {
    /* Concrete editor defined in OEconnectEditor.cpp */
    extern AudioProcessorEditor* createOEconnectEditor(OEconnectJuceProcessor*);
    return createOEconnectEditor(this);
}

void OEconnectJuceProcessor::updateSettings() {
    cfg_.num_channels   = getNumInputs();
    cfg_.sample_rate_hz = getSampleRate();
    selectBoardAdapter();
}

void OEconnectJuceProcessor::selectBoardAdapter() {
    GenericProcessor* src = getSourceNode();
    if (!src) { board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv"); return; }
    const String n = src->getName();
    if      (n.containsIgnoreCase("rhythm"))      board_ = std::make_unique<RhdAcqBoardAdapter>(src);
    else if (n.containsIgnoreCase("onix"))        board_ = std::make_unique<OnixAdapter>(src);
    else if (n.containsIgnoreCase("neuropixels")) board_ = std::make_unique<NeuropixelsAdapter>(src, nullptr);
    else if (n.containsIgnoreCase("file reader")) board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv");
    else                                          board_ = std::make_unique<FileReaderAdapter>("ttl_out_log.csv");
}

void OEconnectJuceProcessor::selectTransport() {
    if (cfg_.transport_mode == "Zmq") {
        transport_ = std::make_unique<ZmqTransport>();
        transport_->start(cfg_.zmq_endpoint);
    } else {
        char name[64];
        oec_shm_make_name((int)getpid(), name, sizeof(name));
        cfg_.shm_name = name;
        transport_ = std::make_unique<ShmemTransport>();
        if (!transport_->start(cfg_.shm_name)) {
            transport_ = std::make_unique<ZmqTransport>();
            transport_->start(cfg_.zmq_endpoint);
        }
    }
}

bool OEconnectJuceProcessor::startAcquisition() {
    selectTransport();

    /* Minimum-support gate: refuse to start against a board SDK / firmware
     * older than this OEconnect release supports, rather than risk silently
     * corrupting data. See the plugin README "Observation-only mode" for the
     * workaround during firmware-upgrade windows. */
    if (board_ && !board_->meetsMinimumSdk()) {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
            "OEconnect",
            "Board SDK '" + String(board_->sdkVersionString()) +
            "' is older than the minimum supported by this OEconnect release. " +
            "Upgrade the board firmware/SDK, or run in observation-only mode "
            "(remove this signal chain's TTL outputs and slow-cmd sinks).");

        /* Also emit one OEC_STREAM_ERROR frame so Bonsai sees the reason.
         * Body: [uint16 code][utf8 text...] (no trailing NUL). */
        if (transport_) {
            uint32_t dcap = 0;
            if (uint8_t* dslot = transport_->acquireDataSlot(&dcap, /*dropOldest=*/false)) {
                const char*    msg     = board_->sdkVersionString();
                const uint16_t code    = OEC_ACK_NOT_SUPPORTED;
                const size_t   textlen = std::strlen(msg);
                const size_t   payload = sizeof(code) + textlen;
                if (dcap >= sizeof(oec_frame_header_t) + payload) {
                    oec_frame_header_t h;
                    oec_frame_init(&h, OEC_STREAM_ERROR, (uint32_t)payload, 0, 0, 0);
                    std::memcpy(dslot, &h, sizeof(h));
                    std::memcpy(dslot + sizeof(h), &code, sizeof(code));
                    std::memcpy(dslot + sizeof(h) + sizeof(code), msg, textlen);
                    transport_->publishData((uint32_t)(sizeof(h) + payload));
                }
            }
        }
        return false;
    }

    if (board_) board_->onStartAcquisition(cfg_.block_size, cfg_.sample_rate_hz);

    /* Emit one HELLO frame on the data ring so consumers can negotiate
     * protocol/version before any RAW frame arrives. Spec v1.1 §8.
     * v1.0 consumers route the unknown stream id through their default
     * discard arm. */
    if (transport_) {
        uint32_t dcap = 0;
        if (uint8_t* dslot = transport_->acquireDataSlot(&dcap, /*dropOldest=*/false)) {
            const uint32_t plugin_ver =
                ((uint32_t)OEC_PLUGIN_VERSION_MAJOR << 16) |
                ((uint32_t)OEC_PLUGIN_VERSION_MINOR << 8)  |
                ((uint32_t)OEC_PLUGIN_VERSION_PATCH);
            const uint32_t lib_ver =
                ((uint32_t)OEC_LIB_VERSION_MAJOR << 16) |
                ((uint32_t)OEC_LIB_VERSION_MINOR << 8)  |
                ((uint32_t)OEC_LIB_VERSION_PATCH);
            size_t n = oec_hello_emit(dslot, dcap, plugin_ver, lib_ver);
            if (n > 0) transport_->publishData((uint32_t)n);
        }
    }

    drift_emitter_ = std::make_unique<DriftEmitter>(outbox_, sample_counter_);
    drift_emitter_->start(cfg_.sample_rate_hz);

    cfg_.on_ttl_emit = [this](uint8_t line, uint8_t edge, uint64_t /*s*/) {
        /* TODO(impl): exact addEvent() signature depends on plugin-GUI version.
           Look in external/plugin-GUI/Source/Processors/GenericProcessor.h
           for the current API; typical pattern:
             addEvent(eventChannel, sample_within_block, line | (edge << 8));
           Must remain wait-free -- OE's addEvent is a fixed-size lock-free push
           onto the event bus. */
        (void)this; (void)line; (void)edge;
    };

    slow_worker_ = std::make_unique<SlowCmdWorker>(outbox_,
        [this](const SlowCmdRequest& req) -> uint16_t {
            switch (req.cmd_id) {
                case OEC_CMD_START_RECORD:
                    CoreServices::setRecordingDirectory(juce::String(req.arg1));
                    /* TODO(impl): the CoreServices method for the file-name
                       prefix varies across plugin-GUI minor versions.
                       Look in external/plugin-GUI/Source/CoreServices.h for
                       the current API and forward req.arg2 to it. */
                    CoreServices::setRecordingStatus(true);
                    return OEC_ACK_COMPLETED;
                case OEC_CMD_STOP_RECORD:
                    CoreServices::setRecordingStatus(false);
                    return OEC_ACK_COMPLETED;
                case OEC_CMD_START_ACQ:
                    CoreServices::setAcquisitionStatus(true);
                    return OEC_ACK_COMPLETED;
                case OEC_CMD_STOP_ACQ:
                    CoreServices::setAcquisitionStatus(false);
                    return OEC_ACK_COMPLETED;
                default:
                    return OEC_ACK_NOT_SUPPORTED;
            }
        });
    slow_worker_->start();
    cfg_.slow_enqueue = [this](SlowCmdRequest r) { slow_worker_->enqueue(std::move(r)); };

    /* Write sidecar JSON */
    oec_sidecar_t s{};
    s.pid = (int)getpid();
    std::strncpy(s.shm_region, cfg_.shm_name.c_str(), sizeof(s.shm_region) - 1);
    std::strncpy(s.zmq_fallback_endpoint, cfg_.zmq_endpoint.c_str(), sizeof(s.zmq_fallback_endpoint) - 1);
    std::strncpy(s.spec_version, "1.0", sizeof(s.spec_version) - 1);
    oec_sidecar_write(&s);
    return true;
}

bool OEconnectJuceProcessor::stopAcquisition() {
    if (slow_worker_) slow_worker_->stop();
    if (drift_emitter_) drift_emitter_->stop();
    if (board_) board_->onStopAcquisition();
    if (transport_) transport_->stop();
    oec_sidecar_remove((int)getpid());
    return true;
}

void OEconnectJuceProcessor::process(AudioBuffer<float>& buffer) {
    const int ch = buffer.getNumChannels();
    const int ns = buffer.getNumSamples();
    if (scratch_.size() < (size_t)(ch * ns)) scratch_.resize((size_t)(ch * ns));
    /* Convert float -> int16 chan-major. */
    for (int c = 0; c < ch; ++c) {
        const float* in = buffer.getReadPointer(c);
        int16_t* out = scratch_.data() + c * ns;
        for (int s = 0; s < ns; ++s) {
            float v = in[s] * 32767.0f;
            if (v > 32767.f) v = 32767.f; else if (v < -32768.f) v = -32768.f;
            out[s] = (int16_t)v;
        }
    }
    const uint64_t s0 = sample_counter_.fetch_add((uint64_t)ns, std::memory_order_relaxed);
    cfg_.num_channels = ch;
    cfg_.block_size   = ns;
    processBlock(scratch_.data(), s0, cfg_, *transport_, *board_, outbox_);
}

void OEconnectJuceProcessor::applyConfig(const ProcessorConfig& cfg) { cfg_ = cfg; }
uint64_t OEconnectJuceProcessor::droppedFrames() const { return transport_ ? transport_->totalDropped() : 0; }
std::string OEconnectJuceProcessor::activeTransport() const { return transport_ ? transport_->name() : "-"; }
std::string OEconnectJuceProcessor::activeBoard() const { return board_ ? board_->name() : "-"; }

}  // namespace oec::plugin
