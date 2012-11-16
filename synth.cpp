#include <jack/jack.h>
#include <cstdio>
#include <cmath>
#include <rtosc.h>
#include <thread-link.h>

ThreadLink<1024,1024> bToU;
ThreadLink<1024,1024> uToB;

struct Oscil
{
    float freq;
//    float amp;
//    int shape;

    float phase;
    float last_value;
};

struct Connections
{
    int enable, src, dest;
    float bias, scale;
};

//OSCIL 0 is the output
//connections are used to set parameters based upon other oscilators

Oscil oscil[16];
Connections conn[256];
jack_port_t   *port;
jack_client_t *client;

void echo(msg_t m, void*){
    bToU.write("/display", "s", rtosc_argument(m,0).s);
}
void freq(msg_t m, void*){
    conn[0].bias = rtosc_argument(m,0).f;
}
void bias_cb(msg_t m, void*) {
    unsigned sel = rtosc_argument(m, 0).i;
    if(sel<256) {
        conn[sel].bias = rtosc_argument(m, 1).f;

        char buffer[255];
        snprintf(buffer, 255, "Bias %d is now %f...\n", sel, conn[sel].bias);
        bToU.write("/display", "s", buffer);
    }
}

void scale_cb(msg_t m, void*) {
    unsigned sel = rtosc_argument(m, 0).i;
    if(sel<256) {
        conn[sel].scale = rtosc_argument(m, 1).f;

        char buffer[255];
        snprintf(buffer, 255, "Scale %d is now %f...\n", sel, conn[sel].scale);
        bToU.write("/display", "s", buffer);
    }
}

void enable_cb(msg_t m, void*) {
    unsigned sel = rtosc_argument(m, 0).i;
    if(sel<256) {
        conn[sel].enable = rtosc_argument(m, 1).T;
        
        char buffer[255];
        snprintf(buffer, 255, "Enable %d is now %d...\n", sel, conn[sel].enable);
        bToU.write("/display", "s", buffer);
    }
}

void src_cb(msg_t m, void*) {
    unsigned sel = rtosc_argument(m, 0).i;
    if(sel<256) {
        conn[sel].src = rtosc_argument(m, 1).i;
        
        char buffer[255];
        snprintf(buffer, 255, "Source %d is now %d...\n", sel, conn[sel].src);
        bToU.write("/display", "s", buffer);
    }
}

void dest_cb(msg_t m, void*) {
    unsigned sel = rtosc_argument(m, 0).i;
    if(sel<256) {
        conn[sel].dest = rtosc_argument(m, 1).i;
        
        char buffer[255];
        snprintf(buffer, 255, "Destination %d is now %d...\n", sel, conn[sel].dest);
        bToU.write("/display", "s", buffer);
    }
}

void help(msg_t,void*)
{
    bToU.write("/display", "s", "Good Luck...");
    bToU.write("/display", "s", "");
    bToU.write("/display", "s", "then this controls the output frequency");
    bToU.write("/display", "s", "As the default destination of the zeroth connection is the master oscilator 0,");
    bToU.write("/display", "s", "argument types, so it sets the bias of the zeroth connection.");
    bToU.write("/display", "s", "The command /bias 0 220.0 matches the /bias:if due to its name and");
    bToU.write("/display", "s", "The ports are /bias:if, /scale:if, /enable:iT:iF, /src:ii, and /dest:ii .");
    bToU.write("/display", "s", "This may sound confusing, as it is in fact a convoluted mess");
    bToU.write("/display", "s", "is a linear combination of the rest of them.");
    bToU.write("/display", "s", "This application controls a bank of sine waves where each one's frequency");
    bToU.write("/display", "s", "a hairy mess of connections");
    bToU.write("/display", "s", "Welcome to the OSC prompt, where simple OSC messages control" );
}

Ports<7,void> ports{{{
    //Testing port
    Port<void>("echo:s", "", echo),
    Port<void>("help:", "", help),

    //Ports for connections
    Port<void>("bias:if", "", bias_cb),
    Port<void>("scale:if", "", scale_cb),
    Port<void>("enable:iT:iF","", enable_cb),
    Port<void>("src:ii","", src_cb),
    Port<void>("dest:ii","",dest_cb)
}}};

#define PI 3.141592653589793238462643383279502884L
int process(unsigned nframes, void*)
{
    //Handle events
    while(uToB.hasNext())
        ports.dispatch(uToB.read()+1, NULL);

    //Setup jack parameters
    const float Fs = jack_get_sample_rate(client);
    float *output  = (float*) jack_port_get_buffer(port, nframes);

    //Update all parameters
    for(auto &o:oscil) //Clear old params
        o.freq = 0.0f;
    for(auto &c:conn) { //Provide new params
        if(c.enable) {
            const float val = c.scale*oscil[c.src].last_value+c.bias;
            oscil[c.dest].freq += val;
        }
    }

    //Get output from oscil 0
    for(unsigned i=0; i<nframes; ++i) {
        const float incf = oscil[0].freq/Fs;
        float &phase = oscil[0].phase;

        output[i]  = sin(2*PI*phase);
        phase     += incf;

        if(phase>1.0f)
            phase -= 1.0f;
    }
    oscil[0].last_value = sin(2*3.14*oscil[0].phase);

    //Run all remaining oscils
    for(int i=1; i<16; ++i) {
        const float incf = oscil[i].freq*nframes/Fs;
        float &phase = oscil[i].phase;

        phase += incf;

        oscil[i].last_value = sin(2*PI*phase);

        if(phase>1.0f)
            phase -= 1.0f;
    }
    return 0;
}


void init_audio(void)
{
    //Setup ports
    client = jack_client_open("oscprompt-demo", JackNullOption, NULL, NULL);
    jack_set_process_callback(client, process, NULL);

    port = jack_port_register(client, "output",
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);

    //Setup default connections
    conn[0].enable = 1;
    conn[0].bias = 440.0f;

    //Run audio
    jack_activate(client);
}
