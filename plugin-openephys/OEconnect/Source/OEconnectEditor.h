#pragma once
/*
 * JUCE editor UI for OEconnect. Compiled only when OEC_PLUGIN_BUILD_BUNDLE=ON.
 */

#include <EditorHeaders.h>
#include "OEconnectJuceProcessor.h"

namespace oec::plugin {

class OEconnectEditor final : public GenericEditor,
                              public Timer
{
public:
    explicit OEconnectEditor(GenericProcessor* p);
    void timerCallback() override;
    void paint(Graphics& g) override;
    void resized() override;

private:
    OEconnectJuceProcessor* proc() {
        return static_cast<OEconnectJuceProcessor*>(getProcessor());
    }

    ComboBox    transport_box_;
    TextEditor  zmq_port_;
    TextEditor  bind_addr_;
    ToggleButton auth_toggle_;
    ToggleButton stream_raw_, stream_filt_, stream_spk_, stream_ttl_;
    ComboBox    block_size_;
    TextEditor  slot_count_;
    Label       status_;
};

AudioProcessorEditor* createOEconnectEditor(OEconnectJuceProcessor* p);

}  // namespace oec::plugin
