#include "OEconnectEditor.h"
#include <cstdio>

namespace oec::plugin {

#define OECDIAG(msg) do { std::FILE* _f = std::fopen("oecdiag.log", "a"); \
    if (_f) { std::fprintf(_f, "%s\n", msg); std::fclose(_f); } } while (0)

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    OECDIAG("editor ctor enter");
    desiredWidth = 280;

    transport_box_.addItem("Auto",      1);
    transport_box_.addItem("SharedMem", 2);
    transport_box_.addItem("Zmq",       3);
    transport_box_.setSelectedId(1, dontSendNotification);
    addAndMakeVisible(transport_box_);

    zmq_port_.setText("5557", dontSendNotification);
    addAndMakeVisible(zmq_port_);
    bind_addr_.setText("127.0.0.1", dontSendNotification);
    addAndMakeVisible(bind_addr_);

    auth_toggle_.setButtonText("Auth");
    addAndMakeVisible(auth_toggle_);

    stream_raw_.setButtonText("Raw");   stream_raw_.setToggleState(true, dontSendNotification);
    stream_filt_.setButtonText("Filt");
    stream_spk_.setButtonText("Spk");   stream_spk_.setToggleState(true, dontSendNotification);
    stream_ttl_.setButtonText("TTL");   stream_ttl_.setToggleState(true, dontSendNotification);
    addAndMakeVisible(stream_raw_);
    addAndMakeVisible(stream_filt_);
    addAndMakeVisible(stream_spk_);
    addAndMakeVisible(stream_ttl_);

    block_size_.addItem("32", 1);
    block_size_.addItem("64", 2);
    block_size_.addItem("128", 3);
    block_size_.setSelectedId(1, dontSendNotification);
    addAndMakeVisible(block_size_);

    slot_count_.setText("256", dontSendNotification);
    addAndMakeVisible(slot_count_);

    status_.setText("idle", dontSendNotification);
    addAndMakeVisible(status_);

    startTimerHz(2);
    OECDIAG("editor ctor exit");
}

OEconnectEditor::~OEconnectEditor() { stopTimer(); }

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

GenericEditor* createOEconnectEditor(OEconnectJuceProcessor* p) {
    return new OEconnectEditor(p);
}

}  // namespace oec::plugin
