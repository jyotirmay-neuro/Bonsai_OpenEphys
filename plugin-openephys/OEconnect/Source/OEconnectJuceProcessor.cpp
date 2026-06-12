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
}

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
    if (board_) board_->onStartAcquisition(cfg_.block_size, cfg_.sample_rate_hz);
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
