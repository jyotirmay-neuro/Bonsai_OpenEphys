#include "OEconnectEditor.h"

namespace oec::plugin {

OEconnectEditor::OEconnectEditor(GenericProcessor* p) : GenericEditor(p) {
    transport_box_.addItem("Auto",      1);
    transport_box_.addItem("SharedMem", 2);
    transport_box_.addItem("Zmq",       3);
    transport_box_.setSelectedId(1);
    addAndMakeVisible(transport_box_);

    zmq_port_.setText("5557");
    bind_addr_.setText("127.0.0.1");
    addAndMakeVisible(zmq_port_);
    addAndMakeVisible(bind_addr_);
    addAndMakeVisible(auth_toggle_);

    stream_raw_.setToggleState(true, dontSendNotification);
    stream_filt_.setToggleState(true, dontSendNotification);
    stream_spk_.setToggleState(true, dontSendNotification);
    stream_ttl_.setToggleState(true, dontSendNotification);
    addAndMakeVisible(stream_raw_);
    addAndMakeVisible(stream_filt_);
    addAndMakeVisible(stream_spk_);
    addAndMakeVisible(stream_ttl_);

    block_size_.addItem("16 samp", 1);
    block_size_.addItem("32 samp", 2);
    block_size_.addItem("64 samp", 3);
    block_size_.setSelectedId(2);
    addAndMakeVisible(block_size_);

    slot_count_.setText("256");
    addAndMakeVisible(slot_count_);

    addAndMakeVisible(status_);
    status_.setText("idle", dontSendNotification);

    desiredWidth = 280;
    startTimer(500);  /* refresh status every 500 ms */
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
