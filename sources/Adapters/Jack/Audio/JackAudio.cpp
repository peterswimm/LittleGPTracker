#ifndef _NO_JACK_
#include "JackAudio.h"
#include "JackAudioDriver.h"
#include "Services/Audio/AudioOutDriver.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "Adapters/Jack/Client/JackClient.h"

// Only logged. The engine reads the rate through GetSampleRate as it plays,
// but values it has already worked out, such as the tick length, are not
// redone, so changing the server's rate mid-session needs a restart
int jack_srate(jack_nframes_t nframes, void *arg) {
    Trace::Debug("[J]: sample rate is now %u/sec", (unsigned)nframes);
    return 0;
}

JackAudio::JackAudio(AudioSettings &hints) : Audio(hints), drv_(0) {}

JackAudio::~JackAudio() {
}



void JackAudio::Init() {

	JackClient *jc=JackClient::GetInstance() ;
	jack_client_t *client=jc->GetClient() ; NAssert(client) ;
	if (client==0) return  ;

	jack_set_sample_rate_callback (client, jack_srate, 0);

//	Create the driver

     JackAudioDriver *drv=new JackAudioDriver(client,settings_) ;
     AudioOut *out=new AudioOutDriver(*drv) ;
     Insert(out) ;
	jc->Insert(drv) ;
    drv_ = drv;

    // Return, the rest will be done later

    if (jack_activate(client)) {
        Trace::Debug("Failed to activate client ?");
    }
} ;

void JackAudio::Close() {
    // stop the process callback before the driver frees its buffer
    jack_client_t *client = JackClient::GetInstance()->GetClient();
    if (client) {
        jack_deactivate(client);
    }
     IteratorPtr<AudioOut>it(GetIterator()) ;
     for (it->Begin();!it->IsDone();it->Next()) {
         AudioOut &current=it->CurrentItem() ;
         current.Close() ;
     }
} ;

const char *JackAudio::GetFailure() {
    // no runtime fallback to another driver: the app has to be restarted
    return JackClient::GetInstance()->IsShutdown()
               ? "JACK server stopped, restart lgpt"
               : 0;
}

int JackAudio::GetSampleRate() {
    // the engine asks the Audio service, so pass on the server's rate
    return drv_ ? drv_->GetSampleRate() : Audio::GetSampleRate();
};
#endif
