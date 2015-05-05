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
#include <rtosc/thread-link.h>
#include <lo/lo.h>
#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <cstdlib>
#include <cctype>
#include <string>
#include <sstream>
#include "render.h"
using namespace rtosc;
using std::string;

//History
char history_buffer[32][1024];
int  history_pos;
const int history_size = 32;

//Tab completion recommendation
string tab_recommendation;

//Error detected in user promppt
int error = 0;

//Liblo destination
lo_address lo_addr;


/**
 * Parses simple messages from strings into something that librtosc can accept
 *
 * This function assumes that message_buffer contains a valid string and that
 * message_arguments is populated as a side effect of the pretty printer
 */
void send_message(void)
{
    const char *str = message_buffer;
    char path[1024];

    //Load message path
    {
        char *p = path;
        while(*str && *str != ' ')
            *p++ = *str++;
        *p = 0;
    }

    //Load arguments
    rtosc_arg_t *args = new rtosc_arg_t[message_narguments];

    //String buffer
    char buf[1024];
    char *b = buf;

    for(int i=0; i<message_narguments; ++i) {
        switch(message_arguments[i]) {
            case 'i':
            case 'f':
                while(!isdigit(*str) && *str!='-' && *str!='.') ++str;
                if(message_arguments[i] == 'i')
                    args[i].i = atoi(str);
                else
                    args[i].f = atof(str);
                while(isdigit(*str) || *str=='-' || *str=='.') ++str;
                break;
            case 'c':
                while(*str++ != 'c');
                args[i].i = atoi(str);
                while(*str && *str != ' ') ++str;
                break;
            case 's':
                while(*str!='"') ++str;
                args[i].s = b;
                ++str;
                while(*str!='"') *b++ = *str++;
                str++;
                *b++ = 0;
                break;
            case 'T':
            case 'F':
                while(*str != 'T' && *str != 'F') ++str;
                ++str;
        }
    }
    char buffer[2048];
    size_t len = rtosc_amessage(buffer, 2048, path, message_arguments, args);
    lo_message msg = lo_message_deserialise(buffer, len, NULL);
    if(lo_addr)
        lo_send_message(lo_addr, buffer, msg);


    delete [] args;
}

int do_exit = 0;

void die_nicely(msg_t, void*)
{
    do_exit = 1;
}

void update_paths(msg_t m, void*)
{
    unsigned fields = rtosc_narguments(m)/2;

    werase(status);

    if(fields)
        tab_recommendation = rtosc_argument(m,0).s;
    else {
        tab_recommendation = "";
        wprintw(status, "No matching ports...\n");
    }

    //Gather information for the long format of the status field
    char *tmp = rindex(message_buffer, '/');
    if(fields == 1 && tmp) {
        status_name = rtosc_argument(m, 0).s;
        *tmp = 0;
        status_url = string(message_buffer) + "/" + status_name;
        *tmp = '/';

        auto trim = status_url.find(':');
        if(trim != string::npos)
            status_url.erase(trim);

        assert(rtosc_type(m, 1) == 'b');
        auto blob = rtosc_argument(m, 1).b;
        delete[] status_metadata;
        status_metadata = new char[blob.len];
        memcpy(status_metadata, blob.data, blob.len);


        //Request the value of the field when possible
        auto meta = rtosc::Port::MetaContainer(status_metadata);
        if(meta.find("parameter") != meta.end())
            lo_send(lo_addr, status_url.c_str(), "");
    } else {
        status_name     = "";
        status_url      = "";
        status_value    = "";
    }


    for(unsigned i=0; i<fields; ++i)
        emit_status_field(rtosc_argument(m,2*i).s,
                          (const char*)rtosc_argument(m,2*i+1).b.data,
                          fields==1 ? LONG : SHORT);
    wrefresh(status);
}

void rebuild_status(void)
{
    //Base port level from synth code
    const char *src    = strlen(message_buffer) ? message_buffer+1 : "";
    if(index(src, ' ')) return; //avoid complete strings

    //Trim the string to its last subpath.
    //This allows manual filtering of the results
    char       *s1     = strdup(src);
    char       *s2     = strdup(src);
    const char *path   = NULL;
    const char *needle = NULL;
    char *tmp          = rindex(s2, '/');

    if(!tmp) {
        needle = s2;
        path   = "";

        tmp = s2;
        while(*tmp++)
            if(isdigit(*tmp))
                *tmp=0;
    } else {
        needle = tmp + 1;

        //eliminate digits
        while(*tmp++)
            if(isdigit(*tmp))
                *tmp=0;

        //terminate string
        rindex(s1, '/')[1] = 0;
        path   = s1;
    }

    char buffer[2048];
    size_t len = rtosc_message(buffer, 2048, "/path-search", "ss", path, needle);
    lo_message msg = lo_message_deserialise(buffer, len, NULL);
    if(lo_addr)
        lo_send_message(lo_addr, buffer, msg);
    free(s1);
    free(s2);
}

