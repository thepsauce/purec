#include "cmd.h"
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
        [VISUAL_MODE] = { visual_handle_input, getch_digit },
        [VISUAL_BLOCK_MODE] = { visual_handle_input, getch_digit },
        [VISUAL_LINE_MODE] = { visual_handle_input, getch_digit },
    };

    struct buf *buf;
    const struct input_handler *ih;

    setlocale(LC_ALL, "");

    initscr();
    raw();
    keypad(stdscr, true);
    noecho();

    set_tabsize(4);
    /* keep the cursor hidden so it does not jump around */
    curs_set(0);

    set_escdelay(0);

    //buf = create_buffer("../VM_jsm/cases.h");
    buf = create_buffer("src/main.c");
    //buf = create_buffer(NULL);

    (void) create_frame(NULL, 0, buf);
    FirstFrame->buf = buf;

    set_mode(NORMAL_MODE);

    IsRunning = true;
    while (IsRunning) {
        erase();
        for (struct frame *f = FirstFrame; f != NULL; f = f->next) {
            render_frame(f);
        }
        if (Message != NULL) {
            move(LINES - 1, 0);
            addstr(Message);
        }
        move(SelFrame->y + SelFrame->cur.line - SelFrame->scroll.line,
                (SelFrame->x == 0 ? 0 : SelFrame->x + 1) + SelFrame->cur.col -
                SelFrame->scroll.col);
        curs_set(1);
        while (Mode.counter = 0, ih = &input_handlers[Mode.type],
                ih->handler(ih->getter_ch()) == 0) {
            (void) 0;
        }
        curs_set(0);
    }

    endwin();

    for (struct frame *frame = FirstFrame, *next; frame != NULL; frame = next) {
        next = frame->next;
        free(frame);
    }

    free(CmdLine.buf);
    delete_buffer(buf);
    return 0;
}
