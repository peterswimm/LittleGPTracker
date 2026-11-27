#ifndef _NO_JACK_
#include "JackAudioDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
//#include "Adapters/Jack/Midi/JackMidiService.h"

JackAudioDriverThread::JackAudioDriverThread(JackAudioDriver *driver) {
	semaphore_=0 ;
	driver_=driver ;
} ;

bool JackAudioDriverThread::Execute() {
	semaphore_=SysSemaphore::Create(0,4) ;
	while (!shouldTerminate()) {
		semaphore_->Wait() ;
		driver_->SetChanged() ;
		driver_->NotifyObservers() ;
	} ;
	delete semaphore_ ;
	return true ;
} ;

void JackAudioDriverThread::Notify() {
	if (semaphore_) {
		semaphore_->Post() ;
	}
} ;

JackAudioDriver::JackAudioDriver(jack_client_t *client,AudioSettings &settings):AudioDriver(settings) {
// Jack driver currently ignores the settings
	client_=client ;
	portL_ = jack_port_register (client, "mixL",JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	portR_ = jack_port_register (client, "mixR",JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	thread_=0 ;
    running_ = false;
    tempBuffer_ = 0;
    current_ = 0;
    available_ = 0;
    startTime_ = 0;
    restart_ = false;
} ;

JackAudioDriver::~JackAudioDriver() {
} ;

int JackAudioDriver::GetSampleRate() {
    // the engine renders at the server's rate (48000 on most JACK2 and
    // PipeWire setups), not a fixed 44100
    return client_ ? int(jack_get_sample_rate(client_)) : 44100;
};

bool JackAudioDriver::InitDriver() {

	Trace::Debug("[J]: Init driver") ;

    if (!portL_ || !portR_) {
        Trace::Error("[J]: cannot register output ports");
        return false;
    }

    // The process callback runs on JACK's realtime thread, so the buffer it
    // plays from is allocated here rather than there
    if (!tempBuffer_) {
        tempBuffer_ = (short *)SYS_MALLOC(SOUND_BUFFER_MAX);
        if (!tempBuffer_) {
            Trace::Error("[J]: cannot allocate the audio buffer");
            return false;
        }
    }
    restart_ = true;

    // Get output port and connect straight to the default system output

    thread_ = new JackAudioDriverThread(this);
    thread_->Start() ;
    running_ = false;

    const char **ports;

    Trace::Debug("[J]: Get ports");
    ports = jack_get_ports(client_, NULL, JACK_DEFAULT_AUDIO_TYPE,
                           JackPortIsPhysical | JackPortIsInput);
    if (ports == NULL || ports[0] == NULL) {
        // headless servers and some PipeWire setups have none: keep running,
        // the ports can be connected with a patchbay
        Trace::Debug("[J]: no physical playback ports, outputs unconnected");
        if (ports) {
            jack_free(ports);
        }
        return true;
    }

    Trace::Debug("[J]: connect L");
    if (jack_connect(client_, jack_port_name(portL_), ports[0])) {
        Trace::Debug("[J]: cannot connect left out");
    }
    // a mono playback device has a single port: send both channels to it
    const char *right = ports[1] ? ports[1] : ports[0];
    Trace::Debug("[J]: connect R");
    if (jack_connect(client_, jack_port_name(portR_), right)) {
        Trace::Debug("[J]: cannot connect right out");
    }
    Trace::Debug("[J]: free ports");
    jack_free(ports);

    return true;
} ;

void JackAudioDriver::CloseDriver() {

	if (thread_) {
		thread_->RequestTermination() ;
		thread_->Notify() ;
		while(!thread_->IsFinished()) {
		} ;
        delete thread_;
    };
    thread_ = 0;
    // JackAudio::Close deactivates the client first, so the process callback
    // no longer reads the buffer
    SAFE_FREE(tempBuffer_);
    current_ = 0;
    available_ = 0;
} ;

bool JackAudioDriver::StartDriver() {
	Trace::Debug("[J]: start driver") ;
    // the process callback may be running: it drops what it was playing at
    // its next call, so only that thread writes current_ and available_
    restart_ = true;
    startTime_ = jack_get_time();
    return true;
} ;

// The buffer is kept: the process callback keeps running while the driver is
// stopped, and it plays silence once AudioDriver::Stop clears hasData_
void JackAudioDriver::StopDriver() {};

void JackAudioDriver::dumpBuffer(jack_default_audio_sample_t *dstL,jack_default_audio_sample_t *dstR,short *src,int frameCount) {

	for (int i=0;i<frameCount;i++) {
		*dstL++=(float(*src++)/32767.0) ;
		*dstR++=(float(*src++)/32767.0) ;
	}
} ;

double JackAudioDriver::GetStreamTime() {
	return (jack_get_time()-startTime_)/1000.0/1000.0 ;
}

void JackAudioDriver::ProcessCallback(jack_nframes_t frames) {

//	Trace::Dump("[J]: process frames %d",frames) ;
	jack_nframes_t nframes=frames ;

 // Get the out buffer

	jack_default_audio_sample_t *outL = 
                (jack_default_audio_sample_t *) 
                jack_port_get_buffer (portL_, nframes);

	jack_default_audio_sample_t *outR = 
                (jack_default_audio_sample_t *) 
                jack_port_get_buffer (portR_, nframes);

//	JackMidiService *jms=(JackMidiService *)MidiService::GetInstance() ;
//	jms->OnNewFrame() ;

    if (!outL || !outR) {
        return;
    }

    // not initialised yet, or already closed
    if (!tempBuffer_) {
        SYS_MEMSET(outL, 0, nframes * sizeof(jack_default_audio_sample_t));
        SYS_MEMSET(outR, 0, nframes * sizeof(jack_default_audio_sample_t));
        return;
    }
    if (restart_) {
        restart_ = false;
        current_ = tempBuffer_;
        available_ = 0;
    }

    if (!hasData()) {
        SYS_MEMSET(outL, 0, nframes * sizeof(jack_default_audio_sample_t));
        SYS_MEMSET(outR, 0, nframes * sizeof(jack_default_audio_sample_t));
        if (thread_)
            thread_->Notify();
    } else {
        while (nframes > 0) {

            int read = 0;

            if (!available_) {
                if (thread_) {
                    thread_->Notify();
                }
                //			jms->SetFrameOffset(frames-nframes) ;
                //			jms->Flush() ;
                if (pool_[poolPlayPosition_].buffer_ != 0) {
                    // tempBuffer_ holds SOUND_BUFFER_MAX bytes
                    int size = pool_[poolPlayPosition_].size_;
                    if (size > SOUND_BUFFER_MAX) {
                        size = SOUND_BUFFER_MAX;
                    }
                    available_ = size / 2 / sizeof(short);
                    SYS_MEMCPY(tempBuffer_, pool_[poolPlayPosition_].buffer_,
                               size);
                    SYS_FREE(pool_[poolPlayPosition_].buffer_);
                    pool_[poolPlayPosition_].buffer_ = 0;
                    poolPlayPosition_ =
                        (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
                    current_ = tempBuffer_;
                } else {
                    // Underrun: play silence. A stereo frame is two shorts,
                    // and a period longer than tempBuffer_ is played in
                    // several chunks by this loop.
                    jack_nframes_t silent = nframes;
                    if (silent > SOUND_BUFFER_MAX / (2 * sizeof(short))) {
                        silent = SOUND_BUFFER_MAX / (2 * sizeof(short));
                    }
                    SYS_MEMSET(tempBuffer_, 0, silent * 2 * sizeof(short));
                    available_ = silent;
                    current_ = tempBuffer_;
                    Trace::Debug("[J]: Underrun detected");
                }
            }
            read = (available_ > nframes) ? nframes : available_;
            dumpBuffer(outL, outR, current_, read);
            available_ -= read;
            outL += read;
            outR += read;
            nframes -= read;
            current_ += read * 2;
        }
    }
} ;
#endif
