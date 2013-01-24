#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <rtosc/rtosc.h>
#include <rtosc/ports.h>
#include <rtosc/thread-link.h>
#include <cstdlib>
#include <cctype>
#include <string>
using namespace rtosc;

extern ThreadLink<1024,1024> bToU;
extern ThreadLink<1024,1024> uToB;

#define MAX_SNARF 10000
extern char snarf_buffer[MAX_SNARF];

//Global buffer for user entry
char message_buffer[1024];
char message_arguments[32];
int  message_pos=0;
int  message_narguments=0;

//Error detected in user promppt
int error = 0;


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
    uToB.writeArray(path, message_arguments, args);
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
    const unsigned nargs = rtosc_narguments(msg);
    for(int i=0; i<nargs; ++i) {
        wprintw(log, i?"\n   ":"\n\n");
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

Ports viewports = {
    {"display", "", 0, display},
    {"exit", "", 0, die_nicely},
};

enum presentation_t
{
    SHORT,
    LONG
};

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
    extern const Ports *backend_ports;

    //Trim the string to its last subpath.
    //This allows manual filtering of the results
    char *str = strdup(message_buffer);

    if(index(str,' ')) {//This path is complete
        free(str);
        return;
    }

    char *thresh = rindex(str, '/');
    if(thresh) {//kill off any digits in the path to allow cheap matching
        char *digit_elim=thresh;
        while(*digit_elim++)
            if(isdigit(*digit_elim))
                *digit_elim=0;
    }

    //split strings
    if(thresh)
        *thresh = 0;

    const Ports *ports = NULL;
    if(!*str)
        ports = backend_ports;
    else {
        const Port *port = backend_ports->apropos((std::string(str+1)+'/').c_str());
        if(port)
            ports = port->ports;
    }

    werase(status);

    if(!ports) {
        wprintw(status,"no match...\n");
    } else {
        int num_fields = 0;
        for(const Port &p:*ports) {
            if(thresh && strstr(p.name, thresh+1)!=p.name)
                continue;
            ++num_fields;
        }

        for(const Port &p:*ports) {
            if(thresh && strstr(p.name, thresh+1)!=p.name)
                continue;
            emit_status_field(p.name, p.metadata, num_fields==1 ? LONG : SHORT);
        }
    }

    free(str);
}

void tab_complete(void)
{
    //Base port level from synth code
    extern Ports *backend_ports;

    //Trim the string to its last subpath.
    //This allows manual filtering of the results
    char *str = strdup(message_buffer);

    if(index(str,' ')) {//This path is complete
        free(str);
        return;
    }

    char *thresh = rindex(str, '/');
    if(thresh) {//kill off any digits in the path to allow cheap matching
        char *digit_elim=thresh;
        while(*digit_elim++)
            if(isdigit(*digit_elim))
                *digit_elim=0;
    }

    //split strings
    if(thresh)
        *thresh = 0;

    const Ports *ports = NULL;
    if(!*str)
        ports = backend_ports;
    else {
        const Port *port = backend_ports->apropos((std::string(str+1)+'/').c_str());
        if(port)
            ports = port->ports;
    }

    werase(status);

    if(!ports) {
        wprintw(status,"no match...\n");
    } else {
        for(const Port &port:*ports) {
            if(thresh && strstr(port.name, thresh+1)!=port.name)
                continue;
            char *w_ptr = rindex(message_buffer,'/')+1;
            const char *src = port.name;
            while(*src && *src != '#' && *src != ':')
                *w_ptr++ = *src++;
            message_pos = strlen(message_buffer);
            free(str);
            return;
        }
    }
    free(str);
    wprintw(status,"no match...\n");
}

void init_audio(void);
int main()
{
    //For misc utf8 chars
    setlocale(LC_ALL, "");

    init_audio();
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
    wtimeout(prompt, 100);
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
        while(bToU.hasNext())
            viewports.dispatch(bToU.read()+1, NULL);

        FILE *file;
        switch(ch = wgetch(prompt)) {
            case KEY_BACKSPACE:
            case '':
                if(message_pos)
                    message_buffer[--message_pos] = 0;
                rebuild_status();
                break;
            case KEY_F(1): //saving snarf
                file=fopen("osc-prompt.snarf","wb");
                if(!file) {
                    wprintw(status,"Unable to snarf file!");
                    continue;
                }
                fwrite(snarf_buffer, rtosc_message_length(snarf_buffer, MAX_SNARF),
                        1, file);
                wprintw(status,"snarf saved...");
                break;
            case KEY_F(2): //loading snarf
                file=fopen("osc-prompt.snarf","rb");
                if(!file) {
                    wprintw(status,"Unable to unsnarf file!");
                    continue;
                }
                memset(snarf_buffer, 0, MAX_SNARF);
                fread(snarf_buffer, MAX_SNARF, 1, file);
                wprintw(status,"snarf loaded...");
                break;
            case '\t':
                tab_complete();
                rebuild_status();
                break;
            case '\n':
            case '\r':
                if(error)
                    wprintw(status,"bad message...");
                else
                    send_message();

                //Reset message buffer
                memset(message_buffer,    0, 1024);
                memset(message_arguments, 0, 32);
                message_narguments      = 0;
                message_pos             = 0;
                break;
            case 3: //Control-C
                do_exit = 1;
                break;
            default:
                if(ch > 0 && isprint(ch)) {
                    message_buffer[message_pos++] = ch;
                    rebuild_status();
                } else
                    usleep(100);
        }

    } while(!do_exit);

    endwin();
    return 0;
}

