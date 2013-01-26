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
    [](const char *m, void *v) { \
        if(rtosc_narguments(m)==0) {\
            bToU.write("/display", "sf", uToB.peak(), ((type*)v)->var); \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='f') {\
            ((type*)v)->var = lim<float>(_min,_max,rtosc_argument(m,0).f); \
            bToU.write(uToB.peak(), "f", ((type*)v)->var); \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N')  \
            snarf_addf(((type*)v)->var);}}

//integer parameter
#define PARAMI(type, var, name, _max, desc) \
{#name"::N:i", "_,0," #_max ":'parameter':" desc, 0, \
    [](const char *m, void *v) { \
        if(rtosc_narguments(m)==0) { \
            bToU.write("/display", "si", uToB.peak(),((type*)v)->var);    \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='i') {   \
            ((type*)v)->var = lim<unsigned>(0,_max,rtosc_argument(m,0).i); \
            bToU.write(uToB.peak(), "i", ((type*)v)->var); \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='N')   \
            snarf_addi(((type*)v)->var);}}

//boolean parameter
#define PARAMT(type, var, name, desc) \
{#name":T:F", ":'parameter':" desc, 0, \
    [](const char *m, void *v) { \
        ((type*)v)->var = rtosc_argument(m, 0).T;}}

//optional subclass
#define OPTION(type, cast, name, var) \
{#name "/", ":'option':", &cast ::ports, \
    [](const char *m, void *v) { \
        cast *c = dynamic_cast<cast*>(((type*)v)->var); \
        if(c) cast::ports.dispatch(snip(m), c); }}

//Dummy - a placeholder port
#define DUMMY(name) \
{#name, ":'dummy':", 0, [](const char *, void *){}}

//Recur - perform a simple recursion
#define RECUR(type, cast, name, var, desc) \
{#name"/", ":'recursion':" desc, &cast::ports, [](const char *m, void *v){\
    cast::ports.dispatch(snip(m), &(((type*)v)->var));}}

//Recurs - perform a ranged recursion
#define RECURS(type, cast, name, var, length, desc) \
{#name "#" #length "/", ":'recursion':" desc, &cast::ports, [](const char *m, void *v){ \
    const char *mm = m; \
    while(!isdigit(*mm))++mm; \
        cast::ports.dispatch(snip(m), &(((type*)v)->var)[atoi(mm)]);}}
