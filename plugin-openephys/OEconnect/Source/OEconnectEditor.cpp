#include "OEconnectEditor.h"
#include <cstdio>

namespace oec::plugin {

#define OECDIAG(msg) do { std::FILE* _f = std::fopen("oecdiag.log", "a"); \
    if (_f) { std::fprintf(_f, "%s\n", msg); std::fclose(_f); } } while (0)

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    OECDIAG("editor ctor enter");
    /* BISECT(test1): minimal editor — no custom components, no timer. */
    desiredWidth = 280;
    OECDIAG("editor ctor exit");
}

void OEconnectEditor::timerCallback() {
    auto* p = proc();
    if (!p) return;
    String s;
    s << "drops: " << (int64)p->droppedFrames()
      << "  mode: " << String(p->activeTransport())
      << "  board: " << String(p->activeBoard());
    status_.setText(s, dontSendNotification);
}

void OEconnectEditor::paint(Graphics& g) {
    GenericEditor::paint(g);
}

void OEconnectEditor::resized() {
    GenericEditor::resized();
    auto area = getLocalBounds().reduced(6);
    int y = 30;
    transport_box_.setBounds(area.getX(),       y, 120, 24);
    zmq_port_.setBounds      (area.getX() + 130, y, 60,  24);
    y += 28;
    bind_addr_.setBounds     (area.getX(),       y, 120, 24);
    auth_toggle_.setBounds   (area.getX() + 130, y, 24,  24);
    y += 28;
    stream_raw_.setBounds (area.getX(),      y, 60, 24);
    stream_filt_.setBounds(area.getX() + 60, y, 60, 24);
    stream_spk_.setBounds (area.getX() + 120, y, 60, 24);
    stream_ttl_.setBounds (area.getX() + 180, y, 60, 24);
    y += 28;
    block_size_.setBounds(area.getX(),       y, 100, 24);
    slot_count_.setBounds(area.getX() + 110, y, 60,  24);
    y += 28;
    status_.setBounds(area.getX(), y, area.getWidth(), 40);
}

AudioProcessorEditor* createOEconnectEditor(OEconnectJuceProcessor* p) {
    return new OEconnectEditor(p);
}

}  // namespace oec::plugin
