#pragma once
/*
 * JUCE GenericProcessor wrapper for OEconnect.
 *
 * This translation unit is compiled only when the OEconnect bundle is built
 * (OEC_PLUGIN_BUILD_BUNDLE=ON); the unit-test executable excludes it because
 * it has no JUCE/plugin-GUI toolchain wired in. The hot-path implementation
 * proper lives in OEconnectProcessor.h/.cpp and is pure C++.
 */

#include "OEconnectProcessor.h"
#include "Boards/EventBusTtlAdapter.h"
#include "Compat/OECompat.h"             /* plugin API v8 / v10 shim */
#include "Util/SlowCmdWorker.h"
#include "Util/PulseScheduler.h"
#include <ProcessorHeaders.h>            /* from external/plugin-GUI */

namespace oec::plugin {

class OEconnectEditor;   /* fwd */

class OEconnectJuceProcessor : public GenericProcessor,
                               public ITtlEventEmitter {
public:
    OEconnectJuceProcessor();
    ~OEconnectJuceProcessor() override;

    AudioProcessorEditor* createEditor() override;
    void updateSettings() override;
    void process(AudioBuffer<float>& buffer) override;
    bool startAcquisition() override;
    bool stopAcquisition() override;

    /** Declares the editor-visible parameters. Each carries a description (shown
     *  as the control's tooltip) and is saved/restored with the signal chain.
     *  Called from registerParameters() on API v10, from the constructor on v8. */
    void registerOecParameters();
    OEC_REGISTER_PARAMS_HOOK

    /** Pushes a changed parameter into cfg_. */
    void parameterValueChanged(Parameter* param) override;

    /* --- ITtlEventEmitter. GenericProcessor::setTTLState and broadcastMessage
     *     are protected, so only this subclass can reach them. --- */
    void emitTtlEdge(int sample_in_block, int line, bool state) override {
        setTTLState(sample_in_block, line, state);
    }
    void sendBoardTrigger(int line, int width_ms) override {
        /* Documented Open Ephys acquisition-board remote-control grammar.
         * Delivered only while acquisition is active; ignored by boards that do
         * not implement handleBroadcastMessage(). */
        broadcastMessage("ACQBOARD TRIGGER " + String(line) + " " + String(width_ms));
    }

    /** Called by the editor when UI knobs change. */
    void applyConfig(const ProcessorConfig& cfg);

    /** Forwarded by Editor to inspect status. */
    uint64_t droppedFrames() const;
    std::string activeTransport() const;
    std::string activeBoard() const;

private:
    void selectBoardAdapter();
    void selectTransport();
    /** Rebuilds cfg_.zmq_endpoint from the bind-address / port parameters. */
    void rebuildZmqEndpoint();

    ProcessorConfig cfg_;
    std::unique_ptr<ITransport>     transport_;
    std::unique_ptr<IBoardAdapter>  board_;
    /* Non-owning view of board_ (always the event-bus adapter in the shipped
     * plugin); lets us push per-block state without a downcast. */
    EventBusTtlAdapter*             ttl_adapter_ = nullptr;
    std::unique_ptr<DriftEmitter>   drift_emitter_;
    std::unique_ptr<SlowCmdWorker>  slow_worker_;
    AckOutbox                       outbox_{1024};
    std::atomic<uint64_t>           sample_counter_{0};
    std::vector<int16_t>            scratch_;
    PulseScheduler                  pulse_sched_;
};

}  // namespace oec::plugin
