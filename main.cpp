#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <rtosc.h>
#include <thread-link.h>

extern ThreadLink<1024,1024> bToU;
extern ThreadLink<1024,1024> uToB;

char buffer[1024];
char args[32];
int  idx=0;
int  aidx=0;

//Check if a string is a float
int float_p(const char *str)
{
    int result = 0;
    while(*str && *str != ' ')
        result |= *str++ == '.';
    return result;
}

int i = 2;
int error = 0;
void display(msg_t msg, void*)
{
    move(2,0);
    insertln();
    mvprintw(2,0,rtosc_argument(msg,0).s);
    move(1,i);
}

Ports<1,void> viewports{{{
    Port<void>("display:s", "", display)
}}};

void init_audio(void);
int main()
{
    init_audio();
    memset(buffer,0,1024);
    int ch;

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

    printw("Type magical OSC incantations or (/help)\n> ");
loop:
    while(bToU.hasNext())
        viewports.dispatch(bToU.read()+1, NULL);
    ;
    ch   = getch();
    if(ch == KEY_BACKSPACE) {
        if(idx) {
            buffer[--idx] = 0;
            mvprintw(1,i-1," ");
        }
    } else if(ch == '\n' || ch == '\r') {
        if(error)
            mvprintw(2,0,"bad message...");
        else {
            const char *str = buffer;
            char name[1024];
            char *_name = name;
            while(*str && *str != ' ')
                *_name++ = *str++;
            *_name = 0;

            if(aidx) { //there must be arguments
                arg_t *_args = new arg_t[aidx];
                //Process one integer, float or string at a time
                for(int i=0; i<aidx; ++i) {
                    char b[1024];
                    char *_b = b;
                    switch(args[i]) {
                        case 'i':
                            while(!isdigit(*str)) ++str;
                            _args[i].i = atoi(str);

                            while(isdigit(*str)) ++str;
                            break;
                        case 'f':
                            while(!isdigit(*str)) ++str;
                            _args[i].f = atof(str);
                            while(isdigit(*str) || *str=='.') ++str;
                            break;
                        case 's':
                            while(*str!='"') ++str;
                            _args[i].s = _b;
                            ++str;
                            while(*str!='"') *_b++ = *str++;
                            str++;
                            *++_b = 0;
                            break;
                        case 'T':
                        case 'F':
                            while(*str != 'T' && *str != 'F') ++str;
                            ++str;
                    }
                }
                uToB.writeArray(name, args, _args);
            }
            else {
                uToB.write(name, "");
            }
        }
        memset(buffer, 0, 1024);
        memset(args, 0, 32);
        idx = 0;
        aidx = 0;
    } else if(ch == 3) {
        endwin();
        return 0;
    } else if(ch>0)
        buffer[idx++] = ch;
    else
        goto loop;

    error = 0;
    aidx = 0;
    i = 2;
    mvprintw(1,0,"                                        ");

    mvprintw(1,0,"> ");

    const char *str = buffer;

    //Print the path
    attron(A_BOLD);
    while(*str && *str != ' ') mvprintw(1,i++,"%c",*str++);
    attroff(A_BOLD);

hop:

    while(*str == ' ') mvprintw(1,i++," "), ++str;

    int is_float = float_p(str);
    switch(*str)
    {
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
            ;
            args[aidx++] = is_float ? 'f' : 'i';
            attron(COLOR_PAIR(is_float ? 4:1));
            while(*str && (isdigit(*str) || *str == '.'))
                mvprintw(1,i++,"%c",*str++);
            attroff(COLOR_PAIR(is_float ? 4:1));
            while(*str && *str != ' ') {
                error = 1;
                attron(COLOR_PAIR(2));
                mvprintw(1,i++,"%c", *str++);
                attroff(COLOR_PAIR(2));
            }
            break;

        case 'T':
            args[aidx++] = 'T';
            attron(COLOR_PAIR(5));
            mvprintw(1,i++,"T");
            attroff(COLOR_PAIR(5));
            ++str;
            break;
        case 'F':
            args[aidx++] = 'F';
            attron(COLOR_PAIR(5));
            mvprintw(1,i++,"F");
            attroff(COLOR_PAIR(5));
            ++str;
            break;

        case '"':
            args[aidx++] = 's';
            attron(COLOR_PAIR(3));
            mvprintw(1,i++,"%c",*str++);
            while(*str && *str != '"')
                mvprintw(1,i++,"%c",*str++);
            if(*str == '"')
                mvprintw(1,i++,"%c",*str++);
            else
                error = 1;
            attroff(COLOR_PAIR(3));
            break;
        default:
            attron(COLOR_PAIR(2));
            mvprintw(1,i++,"%c", *str++);
            attroff(COLOR_PAIR(2));
            error = 1;
            ;
        case '\0':
            ;
    }

    if(*str) //Parse more args
        goto hop;
    else {
        refresh();
        goto loop;
    }

exit:

    getch();
    endwin();

    return 0;
}
