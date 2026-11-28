
#include "AudioDriver.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

// Orders the writes of a queued buffer before the pointer that hands it to
// the audio callback, which runs on another thread
#if defined(_MSC_VER)
#include <intrin.h>
#define AUDIO_WRITE_BARRIER() _ReadWriteBarrier() // x86/x64: compiler only
#elif defined(__GNUC__) && !defined(ARM9)
#define AUDIO_WRITE_BARRIER() __sync_synchronize()
#elif defined(__GNUC__)
// NDS: a single-core ARM9, so a compiler barrier is enough
#define AUDIO_WRITE_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define AUDIO_WRITE_BARRIER()
#endif

// Some drivers' callbacks run before Init() and Start() (JACK's starts when
// the client is activated), so the pool and its positions are valid from
// construction
AudioDriver::AudioDriver(AudioSettings &settings) {
    settings_ = settings;
    for (int i = 0; i < SOUND_BUFFER_COUNT; i++) {
        pool_[i].buffer_ = 0;
        pool_[i].size_ = 0;
        pool_[i].driverData_ = 0;
    }
    isPlaying_ = false;
    poolQueuePosition_ = 0;
    poolPlayPosition_ = 0;
    bufferPos_ = 0;
    bufferSize_ = 0;
    hasData_ = false;
}

AudioDriver::~AudioDriver() {
    for (int i = 0; i < SOUND_BUFFER_COUNT; i++) {
        SAFE_FREE(pool_[i].buffer_);
    }
}

bool AudioDriver::Init() {

  // Clear all buffers
	
   for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
     pool_[i].buffer_=0 ;
     pool_[i].size_=0 ;
   } ;
   isPlaying_=false;	 

   return InitDriver() ;
}

void AudioDriver::Close() {
	CloseDriver() ;
};

bool AudioDriver::Start() {

    isPlaying_=true ; 

    for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
  	  SAFE_FREE(pool_[i].buffer_) ;
    } ;
	 
    poolQueuePosition_=0 ;
    poolPlayPosition_=0 ;
	hasData_=false ;

    return StartDriver() ;
};

void AudioDriver::Stop() {
     isPlaying_=false ;
	hasData_=false ;
     StopDriver() ;
}

void AudioDriver::AddBuffer(short *buffer,int samplecount) {
  
  int len=samplecount*2*sizeof(short) ;

  if (!isPlaying_) return ;

  if (len>SOUND_BUFFER_MAX) {
      Trace::Error("Alert: buffer size exceeded") ;
  }

  if (pool_[poolQueuePosition_].buffer_ != 0) {
      // The queue is full and the callback may be playing this very slot, so
      // the slot is left alone and the new buffer is dropped
      NInvalid;
      Trace::Error("Audio overrun, please report");
      return;
  }

  // The callback takes a non-null buffer_ as ready and then reads size_ and
  // the samples, so those are written before buffer_ is set
  char *data = (char *)SYS_MALLOC(len);
  if (!data) {
      Trace::Error("Audio buffer allocation failed");
      return;
  }
  SYS_MEMCPY(data, (char *)buffer, len);
  pool_[poolQueuePosition_].size_ = len;
  AUDIO_WRITE_BARRIER();
  pool_[poolQueuePosition_].buffer_ = data;
  poolQueuePosition_ = (poolQueuePosition_ + 1) % SOUND_BUFFER_COUNT;
  hasData_ = true;
}

void AudioDriver::OnNewBufferNeeded() {
  SetChanged() ;
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event) ;
} ;

void AudioDriver::onAudioBufferTick()
{
  SetChanged() ;
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event) ;
}

bool AudioDriver::hasData() {
	return hasData_ ;
}  ;

AudioSettings AudioDriver::GetAudioSettings() {
	return settings_ ;
} ;
