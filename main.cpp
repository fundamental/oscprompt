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
using namespace rtosc;
using std::string;

//Global buffer for user entry
char message_buffer[1024];
char message_arguments[32];
int  message_pos=0;
int  message_narguments=0;

//Tab completion recommendation
string tab_recommendation;

//Error detected in user promppt
int error = 0;

//Liblo destination
lo_address lo_addr;

//UI Windows
WINDOW *prompt;  //The input pane
WINDOW *log;     //The outupt pane
WINDOW *status;  //The pattern matching and documentation pane


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

//Check if a string is a float
int float_p(const char *str)
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


/**
 * Write a "/display *" style message to the log screen piece by piece
 */
void display(msg_t msg, void*)
{
    wprintw(log, "\n\n%s", msg);
    const unsigned nargs = rtosc_narguments(msg);
    for(int i=0; i<nargs; ++i) {
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
            case 'f':
                wattron(log, COLOR_PAIR(4));
                wprintw(log, "%f", rtosc_argument(msg,i).f);
                wattroff(log, COLOR_PAIR(4));
                break;
        }
    }
    wrefresh(log);
}

int do_exit = 0;

void die_nicely(msg_t, void*)
{
    do_exit = 1;
}

enum presentation_t
{
    SHORT,
    LONG
};

void emit_status_field(const char *name, const char *metadata, presentation_t mode);

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


    for(unsigned i=0; i<fields; ++i)
        emit_status_field(rtosc_argument(m,2*i).s,
                          rtosc_argument(m,2*i+1).s,
                          fields==1 ? LONG : SHORT);
    wrefresh(status);
}

void emit_status_field(const char *name, const char *metadata, presentation_t mode)
{
    if(!metadata)
        metadata = "";

    const char *doc_str = rindex(metadata, ':');
    if(doc_str)
        doc_str++;

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
        if(index(metadata, ':') && index(metadata, ':')[1] != ':') {
            wattron(status, A_BOLD);
            wprintw(status, "  Properties:");
            wattroff(status, A_BOLD);
            int quotes = 0;
            for(const char *p=index(metadata, ':')+1; *p && *p != ':'; ++p) {
                if(*p=='\'' && !(quotes++%2))
                    wprintw(status, "\n    ");
                wprintw(status, "%c", *p);
            }
            wprintw(status, "\n");
        }
        if(*metadata && *metadata != ':') {
            wattron(status, A_BOLD);
            wprintw(status, "  Midi Conversion:\n    ");
            wattroff(status, A_BOLD);
            for(const char *p=metadata; *p && *p != ':'; ++p)
                wprintw(status, "%c", *p);
        }
    }
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
    message_pos = strlen(message_buffer);
}

void error_cb(int i, const char *m, const char *loc)
{
    wprintw(log, "liblo :-( %d-%s@%s\n",i,m,loc);
}

int handler_function(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
    //lo_address addr = lo_message_get_source(msg);
    //if(addr)
    //    response_url = lo_address_get_url(addr);

    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    size_t size = 2048;
    lo_message_serialise(msg, path, buffer, &size);
    if(!strcmp("/paths", buffer))
        update_paths(buffer, NULL);
    else if(!strcmp("/exit", buffer))
        die_nicely(buffer, NULL);
    else
        display(buffer, NULL);
}

void process_message(void)
{
    //alias
    const char *m = message_buffer;
    //Check for special cases
    if(strstr(m, "disconnect")==m)
        lo_addr = NULL;
    else if(strstr(m, "quit")==m)
        do_exit = true;
    else if(strstr(m, "exit")==m)
        do_exit = true;
    else if(strstr(m, "connect")==m) {
        while(*m && !isdigit(*m)) ++m;
        if(isdigit(*m)) //lets hope lo is robust :p
            lo_addr = lo_address_new_with_proto(LO_UDP, NULL, m);

    } else { //normal OSC message
        if(error)
            wprintw(status,"bad message...");
        else
            send_message();
    }

    //Reset message buffer
    memset(message_buffer,    0, 1024);
    memset(message_arguments, 0, 32);
    message_narguments      = 0;
    message_pos             = 0;
}

int main()
{
    //For misc utf8 chars
    setlocale(LC_ALL, "");

    memset(message_buffer,   0,sizeof(message_buffer));
    memset(message_arguments,0,sizeof(message_arguments));
    int ch;
    bool error = false;


    //Initialize NCurses
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);

    start_color();
    init_pair(1, COLOR_BLUE,   COLOR_BLACK);
    init_pair(2, COLOR_RED,    COLOR_BLACK);
    init_pair(3, COLOR_GREEN,  COLOR_BLACK);
    init_pair(4, COLOR_CYAN,   COLOR_BLACK);
    init_pair(5, COLOR_YELLOW, COLOR_BLACK);

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

    //setup liblo - it can choose its own port
    lo_server server = lo_server_new_with_proto(NULL, LO_UDP, error_cb);
    lo_method handle = lo_server_add_method(server, NULL, NULL, handler_function, NULL);
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

        FILE *file;
        switch(ch = wgetch(prompt)) {
            case KEY_BACKSPACE:
            case '':
            case 127: //ascii 'DEL'
                if(message_pos)
                    message_buffer[--message_pos] = 0;
                rebuild_status();
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

    endwin();
    return 0;
}

