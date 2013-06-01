#include <rtosc/rtosc.h>
#include <rtosc/ports.h>

const char *snip(const char *m)
{
    while(*m && *m!='/')++m;
    return *m?m+1:m;
}

template<class T>
T lim(T min, T max, T val)
{
    return val<max?(val>min?val:min):max;
}

//floating point parameter - with lookup code
#define PARAMF(type, var, name, scale, _min, _max, desc) \
{#name"::N:f", #scale "," # _min "," #_max ":'parameter':" desc, 0, \
    [](const char *m, RtData d) { \
        if(rtosc_narguments(m)==0) {\
            bToU.write("/display", "sf", d.loc, ((type*)d.obj)->var); \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='f') {\
            ((type*)d.obj)->var = lim<float>(_min,_max,rtosc_argument(m,0).f); \
            bToU.write(d.loc, "f", ((type*)d.obj)->var);}}}
        //} else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N')  \
        //    snarf_addf(((type*)d.obj)->var);}}

//integer parameter
#define PARAMI(type, var, name, _max, desc) \
{#name"::N:i", "_,0," #_max ":'parameter':" desc, 0, \
    [](const char *m, RtData d) { \
        if(rtosc_narguments(m)==0) { \
            bToU.write("/display", "si", d.loc,((type*)d.obj)->var);    \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='i') {   \
            ((type*)d.obj)->var = lim<unsigned>(0,_max,rtosc_argument(m,0).i); \
            bToU.write(d.loc, "i", ((type*)d.obj)->var);}}}
        //} else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N')   \
        //    snarf_addi(((type*)d.obj)->var);}}

//boolean parameter
#define PARAMT(type, var, name, desc) \
{#name":T:F", ":'parameter':" desc, 0, \
    [](const char *m, RtData d) { \
        ((type*)d.obj)->var = rtosc_argument(m, 0).T;}}

//optional subclass
#define OPTION(type, cast, name, var) \
{#name "/", ":'option':", &cast ::ports, \
    [](const char *m, RtData d) { \
        cast *c = dynamic_cast<cast*>(((type*)d.obj)->var); \
        if(c) cast::ports.dispatch(snip(m), c); }}

//Dummy - a placeholder port
#define DUMMY(name) \
{#name, ":'dummy':", 0, [](const char *, RtData){}}

//Recur - perform a simple recursion
#define RECUR(type, cast, name, var, desc) \
{#name"/", ":'recursion':" desc, &cast::ports, [](const char *m, RtData &d){\
    d.obj = &(((type*)d.obj)->var) cast::ports.dispatch(snip(m), d);}}

//Recurs - perform a ranged recursion
#define RECURS(type, cast, name, var, length, desc) \
{#name "#" #length "/", ":'recursion':" desc, &cast::ports, [](const char *m, RtData d){ \
    const char *mm = m; \
    while(!isdigit(*mm))++mm; \
        d.obj = &(((type*)d.obj)->var)[atoi(mm)]; cast::ports.dispatch(snip(m), d);}}
