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
// it only sets a flag and logs; the client is closed later, in the destructor.
static void jack_shutdown(void *arg) {
    JackClient *client = (JackClient *)arg;
    client->OnShutdown();
}

JackClient::JackClient() : client_(0), shutdown_(false) {};

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
    shutdown_ = true;
    Trace::Debug("[J]: JACK server shut down, audio stopped");
};

bool JackClient::IsShutdown() { return shutdown_; };

bool JackClient::Available() {
	return client_!=0 ;
} ;


jack_client_t *JackClient::GetClient() {
	return client_ ;
} ;

void JackClient::ProcessCallback(jack_nframes_t nframes) {
     IteratorPtr<JackProcessor>it(GetIterator()) ;
     for (it->Begin();!it->IsDone();it->Next()) {
         JackProcessor &current=it->CurrentItem() ;
         current.ProcessCallback(nframes) ;
     }
} ;
#endif
