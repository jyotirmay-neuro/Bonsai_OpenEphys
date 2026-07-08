#include "OEconnectJuceProcessor.h"

/* Needed for the complete GenericEditor type: GenericProcessor.h only
 * forward-declares it, so createEditor() below can't otherwise convert
 * GenericEditor* -> AudioProcessorEditor*. */
#include <EditorHeaders.h>

#include "Boards/EventBusTtlAdapter.h"
#include "Transport/ShmemTransport.h"
#include "Transport/ZmqTransport.h"

#include <cstring>
#include <cstdio>

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

/* DIAG: flush a marker to oecdiag.log (fclose guarantees flush even before a crash). */
#define OECDIAG(msg) do { std::FILE* _f = std::fopen("oecdiag.log", "a"); \
    if (_f) { std::fprintf(_f, "%s\n", msg); std::fclose(_f); } } while (0)

/* Defined in OEconnectEditor.cpp (same namespace). Declared at namespace scope
 * so the call below binds to oec::plugin::createOEconnectEditor, not a
 * block-scope extern that would resolve to the global namespace. */
GenericEditor* createOEconnectEditor(OEconnectJuceProcessor*);

OEconnectJuceProcessor::OEconnectJuceProcessor() : GenericProcessor("OEconnect") { OECDIAG("ctor done"); }
OEconnectJuceProcessor::~OEconnectJuceProcessor() {
    if (transport_) transport_->stop();
}

/* Categories must stay in sync with the switch in parameterValueChanged(). */
static const juce::StringArray kTransportModes { "Auto", "SharedMem", "Zmq" };

void OEconnectJuceProcessor::registerParameters() {
    addCategoricalParameter(
        Parameter::PROCESSOR_SCOPE, "transport", "Transport",
        "How samples reach Bonsai. Auto: shared memory if Bonsai runs on this "
        "machine, else ZMQ. SharedMem: lock-free shared ring, sub-millisecond, "
        "same host only. Zmq: TCP sockets, works across machines, 1-5 ms typical.",
        Array<String>(kTransportModes.begin(), kTransportModes.size()),
        /*defaultIndex=*/0, /*deactivateDuringAcquisition=*/true);

    addBooleanParameter(
        Parameter::PROCESSOR_SCOPE, "stream_raw", "Stream raw",
        "Publish the unprocessed broadband block each callback (stream id "
        "RAW_BLOCK). This is what Bonsai's RawSamples node receives.",
        /*defaultValue=*/true, /*deactivateDuringAcquisition=*/false);

    addBooleanParameter(
        Parameter::PROCESSOR_SCOPE, "stream_filtered", "Stream filtered",
        "Publish a second copy of the incoming block tagged FILTERED_BLOCK. "
        "NOTE: this node does not filter - it forwards whatever the upstream "
        "chain already produced. Place a Bandpass Filter before OEconnect to "
        "make this meaningful, otherwise it duplicates the raw stream.",
        /*defaultValue=*/false, /*deactivateDuringAcquisition=*/false);

    addBooleanParameter(
        Parameter::PROCESSOR_SCOPE, "direct_board_trigger", "Direct board trigger",
        "In addition to the TTL event (always emitted), broadcast the Open Ephys "
        "acquisition board's remote-control command so the board fires the pulse "
        "itself, skipping the downstream output plugin's response time. Applies to "
        "PulseTtl only - the board's command grammar takes a duration and cannot "
        "latch a line, so SetTtl always goes via the event bus. Boards that do not "
        "understand the command ignore it, so this is harmless to leave on.",
        /*defaultValue=*/false, /*deactivateDuringAcquisition=*/false);

    addStringParameter(
        Parameter::PROCESSOR_SCOPE, "zmq_bind", "ZMQ bind address",
        "Interface the ZMQ sockets bind to. 127.0.0.1 keeps the data and "
        "control channels on this machine (no authentication needed). Any "
        "routable address (e.g. 0.0.0.0) exposes them to the network and is "
        "REFUSED unless the OEC_ZMQ_CURVE_SECRET environment variable holds a "
        "40-character CURVE key. Ignored when Transport is SharedMem.",
        "127.0.0.1", /*deactivateDuringAcquisition=*/true);

    addIntParameter(
        Parameter::PROCESSOR_SCOPE, "zmq_data_port", "ZMQ data port",
        "TCP port for the outbound data stream (PUB socket). Bonsai subscribes "
        "here. Ignored when Transport is SharedMem.",
        5557, 1024, 65535, /*deactivateDuringAcquisition=*/true);

    addIntParameter(
        Parameter::PROCESSOR_SCOPE, "zmq_cmd_port", "ZMQ command port",
        "TCP port for the inbound command channel (REP socket) carrying "
        "START_RECORD / TTL commands from Bonsai. Must differ from the data "
        "port. Ignored when Transport is SharedMem.",
        5558, 1024, 65535, /*deactivateDuringAcquisition=*/true);

    rebuildZmqEndpoint();
}

