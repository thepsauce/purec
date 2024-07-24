#include "frame.h"

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

    buf = create_buffer("src/main.c");

    memset(&fr, 0, sizeof(fr));
    fr.x = 0;
    fr.y = 0;
    fr.w = COLS;
    fr.h = LINES - 1; /* minus status bar */
    fr.buf = buf;

    set_mode(NORMAL_MODE);

    while (1) {
        erase();
        for (size_t i = 0; i < buf->num_lines; i++) {
            addnstr(buf->lines[i].s, buf->lines[i].n);
            addch('\n');
        }
        move(LINES - 1, 0);
        clrtoeol();
        move(LINES - 1, 0);
        printw("%zu/%zu:%zu", fr.cur.line + 1, buf->num_lines, fr.cur.col + 1);
        move(fr.cur.line, fr.cur.col);
        while (handle_input(&fr, getch()) == 0);
    }

    delete_buffer(buf);
    endwin();
    return 0;
}
