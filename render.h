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
