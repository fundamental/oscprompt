/*
 *  This file is part of oscprompt - a curses OSC frontend
 *  Copyright Mark McCurry 2014
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
{#name"::N:f", ":scale\0="#scale "\0:min\0=" # _min "\0:max\0=" #_max "\0:parameter\0" \
    DOC(desc), 0, \
    [](const char *m, RtData d) { \
        if(rtosc_narguments(m)==0) {\
            bToU.write(d.loc, "f", ((type*)d.obj)->var); \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='f') {\
            ((type*)d.obj)->var = lim<float>(_min,_max,rtosc_argument(m,0).f); \
            bToU.write(d.loc, "f", ((type*)d.obj)->var);}}}

//integer parameter
#define PARAMI(type, var, name, _max, desc) \
{#name"::N:i", ":max\0=" #_max "\0:parameter\0" DOC(desc), 0, \
    [](const char *m, RtData d) { \
        if(rtosc_narguments(m)==0) { \
            bToU.write(d.loc, "i", ((type*)d.obj)->var);    \
        } else if(rtosc_narguments(m)==1 && rtosc_type(m,0)=='i') {   \
            ((type*)d.obj)->var = lim<unsigned>(0,_max,rtosc_argument(m,0).i); \
            bToU.write(d.loc, "i", ((type*)d.obj)->var);}}}

//boolean parameter
#define PARAMT(type, var, name, desc) \
{#name":T:F", ":parameter\0" DOC(desc), 0, \
    [](const char *m, RtData d) { \
        ((type*)d.obj)->var = rtosc_argument(m, 0).T;}}

//optional subclass
#define OPTION(type, cast, name, var) \
{#name "/", ":option\0", &cast ::ports, \
    [](const char *m, RtData d) { \
        cast *c = dynamic_cast<cast*>(((type*)d.obj)->var); \
        if(c) cast::ports.dispatch(snip(m), c); }}

//Dummy - a placeholder port
#define DUMMY(name) \
{#name, ":dummy\0", 0, [](const char *, RtData){}}

//Recur - perform a simple recursion
#define RECUR(type, cast, name, var, desc) \
{#name"/", ":recursion\0" DOC(desc), &cast::ports, [](const char *m, RtData &d){\
    d.obj = &(((type*)d.obj)->var) cast::ports.dispatch(snip(m), d);}}

//Recurs - perform a ranged recursion
#define RECURS(type, cast, name, var, length, desc) \
{#name "#" #length "/", ":recursion\0" DOC(desc), &cast::ports, [](const char *m, RtData d){ \
    const char *mm = m; \
    while(!isdigit(*mm))++mm; \
        d.obj = &(((type*)d.obj)->var)[atoi(mm)]; cast::ports.dispatch(snip(m), d);}}
