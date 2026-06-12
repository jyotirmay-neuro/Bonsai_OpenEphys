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
#include "Util/SlowCmdWorker.h"
#include <ProcessorHeaders.h>            /* from external/plugin-GUI */

namespace oec::plugin {

class OEconnectEditor;   /* fwd */

class OEconnectJuceProcessor : public GenericProcessor {
public:
    OEconnectJuceProcessor();
    ~OEconnectJuceProcessor() override;

    AudioProcessorEditor* createEditor() override;
    void updateSettings() override;
    void process(AudioBuffer<float>& buffer) override;
    bool startAcquisition() override;
    bool stopAcquisition() override;

    /** Called by the editor when UI knobs change. */
    void applyConfig(const ProcessorConfig& cfg);

    /** Forwarded by Editor to inspect status. */
    uint64_t droppedFrames() const;
    std::string activeTransport() const;
    std::string activeBoard() const;

private:
    void selectBoardAdapter();
    void selectTransport();

    ProcessorConfig cfg_;
    std::unique_ptr<ITransport>     transport_;
    std::unique_ptr<IBoardAdapter>  board_;
    std::unique_ptr<DriftEmitter>   drift_emitter_;
    std::unique_ptr<SlowCmdWorker>  slow_worker_;
    AckOutbox                       outbox_{1024};
    std::atomic<uint64_t>           sample_counter_{0};
    std::vector<int16_t>            scratch_;
};

}  // namespace oec::plugin
