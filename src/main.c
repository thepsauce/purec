#include "frame.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#include <ncurses.h>

int main(void)
{
    struct buf *buf;
    struct frame fr;
    int (*input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
    };

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
    fr.h = LINES - 1; /* minus status bar */
    fr.buf = buf;

    set_mode(NORMAL_MODE);

    SelFrame = &fr;

    while (1) {
        erase();
        for (size_t i = 0; i < MIN(buf->num_lines, (size_t) fr.h); i++) {
            mvaddnstr(i, 0, buf->lines[i].s, buf->lines[i].n);
        }
        move(LINES - 1, 0);
        clrtoeol();
        move(LINES - 1, 0);
        printw("%zu/%zu:%zu", fr.cur.line + 1, buf->num_lines, fr.cur.col + 1);
        move(fr.cur.line, fr.cur.col);
        curs_set(1);
        while (Mode.counter = 0,
                input_handlers[Mode.type](getch_digit()) == 0) {
            (void) 0;
        }
        curs_set(0);
    }

    delete_buffer(buf);
    endwin();
    return 0;
}
