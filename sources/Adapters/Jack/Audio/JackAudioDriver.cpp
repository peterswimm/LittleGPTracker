#ifndef _NO_JACK_
#include "JackAudioDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <unistd.h>
//#include "Adapters/Jack/Midi/JackMidiService.h"

JackAudioDriverThread::JackAudioDriverThread(JackAudioDriver *driver) {
	semaphore_=0 ;
	driver_=driver ;
} ;

bool JackAudioDriverThread::Execute() {
	semaphore_=SysSemaphore::Create(0,4) ;
    while (!shouldTerminate()) {
        semaphore_->Wait();
        driver_->OnBufferRequest();
    };
    delete semaphore_;
    return true;
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
    ready_ = false;
    playing_ = 0;
    current_ = 0;
    available_ = 0;
    startTime_ = 0;
    restart_ = false;
    stopRequest_ = false;
    stopAck_ = false;
} ;

JackAudioDriver::~JackAudioDriver() {
} ;

// Runs on the driver thread each time the process callback wants audio. It
// sends the events the other drivers send: the mixer renders on
// ADET_BUFFERNEEDED and MIDI is flushed on ADET_DRIVERTICK. (A bare
// NotifyObservers() passed them no event, and they dereferenced it.) Both
// allocate, so neither is sent from the realtime callback.
void JackAudioDriver::OnBufferRequest() {
    OnNewBufferNeeded();
    onAudioBufferTick();
}

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

    restart_ = true;
    ready_ = true;

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
    ready_ = false;
    SAFE_FREE(playing_);
    current_ = 0;
    available_ = 0;
} ;

bool JackAudioDriver::StartDriver() {
	Trace::Debug("[J]: start driver") ;
    // the process callback may be running: it drops what it was playing at
    // its next call, so only that thread writes current_ and available_
    restart_ = true;
    __sync_synchronize();
    stopRequest_ = false;
    startTime_ = jack_get_time();
    return true;
} ;

// AudioDriver::Start frees the queued buffers right after this returns, so
// wait until the process callback has stopped using them. The callback
// acknowledges at its next call, so the call that was using the pool has
// returned. The wait is bounded: no call comes when the client is inactive
// or the server has gone.
void JackAudioDriver::StopDriver() {
    stopAck_ = false;
    __sync_synchronize();
    stopRequest_ = true;
    for (int i = 0; i < 200 && !stopAck_; i++) {
        usleep(1000);
    }
    if (!stopAck_) {
        Trace::Debug("[J]: stop not acknowledged");
    }
}

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
    if (!ready_) {
        SYS_MEMSET(outL, 0, nframes * sizeof(jack_default_audio_sample_t));
        SYS_MEMSET(outR, 0, nframes * sizeof(jack_default_audio_sample_t));
        return;
    }
    if (stopRequest_) {
        // stopped: play silence and leave the pool to AudioDriver::Start
        SAFE_FREE(playing_);
        current_ = 0;
        available_ = 0;
        SYS_MEMSET(outL, 0, nframes * sizeof(jack_default_audio_sample_t));
        SYS_MEMSET(outR, 0, nframes * sizeof(jack_default_audio_sample_t));
        __sync_synchronize();
        stopAck_ = true;
        return;
    }
    if (restart_) {
        // AudioDriver::Start emptied the pool; what was being played is
        // dropped here, on the only thread that uses it
        restart_ = false;
        SAFE_FREE(playing_);
        current_ = 0;
        available_ = 0;
    }

    if (!hasData()) {
        SYS_MEMSET(outL, 0, nframes * sizeof(jack_default_audio_sample_t));
        SYS_MEMSET(outR, 0, nframes * sizeof(jack_default_audio_sample_t));
        if (thread_)
            thread_->Notify();
        return;
    }
    while (nframes > 0) {
        if (!available_) {
            if (thread_) {
                thread_->Notify();
            }
            //			jms->SetFrameOffset(frames-nframes) ;
            //			jms->Flush() ;
            SAFE_FREE(playing_);
            if (pool_[poolPlayPosition_].buffer_ == 0) {
                // Underrun: the rest of this period is silence
                SYS_MEMSET(outL, 0,
                           nframes * sizeof(jack_default_audio_sample_t));
                SYS_MEMSET(outR, 0,
                           nframes * sizeof(jack_default_audio_sample_t));
                Trace::Debug("[J]: Underrun detected");
                return;
            }
            // Play the queued buffer where it is, whatever its size (a slice
            // can be bigger than SOUND_BUFFER_MAX at high rates and slow
            // tempos), and free it once it has been played
            playing_ = (short *)pool_[poolPlayPosition_].buffer_;
            available_ = pool_[poolPlayPosition_].size_ / 2 / sizeof(short);
            pool_[poolPlayPosition_].buffer_ = 0;
            poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
            current_ = playing_;
            continue;
        }
        jack_nframes_t read = (available_ > nframes) ? nframes : available_;
        dumpBuffer(outL, outR, current_, read);
        available_ -= read;
        outL += read;
        outR += read;
        nframes -= read;
        current_ += read * 2;
    }
} ;
#endif