void OEconnectJuceProcessor::rebuildZmqEndpoint() {
    auto* bind = getParameter("zmq_bind");
    auto* dport = getParameter("zmq_data_port");
    auto* cport = getParameter("zmq_cmd_port");
    if (!bind || !dport || !cport) return;

    const String addr = bind->getValue().toString();
    const int data_port = (int)dport->getValue();
    const int cmd_port  = (int)cport->getValue();

    cfg_.zmq_endpoint = ("tcp://" + addr + ":" + String(data_port) +
                         "|tcp://" + addr + ":" + String(cmd_port)).toStdString();
}

void OEconnectJuceProcessor::parameterValueChanged(Parameter* param) {
    if (param == nullptr) return;
    const String name = param->getName();

    if (name.equalsIgnoreCase("transport")) {
        const int idx = (int)param->getValue();
        if (idx >= 0 && idx < kTransportModes.size())
            cfg_.transport_mode = kTransportModes[idx].toStdString();
    } else if (name.equalsIgnoreCase("stream_raw")) {
        cfg_.enable_raw = (bool)param->getValue();
    } else if (name.equalsIgnoreCase("stream_filtered")) {
        cfg_.enable_filtered = (bool)param->getValue();
    } else if (name.equalsIgnoreCase("direct_board_trigger")) {
        if (ttl_adapter_) ttl_adapter_->setDirectTrigger((bool)param->getValue());
    } else if (name.equalsIgnoreCase("zmq_bind") ||
               name.equalsIgnoreCase("zmq_data_port") ||
               name.equalsIgnoreCase("zmq_cmd_port")) {
        rebuildZmqEndpoint();
    }
}

AudioProcessorEditor* OEconnectJuceProcessor::createEditor() {
    OECDIAG("createEditor enter");
    /* GenericProcessor owns the editor through its `editor` unique_ptr, and
     * getEditor() returns editor.get(). Returning a raw editor without storing
     * it here left getEditor() == nullptr, so the GUI null-dereferenced as soon
     * as the node was added to the signal chain (and the editor leaked). */
    editor.reset(createOEconnectEditor(this));
    OECDIAG("createEditor exit");
    return editor.get();
}

void OEconnectJuceProcessor::updateSettings() {
    OECDIAG("updateSettings enter");
    cfg_.num_channels   = getNumInputs();
    OECDIAG("updateSettings after getNumInputs");
    cfg_.sample_rate_hz = (getNumDataStreams() > 0) ? getSampleRate(0) : 30000.0;
    OECDIAG("updateSettings after getSampleRate");

    /* Publish a TTL event channel on the first incoming stream so downstream
     * plugins (Acq Board Output, Arduino Output, Pulse Pal, ...) and Record Nodes
     * can see every edge the bridge issues. This is the only board-agnostic route
     * from a plugin to a physical digital output. */
    addTTLChannel("OEconnect TTL");
    OECDIAG("updateSettings after addTTLChannel");

    selectBoardAdapter();
    OECDIAG("updateSettings after selectBoardAdapter");
}

