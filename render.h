#include <ncurses.h>
#include <string>
//UI Windows
extern WINDOW *prompt;  //The input pane
extern WINDOW *log;     //The outupt pane
extern WINDOW *status;  //The pattern matching and documentation pane

//Global buffer for user entry
extern char message_buffer[1024];
extern char message_arguments[32];
extern int  message_pos;
extern int  message_narguments;

//Status tab globals
extern std::string status_url;
extern std::string status_name;
extern char *status_metadata;
extern std::string status_value;
extern char status_type;

typedef const char *msg_t;

enum presentation_t
{
    SHORT,
    LONG
};

void render_setup(void);
void display(msg_t msg, void*);
bool print_colorized_message(WINDOW *window);
void emit_status_field(const char *name, const char *metadata, presentation_t mode);
void update_paths(msg_t m, void*);