void tab_complete(void)
{
    if(tab_recommendation.empty())
        return;
    const char *src = tab_recommendation.c_str();
    char *w_ptr = rindex(message_buffer,'/');
    if(w_ptr)
        ++w_ptr;
    else {
        w_ptr = message_buffer+1;
        message_buffer[0] = '/';
    }

    while(*src && *src != '#' && *src != ':')
        *w_ptr++ = *src++;

    if(*w_ptr != '\0' && *src == '#')
        strcat(message_buffer, "/");

    message_pos = strlen(message_buffer);
}

void error_cb(int i, const char *m, const char *loc)
{
    wprintw(log, "liblo :-( %d-%s@%s\n",i,m,loc);
}

template<class T>
string toString(const T &t)
{
    std::stringstream converter;
    converter << t;
    return converter.str();
}

int all_strings(const char *args)
{
    while(args && *args) {
        if(*args != 's')
            break;
        args++;
    }
    return *args==0;
}

void update_status_info(const char *buffer)
{
    const char *args = rtosc_argument_string(buffer);
    if(!strcmp(args , "f")) {
        status_value = toString(rtosc_argument(buffer,0).f);
        status_type  = 'f';
    } else if(!strcmp(args, "i")) {
        status_value = toString(rtosc_argument(buffer,0).i);
        status_type = 'i';
    } else if(!strcmp(args, "c")) {
        status_value = toString((int)rtosc_argument(buffer,0).i);
        status_type = 'c';
    } else if(!strcmp(args, "T")) {
        status_value = "T";
        status_type = 'T';
    } else if(!strcmp(args, "F")) {
        status_value = "F";
        status_type = 'T';
    } else if(all_strings(args)) {
        status_type = 's';
        status_value = rtosc_argument(buffer,0).s;
        for(int i=1; i<(int)rtosc_narguments(buffer); ++i) {
            status_value += "\n";
            status_value += rtosc_argument(buffer,i).s;
        }
    } else
        status_type = 0;
    werase(status);
    emit_status_field(status_name.c_str(), status_metadata, LONG);
    wrefresh(status);
}

int handler_function(const char *path, const char *, lo_arg **, int, lo_message msg, void *)
{
    static char buffer[1024*20];
    memset(buffer, 0, sizeof(buffer));
    size_t size = sizeof(buffer);
    lo_message_serialise(msg, path, buffer, &size);
    if(!strcmp("/paths", buffer)) // /paths:sbsbsbsbsb...
        update_paths(buffer, NULL);
    else if(!strcmp("/exit", buffer))
        die_nicely(buffer, NULL);
    else if(status_url == path)
        update_status_info(buffer);
    else if(!strcmp("undo_change", buffer))
        ;//ignore undo messages
    else
        display(buffer, NULL);

    return 0;
}

void process_message(void)
{
    //alias
    const char *m = message_buffer;
    //Check for special cases
    if(strstr(m, "disconnect")==m) {
        if(lo_addr) {
            lo_address_free(lo_addr);
            lo_addr = NULL;
	}
    }
    else if(strstr(m, "quit")==m)
        do_exit = true;
    else if(strstr(m, "exit")==m)
        do_exit = true;
    else if(strstr(m, "connect")==m) {
        while(*m && !isdigit(*m)) ++m;
        if(isdigit(*m)) { //lets hope lo is robust :p
            if(lo_addr)
                lo_address_free(lo_addr);
            lo_addr = lo_address_new(NULL, m);

            //populate fields
            if(lo_addr)
                lo_send(lo_addr, "/path-search", "ss", "", "");
        }

    } else if(strstr(m, "help")==m) {
        wprintw(log, "\nWelcome to oscprompt...\n");
        wprintw(log, "To start talking to an app please run\nconnect PORT\n");
        wprintw(log, "From here you can enter in osc paths with arguments to send messages. ");
        wprintw(log, "Assuming that everything has connected properly you should see some metadata ");
        wprintw(log, "telling you about the ports...\n");
    } else { //normal OSC message
        if(error)
            wprintw(status,"bad message...");
        else
            send_message();
    }

    //History Buffer
    for(unsigned i=sizeof(history_buffer)/sizeof(history_buffer[0])-1; i>0; --i)
        memcpy(history_buffer[i], history_buffer[i-1], sizeof(history_buffer[0]));
    memcpy(history_buffer[0], message_buffer, sizeof(message_buffer));
    history_pos = 0;


    //Reset message buffer
    memset(message_buffer,    0, 1024);
    memset(message_arguments, 0, 32);
    message_narguments      = 0;
    message_pos             = 0;
}

