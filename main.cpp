#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <rtosc/rtosc.h>
#include <rtosc/ports.h>
#include <rtosc/thread-link.h>
#include <cstdlib>
#include <cctype>
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

int i = 2; //Printer location
int error = 0;

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
    arg_t *args = new arg_t[message_narguments];

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

bool print_colorized_message(void)
{
    //Reset globals
    i = 2;
    message_narguments = 0;

    bool error = false;
    const char *str = message_buffer;

    //Print the path
    attron(A_BOLD);
    while(*str && *str!=' ')
        mvprintw(1, i++, "%c", *str++);
    attroff(A_BOLD);

    do {
        while(*str==' ')
            mvprintw(1, i++, " "), ++str;

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

                attron(COLOR_PAIR(is_float ? 4:1));
                while(*str && (isdigit(*str) || *str == '.' || *str == '-'))
                    mvprintw(1,i++,"%c",*str++);
                attroff(COLOR_PAIR(is_float ? 4:1));

                //Stuff was left on the end of the the number
                while(*str && *str != ' ') {
                    error = true;
                    attron(COLOR_PAIR(2));
                    mvprintw(1,i++,"%c", *str++);
                    attroff(COLOR_PAIR(2));
                }
                break;

            case 'T':
                message_arguments[message_narguments++] = 'T';
                attron(COLOR_PAIR(5));
                mvprintw(1,i++,"T");
                attroff(COLOR_PAIR(5));
                ++str;
                break;
            case 'F':
                message_arguments[message_narguments++] = 'F';
                attron(COLOR_PAIR(5));
                mvprintw(1,i++,"F");
                attroff(COLOR_PAIR(5));
                ++str;
                break;

            case '"':
                message_arguments[message_narguments++] = 's';
                attron(COLOR_PAIR(3));
                mvprintw(1,i++,"%c",*str++);
                while(*str && *str != '"')
                    mvprintw(1,i++,"%c",*str++);
                if(*str == '"')
                    mvprintw(1,i++,"%c",*str++);
                else
                    error = true;
                attroff(COLOR_PAIR(3));
                break;
            default:
                attron(COLOR_PAIR(2));
                mvprintw(1,i++,"%c", *str++);
                attroff(COLOR_PAIR(2));
                error = true;
            case '\0':
                ;
        }

    } while(*str); //Parse more args
    return error;
}


void display(msg_t msg, void*)
{
    move(5,0);
    insertln();
    const unsigned nargs = rtosc_narguments(msg);
    for(int i=0; i<nargs; ++i) {
        switch(rtosc_type(msg, i)) {
            case 's':
                printw("%s ", rtosc_argument(msg,i).s);
                break;
            case 'i':
                printw("%d ", rtosc_argument(msg,i).i);
                break;
            case 'f':
                printw("%f ", rtosc_argument(msg,i).f);
                break;
        }
    }
    move(1,i);
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

void tab_complete(void)
{
    //Base port level from synth code
    extern Ports *backend_ports;

    //Either get a string from the start or from a later path contained in a
    //string
    char *str = NULL;
    if(*message_buffer=='/')
        str = message_buffer;
    char *tmp = rindex(message_buffer,'"');
    if(tmp && tmp[1] == '/')
        str = tmp+1;

    //Try to perform tab completion based upon the given string
    if(str) {
        const Port *port = backend_ports->apropos(str+1);
        if(!port) {
            mvprintw(2,0,"no match...\n");
            mvprintw(3,0,"                                 \n");
        } else {
            mvprintw(2,0,"best match (%s)...\n", port->name);
            if(index(port->name,'/')) {
                mvprintw(3,0,"                                 \n");
            } else
                mvprintw(3,0,"doc %s\n", port->metadata);
            //Now to rewrite the buffer's end with this new match
            char *w_ptr = rindex(message_buffer,'/')+1;
            const char *src = port->name;
            while(*src && *src != '#' && *src != ':')
                *w_ptr++ = *src++;
            message_pos = strlen(message_buffer);
        }
    }
}

void init_audio(void);
int main()
{
    init_audio();
    memset(message_buffer,   0,sizeof(message_buffer));
    memset(message_arguments,0,sizeof(message_arguments));
    int ch;
    bool error = false;

    //Initialize NCurses
    initscr();
    raw();
    keypad(stdscr, TRUE);
    timeout(100);
    noecho();

    start_color();
    init_pair(1, COLOR_BLUE,   COLOR_BLACK);
    init_pair(2, COLOR_RED,    COLOR_BLACK);
    init_pair(3, COLOR_GREEN,  COLOR_BLACK);
    init_pair(4, COLOR_CYAN,   COLOR_BLACK);
    init_pair(5, COLOR_YELLOW, COLOR_BLACK);


    //Loop on prompt until the program should exit
    printw("Type magical OSC incantations or (/help)\n> ");

    do {
        //Handle events from backend
        while(bToU.hasNext())
            viewports.dispatch(bToU.read()+1, NULL);

        FILE *file;
        switch(ch = getch()) {
            case KEY_BACKSPACE:
                if(message_pos) {
                    message_buffer[--message_pos] = 0;
                    mvprintw(1,i-1," ");
                }
                break;
            case KEY_F(1): //saving snarf
                file=fopen("osc-prompt.snarf","wb");
                if(!file) {
                    mvprintw(2,0,"Unable to snarf file!");
                    continue;
                }
                fwrite(snarf_buffer, rtosc_message_length(snarf_buffer),
                        1, file);
                mvprintw(2,0,"snarf saved...");
                break;
            case KEY_F(2): //loading snarf
                file=fopen("osc-prompt.snarf","rb");
                if(!file) {
                    mvprintw(2,0,"Unable to unsnarf file!");
                    continue;
                }
                memset(snarf_buffer, 0, MAX_SNARF);
                fread(snarf_buffer, MAX_SNARF, 1, file);
                mvprintw(2,0,"snarf loaded...");
                break;
            case '\t':
                tab_complete();
                break;
            case '\n':
            case '\r':
                if(error)
                    mvprintw(2,0,"bad message...");
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
            default:
                if(ch > 0)
                    message_buffer[message_pos++] = ch;
                else
                    continue;
        }

        //Redraw prompt
        mvprintw(1,0,"                                                      ");
        mvprintw(1,0,"> ");
        error = print_colorized_message();

        //Update screen
        refresh();
    } while(!do_exit);

    endwin();
    return 0;
}

