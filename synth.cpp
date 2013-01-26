#include <jack/jack.h>
#include <jack/midiport.h>
#include <rtosc/thread-link.h>
#include <rtosc/miditable.h>
#include <lo/lo.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <string>
#include <set>
#include "ports.h"

using std::function;
using namespace rtosc;

ThreadLink bToU(1024,1024);
ThreadLink uToB(1024,1024);
void display(const char *str){bToU.write("/display", "s", str);}

#include "snarf.cpp"

struct Oscil
{
    float volume;
    float cents;
    int shape;

    //private data
    float phase;

    static Ports ports;
};

struct Synth
{
    float freq;
    bool  enable;
    Oscil oscil[16];

    static Ports ports;
} synth;

jack_port_t   *port, *iport;
jack_client_t *client;

void echo(const char *m, void*){
    bToU.raw_write(uToB.peak());
}

Ports Oscil::ports = {
    PARAMF(Oscil, cents,   cents,  lin, -1e5, 1e5, "Detune in cents"),
    PARAMF(Oscil, volume,  volume, lin, 0.0,  1.0, "Volume on linear scale"),
    PARAMI(Oscil, shape,   shape,  2, "Shape of Oscillator: {sine, saw, square}")
};

void help(msg_t,void*)
{
    display("Welcome to the OSC prompt, where simple OSC messages control "
            "parameters in a less than simple manner.\n"
            "\n"
            "This application is a simple additive synthesis engine. "
            "The synthesizer ports are:\n"
            "/synth/enable, /synth/oscil#/cents, /synth/oscil#/volume, /synth/oscil#/shape, "
            "/synth/freq\n"
            "For some audio enable the output, make one volume non-zero, and set a frequency\n\n"
            "/synth/enable T\n"
            "/synth/oscil0/volume 0.2\n"
            "/synth/freq 440.0\n\n"
            "Good Luck...");
}

Ports Synth::ports = {
    PARAMF(Synth, freq,   freq,   lin, 0, 20e3, "Base frequency of note"),
    PARAMT(Synth, enable, enable, "Enable or disable audio output"),
    RECURS(Synth, Oscil,  oscil,  oscil, 16, "Oscillator bank element")
};

void apropos(msg_t m, void*);
void describe(msg_t m, void*);
void midi_register(msg_t m, void*);
void path_search(msg_t m, void*);
bool do_exit = false;

Ports ports = {
    //Meta port
    {"echo",             ":'hidden':Echo all parameters back",     0, echo},
    {"help:",            "::Display help to user",                 0, help},
    {"apropos:s",        "::Find the best match",                  0, apropos},
    {"describe:s",       "::Print out a description of a port",    0, describe},
    {"path-search:ss",   "::Return a list of possible paths",      0, path_search},
    {"midi-register:is", "::Register a midi port <ctl id, path>",  0, midi_register},
    {"quit:",            "::Quit the program", 0,
        [](msg_t m, void*){do_exit=true; bToU.write("/disconnect","");}},
    {"snarf:",           "::Save an image for parameters", 0,
        [](msg_t,void*){snarf();}},
    {"barf:",            "::Apply an image for parameters", 0,
        [](msg_t,void*){barf();}},



    //Normal ports
    {"synth/", "::Main ports for synthesis", &Synth::ports,
        [](msg_t m, void*){Synth::ports.dispatch(snip(m), &synth); }},
};

Ports *backend_ports = &ports;

void path_search(msg_t m, void *)
{
    //assumed upper bound of 32 ports (may need to be resized)
    char         types[65];
    rtosc_arg_t  args[64];
    size_t       pos    = 0;
    const Ports *ports  = NULL;
    const Port  *port   = NULL;
    const char  *str    = rtosc_argument(m,0).s;
    const char  *needle = rtosc_argument(m,1).s;

    //zero out data
    memset(types, 0, sizeof(types));
    memset(args,  0, sizeof(args));

    if(!*str) {
        ports = &::ports;
    } else {
        const Port *port = ::ports.apropos(rtosc_argument(m,0).s);
        if(port)
            ports = port->ports;
    }

    if(ports) {
        //RTness not confirmed here
        for(const Port &p:*ports) {
            if(strstr(p.name, needle)!=p.name)
                continue;
            types[pos]    = types[pos+1] = 's';
            args[pos++].s = p.name;
            args[pos++].s = p.metadata;
        }
    }

    bToU.writeArray("/paths", types, args);
}

void apropos(msg_t m, void*)
{
    const char *s = rtosc_argument(m,0).s;
    if(*s=='/') ++s;
    const Port *p = ports.apropos(s);
    if(p)
        display(p->name);
    else
        display("unknown path...");
}

void describe(msg_t m, void*)
{
    const char *s = rtosc_argument(m,0).s;
    const char *ss = rtosc_argument(m,0).s;
    if(*s=='/') ++s;
    const Port *p = ports.apropos(s);
    if(p)
        display(p->metadata);
    else
        bToU.write("/display", "sss", "could not find path...<", ss, ">");
}

#define PI 3.14159f

