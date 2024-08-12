#include "color.h"
#include "input.h"
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
    int cur_x, cur_y;

    setlocale(LC_ALL, "");

    initscr();
    raw();
    keypad(stdscr, true);
    noecho();

    set_tabsize(4);

    set_escdelay(0);

    init_colors();

    Message = newpad(1, 128);
    OffScreen = newpad(1, 512);

    wbkgdset(stdscr, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(Message, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(OffScreen, ' ' | COLOR_PAIR(HI_NORMAL));

    //buf = create_buffer("../VM_jsm/cases.h");
    buf = create_buffer("src/main.c");
    //buf = create_buffer(NULL);

    (void) create_frame(NULL, 0, buf);
    FirstFrame->buf = buf;

    set_mode(NORMAL_MODE);

    IsRunning = true;
    while (IsRunning) {
        curs_set(0);
        erase();
        for (struct frame *f = FirstFrame; f != NULL; f = f->next) {
            render_frame(f);
        }

        copywin(Message, stdscr, 0, 0, LINES - 1, 0, LINES - 1, COLS - 1, 0);

        get_visual_cursor(SelFrame, &cur_x, &cur_y);
        move(cur_y, cur_x);
        curs_set(1);
        while (Mode.counter = 0, ih = &input_handlers[Mode.type],
                ih->handler(ih->getter_ch()) == 0) {
            (void) 0;
        }
    }

    /* restore terminal state */
    endwin();

    /* free resources */
    for (struct frame *frame = FirstFrame, *next; frame != NULL; frame = next) {
        next = frame->next;
        free(frame);
    }

    while (FirstBuffer != NULL) {
        destroy_buffer(FirstBuffer);
    }

    free(Input.buf);
    free(Input.remember);
    return 0;
}
