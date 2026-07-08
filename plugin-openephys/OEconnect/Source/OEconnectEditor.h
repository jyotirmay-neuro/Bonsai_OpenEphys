#pragma once
/*
 * JUCE editor UI for OEconnect. Compiled only when OEC_PLUGIN_BUILD_BUNDLE=ON.
 *
 * All configurable knobs are declared as OE Parameters in
 * OEconnectJuceProcessor::registerParameters(). This editor only lays out the
 * parameter editors the framework builds from them (so each control gets its
 * displayName as a label and its description as a tooltip, and is saved and
 * restored with the signal chain), plus a read-only status line.
 */

#include <EditorHeaders.h>
#include "OEconnectJuceProcessor.h"

namespace oec::plugin {

class OEconnectEditor final : public GenericEditor,
                              public Timer
{
public:
    explicit OEconnectEditor(GenericProcessor* p);
    ~OEconnectEditor() override;
    void timerCallback() override;
    void resized() override;

private:
    OEconnectJuceProcessor* proc() {
        return static_cast<OEconnectJuceProcessor*>(getProcessor());
    }

    /* Live telemetry only; every editable knob is a registered Parameter. */
    Label status_;
};

/* Returns GenericEditor* so the processor can take ownership via its
 * std::unique_ptr<GenericEditor> editor member (see GenericProcessor). */
GenericEditor* createOEconnectEditor(OEconnectJuceProcessor* p);

}  // namespace oec::plugin
