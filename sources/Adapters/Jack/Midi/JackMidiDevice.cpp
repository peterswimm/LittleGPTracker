#ifndef _NO_JACK_
#include "JackMidiDevice.h"
#include "System/Console/Trace.h"
#include <jack/midiport.h>

// 4096 bytes hold 511 events
#define JACK_MIDI_RING_SIZE 4096

// Bytes in a message with this status. MidiMessage has no SysEx.
static unsigned char messageSize(unsigned char status) {
    if (status < 0xF0) {
        unsigned char type = status & 0xF0;
        return (type == 0xC0 || type == 0xD0) ? 2 : 3;
    }
    if (status == 0xF1 || status == 0xF3) {
        return 2;
    }
    if (status == 0xF2) {
        return 3;
    }
    return 1; // clock, start, stop and the other one-byte messages
}

JackMidiDevice::JackMidiDevice(jack_client_t *client, const char *portName)
    : MidiOutDevice("Jack Midi"), client_(client), port_(0), ring_(0),
      dropped_(0) {
    port_ = jack_port_register(client_, portName, JACK_DEFAULT_MIDI_TYPE,
                               JackPortIsOutput, 0);
    ring_ = jack_ringbuffer_create(JACK_MIDI_RING_SIZE);
    if (!port_ || !ring_) {
        Trace::Error("[J]: cannot create the MIDI out port");
        return;
    }
    jack_ringbuffer_mlock(ring_);
    JackClient::GetInstance()->Insert(this);
}

// The process callback keeps a pointer to the device, so it isn't deleted
// while the client runs (MidiService builds its devices once); the ring is
// left to the process exit.
JackMidiDevice::~JackMidiDevice() {}

bool JackMidiDevice::Init() { return port_ && ring_; }

void JackMidiDevice::Close() {}

bool JackMidiDevice::Start() { return true; }

void JackMidiDevice::Stop() {
    if (dropped_) {
        Trace::Debug("[J]: %lu MIDI messages dropped, the ring was full",
                     dropped_);
    }
}

unsigned long JackMidiDevice::GetDropped() { return dropped_; }

void JackMidiDevice::SendMessage(MidiMessage &msg) {
    if (!ring_) {
        return;
    }
    Event ev;
    ev.time_ = jack_frame_time(client_);
    ev.size_ = messageSize(msg.status_);
    ev.data_[0] = msg.status_;
    ev.data_[1] = msg.data1_;
    ev.data_[2] = msg.data2_;
    // a whole event or nothing, so the reader never sees half of one
    if (jack_ringbuffer_write_space(ring_) < sizeof(ev)) {
        dropped_ = dropped_ + 1;
        return;
    }
    jack_ringbuffer_write(ring_, (const char *)&ev, sizeof(ev));
}

// Realtime: no allocation, no locks. An event sent during the previous
// period is played one period later, at the same offset into it.
void JackMidiDevice::ProcessCallback(jack_nframes_t nframes) {
    if (!port_ || !ring_) {
        return;
    }
    void *buffer = jack_port_get_buffer(port_, nframes);
    if (!buffer) {
        return;
    }
    jack_midi_clear_buffer(buffer);
    jack_nframes_t start = jack_last_frame_time(client_);
    jack_nframes_t last = 0;
    Event ev;
    while (jack_ringbuffer_read_space(ring_) >= sizeof(ev)) {
        jack_ringbuffer_peek(ring_, (char *)&ev, sizeof(ev));
        long offset = long(int(ev.time_ - start)) + long(nframes);
        if (offset >= long(nframes)) {
            break; // sent during this period: it plays in the next one
        }
        // late events go first; offsets never go backwards
        jack_nframes_t at = (offset < long(last)) ? last : offset;
        jack_midi_data_t *dst = jack_midi_event_reserve(buffer, at, ev.size_);
        if (!dst) {
            break; // the port buffer is full: the rest waits a period
        }
        for (int i = 0; i < ev.size_; i++) {
            dst[i] = ev.data_[i];
        }
        jack_ringbuffer_read_advance(ring_, sizeof(ev));
        last = at;
    }
}
#endif
