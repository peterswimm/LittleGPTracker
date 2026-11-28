#ifndef _JACK_AUDIO_H_
#define _JACK_AUDIO_H_

#include "Services/Audio/Audio.h"
#include "jack/jack.h"

class JackAudioDriver;

class JackAudio: public Audio {
public:
	JackAudio(AudioSettings &hints) ;
	~JackAudio() ;
	virtual void Init() ;
	virtual void Close() ;
    virtual int GetSampleRate();
    virtual const char *GetFailure();

  private:
    JackAudioDriver *drv_;
};
#endif
