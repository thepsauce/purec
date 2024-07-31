#include "frame.h"
#include "util.h"

#include <locale.h>

#include <stdio.h>
#include <string.h>

#include <ncurses.h>

int main(void)
{
    static const struct input_handler {
        int (*handler)(int c);
        int (*getter_ch)(void);
    } input_handlers[] = {
        [NORMAL_MODE] = { normal_handle_input, getch_digit },
        [INSERT_MODE] = { insert_handle_input, getch },
    };

    struct buf *buf;
    struct frame fr;
    const struct input_handler *ih;

    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    keypad(stdscr, true);
    noecho();

    set_tabsize(4);
    /* keep the cursor hidden so it does not jump around */
    curs_set(0);

    //buf = create_buffer("../VM_jsm/cases.h");
    buf = create_buffer("src/main.c");

    memset(&fr, 0, sizeof(fr));
    fr.x = 0;
    fr.y = 0;
    fr.w = COLS;
    fr.h = LINES - 2; /* minus status bar, command line */
    fr.buf = buf;

    set_mode(NORMAL_MODE);

    SelFrame = &fr;

    while (1) {
        erase();
        render_frame(&fr);
        if (Message != NULL) {
            move(LINES - 1, 0);
            addstr(Message);
        }
        move(SelFrame->cur.line - SelFrame->scroll.line,
                SelFrame->cur.col - SelFrame->scroll.col);
        curs_set(1);
        while (Mode.counter = 0, ih = &input_handlers[Mode.type],
                ih->handler(ih->getter_ch()) == 0) {
            (void) 0;
        }
        curs_set(0);
    }

    delete_buffer(buf);
    endwin();
    return 0;
}
