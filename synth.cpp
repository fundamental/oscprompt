#include <jack/jack.h>
#include <jack/midiport.h>
#include <rtosc/rtosc.h>
#include <rtosc/thread-link.h>
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <functional>

using std::function;

ThreadLink<1024,1024> bToU;
ThreadLink<1024,1024> uToB;
void display(const char *str){bToU.write("/display", "s", str);}

#define MAX_SNARF 10000
char snarf_buffer[MAX_SNARF];
char snarf_path[128];

extern _Ports *backend_ports;

void barf(void)
{
    unsigned elms = rtosc_bundle_elements(snarf_buffer);
    for(unsigned i=0; i<elms; ++i)
        backend_ports->dispatchCast(rtosc_bundle_fetch(snarf_buffer,i)+1, NULL);
}

bool snarf_p(const char *meta)
{
    while(*meta && *meta != ':') ++meta;
    ++meta;
    while(*meta && *meta != ':')
        if(*meta++ == 's')
            return true;

    return false;
}

void scat(char *dest, const char *src)
{
    while(*dest) dest++;
    if(*dest) dest++;
    while(*src && *src!=':') *dest++ = *src++;
    *dest = 0;
}

void snarf_port(const char *port)
{
    char message_buf[1024];
    //Load the right address
    scat(snarf_path, port);

    //Snarf it
    rtosc_message(message_buf, 1024, snarf_path, "N");

    backend_ports->dispatchCast(message_buf+1, NULL);

    //Clear out the port
    char *buf = rindex(snarf_path, '/')+1;
    while(*buf) *buf++ = 0;
}

void snarf_ports(_Ports *ports);

bool special(const char *name)
{
    return index(name,'#');
}

void ensure_path(char *name)
{
    if(rindex(name, '/')[1] != '/')
        strcat(name, "/");
}

void magic(const char *name, _Ports *ports)
{
    char *old_end = rindex(snarf_path, '/')+1;
    char *pos = old_end;
    while(*name != '#') *pos++ = *name++;
    unsigned max = atoi(name+1);
    for(int i=0; i<max; ++i)
    {
        sprintf(pos,"%d",i);
        ensure_path(snarf_path);
        snarf_ports(ports);//Snarf
    }
    while(*old_end) *old_end++=0; //Erase
}

void snarf_ports(_Ports *ports)
{
    const unsigned nports = ports->nports();
    for(unsigned i=0; i<nports; ++i) {
        const _Port &p = ports->port(i);
        if(index(p.name, '/')) {//it is another tree
            if(special(p.name)) {
                magic(p.name, p.ports);
            } else {
                char *old_end = rindex(snarf_path, '/')+1;
                scat(snarf_path, p.name);//Cat

                snarf_ports(p.ports);//Snarf

                while(*old_end) *old_end++=0; //Erase
            }
        } else
            snarf_port(p.name);
    }
}

void snarf_addf(float f)
{
    unsigned len = rtosc_message_length(snarf_buffer);
    unsigned msg_len = rtosc_message(snarf_buffer+len, MAX_SNARF-len, snarf_path, "f", f);
    *(uint32_t*)(snarf_buffer+len-4) = msg_len;
}

void snarf_addi(int i)
{
    unsigned len = rtosc_message_length(snarf_buffer);
    unsigned msg_len = rtosc_message(snarf_buffer+len, MAX_SNARF-len, snarf_path, "i", i);
    *(uint32_t*)(snarf_buffer+len-4) = msg_len;
}

void snarf(void)
{
    memset(snarf_buffer, 0, sizeof(snarf_buffer));
    memset(snarf_path,   0, sizeof(snarf_path));
    strcpy(snarf_buffer, "#bundle");
    snarf_path[0] = '/';
    snarf_ports(backend_ports);
}


struct Oscil
{
    float volume;
    float cents;
    int shape;

    //private data
    float phase;

    void dispatch(msg_t m);
};

struct Synth
{
    float freq;
    bool  enable;
    Oscil oscil[16];
} synth;

jack_port_t   *port, *iport;
jack_client_t *client;

void echo(msg_t m, void*){
    display(rtosc_argument(m,0).s);
}

template<class T>
T lim(T min, T max, T val)
{
    return val<max?(val>min?val:min):max;
}

template<class T>
auto paramf(float min, float max, float T::*p) -> function<void(msg_t,T*)>
{
    return [p,min,max](msg_t m, T*t)
    {
        if(rtosc_narguments(m)==0)
            bToU.write("/display", "sf", uToB.peak(), (t->*p));
        else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='f')
            (t->*p) = lim<float>(min,max,rtosc_argument(m,0).f);
        else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N') {
            snarf_addf((t->*p));
        }
    };
}

template<class T>
auto parami(unsigned max, int T::*p) -> function<void(msg_t,T*)>
{
    return [p,max](msg_t m, T*t)
    {
        if(rtosc_narguments(m)==0)
            bToU.write("/display", "si", uToB.peak(),(t->*p));
        else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='i')
            (t->*p) = lim<unsigned>(0,max,rtosc_argument(m,0).i);
        else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N') {
            //display("over here bon bon...");
            snarf_addi((t->*p));
        }
    };
}

