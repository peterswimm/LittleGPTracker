
#ifndef _JACK_MIDI_SERVICE_H_
#define _JACK_MIDI_SERVICE_H_

#include "Services/Midi/MidiService.h"

// One JACK MIDI output port, "midiout". There is no JACK MIDI input.
class JackMidiService : public MidiService {
  public:
    JackMidiService();
    ~JackMidiService();

  protected:
    virtual void buildDriverList();
};

#endif