inline float warp(unsigned shape, float phase)
{
    if(shape == 0)
        return phase;
    else if(shape == 1)
        return sinf(2*PI*phase);
    else if(shape == 2)
        return phase<0.5?-1:1;

    return 0.0f;
}

MidiTable<64,64> midi(*backend_ports);
static char current_note = 0;

void midi_register(msg_t m, void*)
{
    midi.addElm(0,
            rtosc_argument(m,0).i,
            rtosc_argument(m,1).s);
}

int process(unsigned nframes, void*)
{
    //Handle user events
    while(uToB.hasNext())
        ports.dispatch(uToB.read()+1, NULL);

    //Handle midi events
    void *midi_buf = jack_port_get_buffer(iport, nframes);
    jack_midi_event_t ev;
    jack_nframes_t event_idx = 0;
    while(jack_midi_event_get(&ev, midi_buf, event_idx++) == 0) {
        switch(ev.buffer[0]&0xf0) {
            case 0x90: //Note On
                synth.freq = 440.0f * powf(2.0f, (ev.buffer[1]-69.0f)/12.0f);
                current_note = ev.buffer[1];
                synth.enable = 1;
                break;
            case 0x80: //Note Off
                if(current_note == ev.buffer[1])
                    current_note = synth.enable = 0;
                break;
            case 0xB0: //Controller
                midi.process(ev.buffer[0]&0x0f, ev.buffer[1], ev.buffer[2]);
                break;
        }
    }

    //Setup jack parameters
    const float Fs = jack_get_sample_rate(client);
    float *output  = (float*) jack_port_get_buffer(port, nframes);

    //Zero out buffer
    for(unsigned i=0; i<nframes; ++i)
        output[i] = 0.0f;

    //Don't synthesize anything if the output is disabled
    if(!synth.enable)
        return 0;

    //Gather all oscilators
    for(int i=0; i<16; ++i) {
        float &phase  = synth.oscil[i].phase;
        float  volume = synth.oscil[i].volume;
        int    shape  = synth.oscil[i].shape;
        float  nfreq  = synth.freq*powf(2.0f,synth.oscil[i].cents/1200.0f);
        const float incf = nfreq/Fs;//oscil[0].freq/Fs;

        for(unsigned j=0; j<nframes; ++j) {
            output[j] += volume*warp(shape, phase);

            phase     += incf;
            if(phase>1.0f)
                phase -= 1.0f;
        }
    }
    return 0;
}

void cleanup_audio(void)
{
    jack_deactivate(client);
    jack_client_close(client);
}

void init_audio(void)
{
    //setup miditable
    midi.event_cb = [](const char *m){ports.dispatch(m+1, NULL);};

    //Setup ports
    client = jack_client_open("oscprompt-demo", JackNullOption, NULL, NULL);
    jack_set_process_callback(client, process, NULL);

    port = jack_port_register(client, "output",
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);

    iport = jack_port_register(client, "input",
            JACK_DEFAULT_MIDI_TYPE, JackPortIsInput | JackPortIsTerminal, 0);

    //Run audio
    jack_activate(client);
    atexit(cleanup_audio);
}


void error(int i, const char *m, const char *loc)
{
    fprintf(stderr, "%d-%s@%s\n",i,m,loc);
}


std::string last_url;//last url to be sent down the pipe
std::string curr_url;//last url to pop out of the pipe

std::set<std::string> logger;
int handler_function(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
    lo_address addr = lo_message_get_source(msg);
    if(addr) {
        const char *tmp = lo_address_get_url(addr);
        if(tmp != last_url) {
            uToB.write("/echo", "ss", "OSC_URL", tmp);
            last_url = tmp;
        }

    }

    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    size_t size = 2048;
    lo_message_serialise(msg, path, buffer, &size);
    if(!strcmp(buffer, "/logging-start")) {
        if(addr)
            logger.insert(lo_address_get_url(addr));
    } else if(!strcmp(buffer, "/logging-stop")) {
        if(addr)
            logger.erase(lo_address_get_url(addr));
    } else
        uToB.raw_write(buffer);
}

int main()
{
    init_audio();

    //setup liblo link
    lo_server server = lo_server_new_with_proto(NULL, LO_UDP, error);
    lo_method handle = lo_server_add_method(server, NULL, NULL, handler_function, NULL);

    printf("Synth running on port %d\n", lo_server_get_port(server));

    while(!do_exit) {
        lo_server_recv_noblock(server, 100);
        while(bToU.hasNext()) {
            const char *rtmsg = bToU.read();
            if(!strcmp(rtmsg, "/echo") && !strcmp(rtosc_argument(rtmsg,0).s, "OSC_URL"))
                curr_url = rtosc_argument(rtmsg,1).s;
            else {
                lo_message msg  = lo_message_deserialise((void*)rtmsg, rtosc_message_length(rtmsg, bToU.buffer_size()), NULL);
                for(std::string str : logger) {
                    lo_address addr = lo_address_new_from_url(str.c_str());
                    lo_send_message(addr, rtmsg, msg);
                }
                if(logger.find(curr_url) == logger.end()) {
                    lo_address addr = lo_address_new_from_url(curr_url.c_str());
                    lo_send_message(addr, rtmsg, msg);
                }
            }
        }
    }
    return 0;
}
