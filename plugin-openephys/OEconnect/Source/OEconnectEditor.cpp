#include "OEconnectEditor.h"
#include "Compat/OECompat.h"
#include <cstdio>

namespace oec::plugin {

#define OECDIAG(msg) do { std::FILE* _f = std::fopen("oecdiag.log", "a"); \
    if (_f) { std::fprintf(_f, "%s\n", msg); std::fclose(_f); } } while (0)

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    OECDIAG("editor ctor enter");
    desiredWidth = 350;

    /* Parameter editors are built by the framework from the declarations in
     * OEconnectJuceProcessor::registerOecParameters(); each exposes its
     * description as a tooltip on hover. The OEC_* spellings absorb the plugin
     * API v8 / v10 differences (see Compat/OECompat.h). */
    OEC_ADD_COMBO_EDITOR("transport", 10, 28);

    OEC_ADD_TOGGLE_EDITOR("stream_raw", 170, 28);
    OEC_ADD_TOGGLE_EDITOR("stream_filtered", 170, 48);
    OEC_ADD_TOGGLE_EDITOR("stream_spikes", 170, 68);
    OEC_ADD_TOGGLE_EDITOR("stream_ttl", 170, 88);
    OEC_ADD_TOGGLE_EDITOR("direct_board_trigger", 10, 56);

    OEC_ADD_TEXT_EDITOR("zmq_bind", 10, 92);
    OEC_ADD_TEXT_EDITOR("zmq_data_port", 10, 116);
    OEC_ADD_TEXT_EDITOR("zmq_cmd_port", 90, 116);

    status_.setText("idle", dontSendNotification);
    status_.setJustificationType(Justification::topLeft);
    status_.setFont(OEC_FONT(12.0f));
    addAndMakeVisible(status_);

    startTimerHz(2);
    OECDIAG("editor ctor exit");
}

OEconnectEditor::~OEconnectEditor() { stopTimer(); }

void OEconnectEditor::timerCallback() {
    auto* p = proc();
    if (!p) return;
    String s;
    s << "transport: " << String(p->activeTransport())
      << "   board: " << String(p->activeBoard())
      << "\ndropped frames: " << String((int64)p->droppedFrames());
    status_.setText(s, dontSendNotification);
}

void OEconnectEditor::resized() {
    GenericEditor::resized();
    auto area = getLocalBounds().reduced(6);
    status_.setBounds(area.getX(), 146, area.getWidth(), 34);
}

GenericEditor* createOEconnectEditor(OEconnectJuceProcessor* p) {
    return new OEconnectEditor(p);
}

}  // namespace oec::plugin
