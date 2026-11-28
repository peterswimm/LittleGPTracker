#ifndef _NO_JACK_
#include "JackMidiService.h"
#include "Adapters/Jack/Client/JackClient.h"
#include "JackMidiDevice.h"

JackMidiService::JackMidiService() {};

JackMidiService::~JackMidiService() {};

void JackMidiService::buildDriverList() {
    jack_client_t *client = JackClient::GetInstance()->GetClient();
    if (!client) {
        return;
    }
    Insert(new JackMidiDevice(client, "midiout"));
};
#endif
