#include "OEconnectEditor.h"
#include <cstdio>

namespace oec::plugin {

#define OECDIAG(msg) do { std::FILE* _f = std::fopen("oecdiag.log", "a"); \
    if (_f) { std::fprintf(_f, "%s\n", msg); std::fclose(_f); } } while (0)

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    OECDIAG("editor ctor enter");
    desiredWidth = 340;

    /* Parameter editors are built by the framework from the declarations in
     * OEconnectJuceProcessor::registerParameters(); each shows its displayName
     * and exposes its description as a tooltip on hover. */
    addComboBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "transport", 10, 28);

    addToggleParameterEditor(Parameter::PROCESSOR_SCOPE, "stream_raw", 170, 30);
    addToggleParameterEditor(Parameter::PROCESSOR_SCOPE, "stream_filtered", 170, 52);

    addTextBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "zmq_bind", 10, 78);
    addTextBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "zmq_data_port", 150, 78);
    addTextBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "zmq_cmd_port", 245, 78);

    status_.setText("idle", dontSendNotification);
    status_.setJustificationType(Justification::topLeft);
    status_.setFont(FontOptions(12.0f));
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
    status_.setBounds(area.getX(), 122, area.getWidth(), 34);
}

GenericEditor* createOEconnectEditor(OEconnectJuceProcessor* p) {
    return new OEconnectEditor(p);
}

}  // namespace oec::plugin
