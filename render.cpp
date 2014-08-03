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
#include <cctype>
#include <cstring>
#include "render.h"


//Globals (see render.h)
WINDOW *prompt, *log, *status;

char message_buffer[1024];
char message_arguments[32];
int  message_pos=0;
int  message_narguments=0;

//Status tab globals
std::string status_url;
std::string status_name;
char *status_metadata;
std::string status_value;
char status_type = 0;

void render_setup(void)
{
    //Initialize NCurses
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);

    start_color();
    init_pair(1, COLOR_BLUE,    COLOR_BLACK);
    init_pair(2, COLOR_RED,     COLOR_BLACK);
    init_pair(3, COLOR_GREEN,   COLOR_BLACK);
    init_pair(4, COLOR_CYAN,    COLOR_BLACK);
    init_pair(5, COLOR_YELLOW,  COLOR_BLACK);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    
    //Define windows
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
}

void display(msg_t msg, void*)
{
    wprintw(log, "\n\n%s <%s>", msg, rtosc_argument_string(msg));
    const unsigned nargs = rtosc_narguments(msg);
    for(unsigned i=0; i<nargs; ++i) {
        wprintw(log, "\n   ");
        switch(rtosc_type(msg, i)) {
            case 's':
                wattron(log, COLOR_PAIR(3));
                wprintw(log, "%s", rtosc_argument(msg,i).s);
                wattroff(log, COLOR_PAIR(3));
                break;
            case 'i':
                wattron(log, COLOR_PAIR(1));
                wprintw(log, "%d", rtosc_argument(msg,i).i);
                wattroff(log, COLOR_PAIR(1));
                break;
            case 'c':
                wattron(log, COLOR_PAIR(1));
                wprintw(log, "%d", rtosc_argument(msg,i).i);
                wattroff(log, COLOR_PAIR(1));
                break;
            case 'f':
                wattron(log, COLOR_PAIR(4));
                wprintw(log, "%f", rtosc_argument(msg,i).f);
                wattroff(log, COLOR_PAIR(4));
                break;
            case 'b':
                wprintw(log, "<b>");
                break;
        }
    }
    //wrefresh(log);
}

//Check if a string is a float
static int float_p(const char *str)
{
    int result = 0;
    while(*str && *str != ' ')
        result |= *str++ == '.';
    return result;
}

bool print_colorized_message(WINDOW *window)
{
    //Reset globals
    message_narguments = 0;

    bool error = false;
    const char *str = message_buffer;

    //Print the path
    wattron(window, A_BOLD);
    while(*str && *str!=' ')
        wprintw(window, "%c", *str++);
    wattroff(window, A_BOLD);

    do {
        while(*str==' ')
            wprintw(window, " "), ++str;

        const bool is_float = float_p(str);
        switch(*str)
        {
            case '-':
            case '.':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                message_arguments[message_narguments++] = is_float ? 'f' : 'i';

                wattron(window, COLOR_PAIR(is_float ? 4:1));
                while(*str && (isdigit(*str) || *str == '.' || *str == '-'))
                    wprintw(window, "%c",*str++);
                wattroff(window, COLOR_PAIR(is_float ? 4:1));

                //Stuff was left on the end of the the number
                while(*str && *str != ' ') {
                    error = true;
                    wattron(window, COLOR_PAIR(2));
                    wprintw(window, "%c", *str++);
                    wattroff(window, COLOR_PAIR(2));
                }
                break;

            case 'c': //Char type
                message_arguments[message_narguments++] = 'c';
                wattron(window, COLOR_PAIR(6));
                wprintw(window, "%c",*str++);
                while(*str && isdigit(*str))
                    wprintw(window, "%c",*str++);
                wattroff(window, COLOR_PAIR(6));

                //Stuff was left on the end of the the number
                while(*str && *str != ' ') {
                    error = true;
                    wattron(window, COLOR_PAIR(2));
                    wprintw(window, "%c", *str++);
                    wattroff(window, COLOR_PAIR(2));
                }
                break;

            case 'T':
            case 'F':
                message_arguments[message_narguments++] = *str;
                wattron(window, COLOR_PAIR(5));
                wprintw(window, "%c", *str++);
                wattroff(window, COLOR_PAIR(5));
                break;

            case '"':
                message_arguments[message_narguments++] = 's';
                wattron(window, COLOR_PAIR(3));
                wprintw(window, "%c",*str++);
                while(*str && *str != '"')
                    wprintw(window, "%c",*str++);
                if(*str == '"')
                    wprintw(window, "%c",*str++);
                else
                    error = true;
                wattroff(window, COLOR_PAIR(3));
                break;
            default:
                wattron(window, COLOR_PAIR(2));
                wprintw(window, "%c", *str++);
                wattroff(window, COLOR_PAIR(2));
                error = true;
            case '\0':
                ;
        }

    } while(*str); //Parse more args
    return error;
}

void emit_status_field(const char *name, const char *metadata, presentation_t mode)
{
    if(!metadata)
        metadata = "";

    rtosc::Port::MetaContainer itr(metadata);

    const char *doc_str = itr["documentation"];

    int color = 0;
    if(strstr(name, ":f"))
        color = 4;
    else if(strstr(name, ":i"))
        color = 1;
    else if(strstr(name, ":T") || strstr(name, ":F"))
        color = 5;
    else if(strstr(name, ":s"))
        color = 3;
    else
        color = 2;

    wattron(status, A_BOLD);
    if(color)
        wattron(status, COLOR_PAIR(color));
    wprintw(status, "%s ::\n", name);
    if(color)
        wattroff(status, COLOR_PAIR(color));
    wattroff(status, A_BOLD);

    wprintw(status,"    %s\n", doc_str);

    if(mode==LONG) {
        wattron(status, A_BOLD);
        wprintw(status, "  Properties:\n");
        wattroff(status, A_BOLD);
        for(auto val : itr) {
            if(val.value)
                wprintw(status, "    %s: %s\n", val.title, val.value);
            else
                wprintw(status, "    %s\n", val.title);
        }

        wprintw(status, "\n");

        if(itr.find("parameter") != itr.end()) {
            wattron(status, A_BOLD);
            wprintw(status, "  Value:\n");
            wattroff(status, A_BOLD);

            wprintw(status, "    %s ", status_value.c_str());

            if(itr["units"])
                wprintw(status, "%s", itr["units"]);
            wprintw(status, "\n");
            //wprintw(status, "'%s'"
        }
    }
}