Ports<3,Oscil> oscil_ports{{{
    //Ports for connections
    Port<Oscil>("cents::N:f",  "1,-1e5,1e5::Oscillator detune in cents",
            paramf(-1e5,1e5, &Oscil::cents)),
    Port<Oscil>("volume::N:f", "1,0,1::Overall volume in a linear scale",
            paramf(0.0,1.0, &Oscil::volume)),
    Port<Oscil>("shape::N:i",  "_,0,2::Shape of Oscillator, {sine,saw,square}",
            parami(2, &Oscil::shape))
}}};

void Oscil::dispatch(msg_t m)
{
    oscil_ports.dispatch(m,this);
}

void help(msg_t,void*)
{
    display("Good Luck...");
    display("");
    display("/synth/freq 440.0");
    display("/synth/oscil0/volume 0.2");
    display("/synth/enable T");
    display("For some audio enable the output, make one volume non-zero, and set a frequency");
    display("");
    display("/synth/freq");
    display("/synth/enable, /synth/oscil#/cents, /synth/oscil#/volume, /synth/oscil#/shape,");
    display("The synthesizer ports are:");
    display("This application is a simple additive synthesis engine.");
    display("parameters in a less than simple manner.");
    display("Welcome to the OSC prompt, where simple OSC messages control" );
}

const char *snip(const char *m)
{
    while(*m && *m!='/')++m;
    return *m?m+1:m;
}

template<class T, class TT>
std::function<void(msg_t,T*)> recur_array(TT T::*p)
{
    return [p](msg_t m, T*t){
        msg_t mm = m;
        while(!isdigit(*mm))++mm;
        (t->*p)[atoi(mm)].dispatch(snip(m));
    };
}

template<class T, class TT>
std::function<void(msg_t,T*)> recur(TT T::*p)
{
    return [p](msg_t m, T*t){(t->*p).dispatch(snip(m));};
}

Ports<3,Synth> synth_ports{{{
    //Ports for connections
    Port<Synth>("freq::N:f", "1,0,20e3::Base frequency of all oscilators", paramf(0,20e3,&Synth::freq)),
    Port<Synth>("enable:T:F", "::Enable or disable of output",
            [](msg_t m, Synth*s){s->enable = rtosc_argument(m,0).T;}),
    Port<Synth>("oscil#16/", &oscil_ports, recur_array(&Synth::oscil)),
}}};

void apropos(msg_t m, void*);
void describe(msg_t m, void*);
void midi_register(msg_t m, void*);

Ports<9,void> ports{{{
    //Meta port
    Port<void>("echo:s", "::Echo all parameters back to the user", echo),
    Port<void>("help:", "::Display help to user", help),
    Port<void>("apropos:s", "::Find the best match", apropos),
    Port<void>("describe:s", "::Print out a description of a port", describe),
    Port<void>("midi-register:iss", "::Register a midi port <ctl id, path, function>",
            midi_register),
    Port<void>("quit:", "::Quit the program",
            [](msg_t m, void*){bToU.write("/exit","");}),
    Port<void>("snarf:", "::Save an image for parameters", [](msg_t,void*){snarf();}),
    Port<void>("barf:", "::Apply an image for parameters", [](msg_t,void*){barf();}),


    //Normal ports
    Port<void>("synth/", &synth_ports,
            [](msg_t m, void*){synth_ports.dispatch(snip(m), &synth); }),
}}};

_Ports *backend_ports = &ports;

void apropos(msg_t m, void*)
{
    const char *s = rtosc_argument(m,0).s;
    if(*s=='/') ++s;
    const _Port *p = ports.apropos(s);
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
    const _Port *p = ports.apropos(s);
    if(p) {
        if(index(p->name,'/')) {
            display("describe not supported for directories");
            return;
        }
        display(p->metadata);
    }
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

MidiTable<64,64> midi;
static char current_note = 0;

void midi_register(msg_t m, void*)
{
    midi.addElm(0,
            rtosc_argument(m,0).i,
            rtosc_argument(m,1).s,
            rtosc_argument(m,2).s);
}
float translate(unsigned char val, const char *conversion)
{
    int type = 0;
    if(conversion[0]=='1' && conversion[1]==',')
        type = 1; //linear
    else if(conversion[0]=='1' && conversion[1]=='0' && conversion[2]=='^')
        type = 2; //exponential

    while(*conversion++!=',');
    float min = atof(conversion);
    while(*conversion++!=',');
    float max = atof(conversion);

    //Allow for middle value to be set
    float x = val!=64.0 ? val/127.0 : 0.5;

    if(type == 1)
        return x*(max-min)+min;
    else if(type == 2) {
        const float b = log(min)/log(10);
        const float a = log(max)/log(10)-b;
        return powf(10.0f, a*x+b);
    }

    return 0;
}

void process_control(unsigned char control[3])
{
    const MidiAddr<64> *addr = midi.get(0,control[0]);
    if(addr) {
        char buffer[1024];
        rtosc_message(buffer,1024,addr->path,"f",
                translate(control[1],addr->conversion));
        ports.dispatch(buffer+1, NULL);
    }
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
                process_control(ev.buffer+1);
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
