
#ifndef _JACK_CLIENT_H_
#define _JACK_CLIENT_H_

#include "Foundation/T_Singleton.h"
#include "jack/jack.h"

// The most processors (audio driver, MIDI ports) the client can call
#define MAX_JACK_PROCESSORS 8

class JackProcessor {
public:
	virtual ~JackProcessor() {} ;
	virtual void ProcessCallback(jack_nframes_t nframes)=0 ; 
} ;

class JackClient : public T_Singleton<JackClient>, public JackProcessor {
  public:
    JackClient();
    virtual ~JackClient();
    bool Init();
    bool Available();
    jack_client_t *GetClient();
    // Adds a processor called on every process cycle. It may be called
    // while the client is active; processors can't be removed.
    void Insert(JackProcessor *processor);
    void Insert(JackProcessor &processor);
    virtual void ProcessCallback(jack_nframes_t nframes);
    // called from JACK's shutdown handler
    void OnShutdown();
    bool IsShutdown();

  private:
    jack_client_t *client_;
    volatile bool shutdown_;
    // A fixed array, so the process callback (on JACK's realtime thread)
    // walks it without allocating
    JackProcessor *processors_[MAX_JACK_PROCESSORS];
    volatile int processorCount_;
};

#endif
