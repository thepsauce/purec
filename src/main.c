#include "frame.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include <ncurses.h>

int main(void)
{
    struct buf *buf;
    struct frame fr;

    initscr();
    cbreak();
    keypad(stdscr, true);
    noecho();

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
        while (normal_handle_input(getch()) == 0);
    }

    delete_buffer(buf);
    endwin();
    return 0;
}