void OEconnectJuceProcessor::selectBoardAdapter() {
    OECDIAG("selectBoardAdapter enter");
    /* TTL output is delivered via OE's event bus regardless of which board is
     * upstream, so there is nothing board-specific to select. We only record the
     * upstream source's name for the status line. */
    GenericProcessor* src = getSourceNode();
    const std::string board_name =
        src ? src->getName().toStdString() : std::string("(no source)");

    auto adapter = std::make_unique<EventBusTtlAdapter>(
        static_cast<ITtlEventEmitter*>(this), board_name);
    if (auto* p = getParameter("direct_board_trigger"))
        adapter->setDirectTrigger((bool)p->getValue());
    ttl_adapter_ = adapter.get();
    board_ = std::move(adapter);
    OECDIAG("selectBoardAdapter end");
}

void OEconnectJuceProcessor::selectTransport() {
    if (cfg_.transport_mode == "Zmq") {
        transport_ = std::make_unique<ZmqTransport>();
        transport_->start(cfg_.zmq_endpoint);
    } else {   /* "Auto" and "SharedMem" both prefer shmem, fall back to ZMQ. */
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

    /* The edge itself is published by EventBusTtlAdapter::setTtl() via
     * setTTLState(), which is what puts it on OE's event bus and into the
     * recording. This hook is now pure observation (telemetry / tests). */
    cfg_.on_ttl_emit = nullptr;

    slow_worker_ = std::make_unique<SlowCmdWorker>(outbox_,
        [this](const SlowCmdRequest& req) -> uint16_t {
            switch (req.cmd_id) {
                case OEC_CMD_START_RECORD:
                    /* applyToAll=true ignores the nodeId and targets every
                       Record Node in the chain. */
                    CoreServices::RecordNode::setRecordingDirectory(
                        juce::String(req.arg1), /*nodeId=*/0, /*applyToAll=*/true);
                    /* TODO(impl): forward the file-name prefix (req.arg2) via
                       CoreServices::setRecordingDirectoryPrependText / ...AppendText
                       once the desired naming semantics are settled. */
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
    /* Convert float -> int16 chan-major.
     *
     * OE continuous buffers hold MICROVOLTS, not normalised [-1,1] audio.
     * Scaling by 32767 (as if this were audio) saturates every sample above
     * ~1 uV, which clips real ephys into a square wave. Divide by the channel's
     * bitVolts instead to recover the raw ADC counts the recorder stores.
     * Consumers multiply by bitVolts to get microvolts back. */
    for (int c = 0; c < ch; ++c) {
        const float* in = buffer.getReadPointer(c);
        int16_t* out = scratch_.data() + c * ns;

        const ContinuousChannel* chan = getContinuousChannel(c);
        const float bit_volts = (chan && chan->getBitVolts() > 0.0f)
                                    ? chan->getBitVolts() : 1.0f;
        const float inv_bit_volts = 1.0f / bit_volts;

        for (int s = 0; s < ns; ++s) {
            float v = in[s] * inv_bit_volts;
            if (v > 32767.f) v = 32767.f; else if (v < -32768.f) v = -32768.f;
            out[s] = (int16_t)v;
        }
    }
    const uint64_t s0 = sample_counter_.fetch_add((uint64_t)ns, std::memory_order_relaxed);
    cfg_.num_channels = ch;
    cfg_.block_size   = ns;
    /* So TTL edges emitted this block can report an absolute sample index. */
    if (ttl_adapter_) ttl_adapter_->setBlockStartSample(s0);
    /* Qualify: unqualified 'processBlock' would bind to the inherited
       GenericProcessor::processBlock member, not our free hot-path function. */
    oec::plugin::processBlock(scratch_.data(), s0, cfg_, *transport_, *board_,
                              outbox_, &pulse_sched_);
}

void OEconnectJuceProcessor::applyConfig(const ProcessorConfig& cfg) { cfg_ = cfg; }
uint64_t OEconnectJuceProcessor::droppedFrames() const { return transport_ ? transport_->totalDropped() : 0; }
std::string OEconnectJuceProcessor::activeTransport() const { return transport_ ? transport_->name() : "-"; }
std::string OEconnectJuceProcessor::activeBoard() const { return board_ ? board_->name() : "-"; }

}  // namespace oec::plugin
