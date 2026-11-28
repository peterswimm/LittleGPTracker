#ifndef _NO_JACK_
#include "JackClient.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"

static void jack_error (const char *desc) {
	Trace::Debug("JACK error: %s\n", desc);
}

int jack_callback(jack_nframes_t nframes, void *arg) {

	JackClient *client=(JackClient *)arg ;
	client->ProcessCallback(nframes) ;
	return 0 ;
}

// Called on a JACK thread when the server goes away. It must not block, so
// it only sets a flag; the client is closed later, in the destructor.
static void jack_shutdown(void *arg) {
    JackClient *client = (JackClient *)arg;
    client->OnShutdown();
}

JackClient::JackClient() : client_(0), shutdown_(false), processorCount_(0) {}

JackClient::~JackClient() {
    Trace::Debug("[J]: close driver");
    if (client_) {
        jack_client_close(client_);
    }
};

bool JackClient::Init() {

    // Initialise callbacks

    jack_set_error_function(jack_error);

    // A fixed name lets JACK patchbays restore connections; the server adds
    // a suffix if another instance already uses it. JackNoStartServer: if no
    // server is running, fall back to the default audio driver instead of
    // starting one.
    jack_status_t status;
    client_ = jack_client_open("littlegptracker", JackNoStartServer, &status);
    if (!client_) {
        Trace::Debug("[J]: jack_client_open failed, status 0x%x",
                     (unsigned)status);
        return false;
    }
    jack_on_shutdown(client_, jack_shutdown, this);
    jack_set_process_callback(client_, jack_callback, this);
    return true;
};

void JackClient::OnShutdown() {
    // JACK runs this like a signal handler, so it only sets the flag; the
    // UI thread reports it (see Audio::GetFailure)
    shutdown_ = true;
};

bool JackClient::IsShutdown() { return shutdown_; };

bool JackClient::Available() {
	return client_!=0 ;
} ;


jack_client_t *JackClient::GetClient() {
	return client_ ;
} ;

void JackClient::Insert(JackProcessor *processor) {
    if (processorCount_ == MAX_JACK_PROCESSORS) {
        Trace::Error("[J]: too many JACK processors");
        return;
    }
    // fill the slot before publishing it to the process callback
    processors_[processorCount_] = processor;
    __sync_synchronize();
    processorCount_ = processorCount_ + 1;
}

void JackClient::Insert(JackProcessor &processor) { Insert(&processor); }

void JackClient::ProcessCallback(jack_nframes_t nframes) {
    int count = processorCount_;
    __sync_synchronize();
    for (int i = 0; i < count; i++) {
        processors_[i]->ProcessCallback(nframes);
    }
}
#endif
