#ifndef _JACK_MIDI_DEVICE_H_
#define _JACK_MIDI_DEVICE_H_

#include "Adapters/Jack/Client/JackClient.h"
#include "Services/Midi/MidiOutDevice.h"
#include <jack/jack.h>
#include <jack/ringbuffer.h>

// A JACK MIDI output port. SendMessage runs on one thread, the audio driver
// thread that MidiService flushes from, and only writes a lock-free ring.
// The process callback is the only place the port buffer is written.
class JackMidiDevice : public MidiOutDevice, public JackProcessor {
  public:
    JackMidiDevice(jack_client_t *client, const char *portName);
    virtual ~JackMidiDevice();
    virtual bool Init();
    virtual void Close();
    virtual bool Start();
    virtual void Stop();
    virtual void SendMessage(MidiMessage &msg);
    virtual void ProcessCallback(jack_nframes_t nframes);
    // messages dropped because the ring was full
    unsigned long GetDropped();

  private:
    struct Event {
        jack_nframes_t time_; // jack_frame_time when it was sent
        unsigned char size_;
        unsigned char data_[3];
    };
    jack_client_t *client_;
    jack_port_t *port_;
    jack_ringbuffer_t *ring_;
    volatile unsigned long dropped_;
};
#endif