template<class T>
T min(T a, T b) { return b<a?b:a; }
template<class T>
T max(T a, T b) { return b<a?a:b; }

void try_param_add(int size)
{
    if(status_url.empty() || status_value.empty() || status_type == 0)
        return;
    if(status_type == 'c') {
        int x = atoi(status_value.c_str());
        if(x != 127)
            lo_send(lo_addr, status_url.c_str(), "c", min(127,x+size));
    }
}

void try_param_sub(int size)
{
    if(status_url.empty() || status_value.empty() || status_type == 0)
        return;
    if(status_type == 'c') {
        int x = atoi(status_value.c_str());
        if(x != 0)
            lo_send(lo_addr, status_url.c_str(), "c", max(0,x-size));
    }
}

int main()
{
    //For misc utf8 chars
    setlocale(LC_ALL, "");

    memset(message_buffer,   0,sizeof(message_buffer));
    memset(history_buffer,   0,sizeof(history_buffer));
    memset(message_arguments,0,sizeof(message_arguments));
    history_pos = 0;
    int ch;


    render_setup();


    //setup liblo - it can choose its own port
    lo_server server = lo_server_new_with_proto(NULL, LO_UDP, error_cb);
    lo_server_add_method(server, NULL, NULL, handler_function, NULL);
    //lo_addr          = lo_address_new_with_proto(LO_UDP, NULL, "8080");
    wprintw(log, "lo server running on %d\n", lo_server_get_port(server));

    //Loop on prompt until the program should exit
    do {
        //Redraw prompt
        wclrtoeol(prompt);
        wprintw(prompt,":> ");
        error = print_colorized_message(prompt);
        wprintw(prompt,"\r");
        wrefresh(prompt);

        //box(helper_box,0,0);
        //wrefresh(helper_box);

        wrefresh(log);

        wrefresh(status);

        //Handle events from backend
        lo_server_recv_noblock(server, 0);

        //FILE *file;
        switch(ch = wgetch(prompt)) {
            case '\\':
                try_param_add(1);
                break;
            case ']':
                try_param_sub(1);
                break;
            case '|':
                try_param_add(10);
                break;
            case '}':
                try_param_sub(10);
                break;
            case KEY_BACKSPACE:
            case '':
            case 127: //ascii 'DEL'
                if(message_pos)
                    message_buffer[--message_pos] = 0;
                rebuild_status();
                break;
            case 91: //escape
                ch = wgetch(prompt);
                if(ch == 'A') {//up
                    history_pos = (history_pos+1)%history_size;
                    memcpy(message_buffer, history_buffer[history_pos], sizeof(message_buffer));
                    message_pos = strlen(message_buffer);
                } else if(ch == 'B') {//down
                    history_pos = (history_pos-1+history_size)%history_size;
                    memcpy(message_buffer, history_buffer[history_pos], sizeof(message_buffer));
                    message_pos = strlen(message_buffer);
                }
                break;
            case '\t':
                tab_complete();
                rebuild_status();
                break;
            case '\n':
            case '\r':
                process_message();
                break;
            case 3: //Control-C
                do_exit = 1;
                break;
            case KEY_RESIZE:
                log    = newwin(LINES-3, COLS/2-3, 1, 1);
                status = newwin(LINES-3, COLS/2-3, 1, COLS/2+1);
                prompt = newwin(1, COLS, LINES-1,0);
                scrollok(log, TRUE);
                idlok(log, TRUE);
                wtimeout(prompt, 10);
                {
                    WINDOW *helper_box = newwin(LINES-1,COLS/2-1,0,0);
                    box(helper_box,0,0);
                    wrefresh(helper_box);
                }
                {
                    WINDOW *helper_box = newwin(LINES-1,COLS/2,0,COLS/2);
                    box(helper_box,0,0);
                    wrefresh(helper_box);
                }
                break;
            default:
                if(ch > 0 && isprint(ch)) {
                    message_buffer[message_pos++] = ch;
                    rebuild_status();
                } else {
                    usleep(100);
                }
        }

    } while(!do_exit);
    
    lo_server_free(server);
    delete[] status_metadata;
    
    endwin();
    return 0;
}

