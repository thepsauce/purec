#include "color.h"
#include "input.h"
#include "frame.h"
#include "purec.h"
#include "xalloc.h"

#include <locale.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

WINDOW *Message;

WINDOW *OffScreen;

bool IsRunning;

int ExitCode;

struct core Core;

bool is_playback(void)
{
    return Core.play_i < Core.dot_e || Core.repeat_count > 0;
}

int get_ch(void)
{
    int c;

    if (Core.play_i >= Core.dot_e) {
        if (Core.repeat_count > 0) {
            Core.repeat_count--;
            Core.play_i = Core.dot_i;
        } else {
            Core.play_i = SIZE_MAX;
        }
    }

    if (Core.play_i != SIZE_MAX) {
        c = Core.rec[Core.play_i++];
        if ((c & 0xf0) == 0xf0) {
            c = (c & 0x1) << 8;
            c |= Core.rec[Core.play_i++];
        }
    } else {
        c = getch();
        if (c > 0xff) {
            if (Core.rec_len + 2 > Core.a_rec) {
                Core.a_rec *= 2;
                Core.a_rec += 2;
                Core.rec = xrealloc(Core.rec, Core.a_rec);
            }
            Core.rec[Core.rec_len++] = 0xf0 | ((c & 0x100) >> 8);
            Core.rec[Core.rec_len++] = c & 0xff;
        } else {
            if (Core.rec_len + 1 > Core.a_rec) {
                Core.a_rec *= 2;
                Core.a_rec++;
                Core.rec = xrealloc(Core.rec, Core.a_rec);
            }
            Core.rec[Core.rec_len++] = c;
        }
    }
    return c;
}

int get_extra_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    while (c = get_ch(), (c == '0' && counter != 0) || (c >= '1' && c <= '9')) {
        /* add a digit to the counter */
        new_counter = counter * 10 + c - '0';
        /* check for overflow */
        if (new_counter < counter) {
            new_counter = SIZE_MAX;
        }
        counter = new_counter;
    }

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);

    return c;
}

size_t get_mode_line_end(struct line *line)
{
    if (Core.mode != NORMAL_MODE) {
        return line->n;
    }
    /* adjustment for normal mode */
    return line->n == 0 ? 0 : line->n - 1;
}

void set_mode(int mode)
{
    if (IS_VISUAL(mode) && !IS_VISUAL(Core.mode)) {
        Core.pos = SelFrame->cur;
    }

    Core.mode = mode;

    if (mode == NORMAL_MODE) {
        clip_column(SelFrame);
    } else if (mode == INSERT_MODE) {
        Core.ev_from_ins = SelFrame->buf->event_i;
    }
}

bool get_selection(struct selection *sel)
{
    if (Core.mode == VISUAL_BLOCK_MODE) {
        sel->is_block = true;
        sel->beg.line = MIN(SelFrame->cur.line, Core.pos.line);
        sel->beg.col = MIN(SelFrame->cur.col, Core.pos.col);
        sel->end.line = MAX(SelFrame->cur.line, Core.pos.line);
        sel->end.col = MAX(SelFrame->cur.col, Core.pos.col);
    } else {
        sel->is_block = false;
        sel->beg = SelFrame->cur;
        sel->end = Core.pos;
        sort_positions(&sel->beg, &sel->end);
        if (Core.mode == VISUAL_LINE_MODE) {
            sel->beg.col = 0;
            sel->end.col = 0;
            sel->end.line++;
        }
    }
    return IS_VISUAL(Core.mode);
}

bool is_in_selection(const struct selection *sel, const struct pos *pos)
{
    if (sel->is_block) {
        return is_in_block(pos, &sel->beg, &sel->end);
    }
    return is_in_range(pos, &sel->beg, &sel->end);
}

int main(void)
{
    static const char cursors[] = {
        [NORMAL_MODE] = '\x30',

        [INSERT_MODE] = '\x35',

        [VISUAL_MODE] = '\x30',
        [VISUAL_LINE_MODE] = '\x30',
        [VISUAL_BLOCK_MODE] = '\x30',
    };

    static const char *mode_strings[] = {
        [NORMAL_MODE] = "",

        [INSERT_MODE] = "-- INSERT --",

        [VISUAL_MODE] = "-- VISUAL --",
        [VISUAL_LINE_MODE] = "-- VISUAL LINE --",
        [VISUAL_BLOCK_MODE] = "-- VISUAL BLOCK --",
    };

    static int (*const input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
        [VISUAL_MODE] = visual_handle_input,
        [VISUAL_BLOCK_MODE] = visual_handle_input,
        [VISUAL_LINE_MODE] = visual_handle_input,
    };

    struct buf *buf;
    int cur_x, cur_y;
    size_t first_event;
    int c;
    int r;
    size_t next_dot_i;
    int old_mode;
    struct frame *old_frame;

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
    while (1) {
        curs_set(0);

        erase();

        for (struct frame *f = FirstFrame; f != NULL; f = f->next) {
            render_frame(f);
        }

        wmove(Message, 0, 0);
        wattr_on(Message, A_BOLD, NULL);
        waddstr(Message, mode_strings[Core.mode]);
        if (Core.mode == INSERT_MODE && Core.counter > 1) {
            wprintw(Message, " REPEAT * %zu", Core.counter);
        }
        wattr_off(Message, A_BOLD, NULL);

        if (Core.user_rec_ch != '\0') {
            waddstr(Message, " recording @");
            waddch(Message, Core.user_rec_ch);
        }
        wclrtoeol(Message);

        copywin(Message, stdscr, 0, 0, LINES - 1, 0, LINES - 1,
                MIN(COLS - 1, getmaxx(Message)), 0);

        get_visual_cursor(SelFrame, &cur_x, &cur_y);
        move(cur_y, cur_x);

        printf("\x1b[%c q", cursors[Core.mode]);
        fflush(stdout);
        curs_set(1);

        old_frame = SelFrame;
        first_event = SelFrame->buf->event_i;

        do {
            next_dot_i = Core.rec_len;
            if (Core.mode == INSERT_MODE) {
                c = get_ch();
            } else {
                Core.counter = 1;
                Core.move_down_count = 0;
                c = get_extra_char();
            }

            old_mode = Core.mode;
            r = input_handlers[Core.mode](c);
            if (!is_playback()) {
                if (old_mode == NORMAL_MODE) {
                    if ((r & DO_RECORD) && Core.play_i == SIZE_MAX) {
                        Core.dot_i = next_dot_i;
                        Core.dot_e = Core.rec_len;
                    }
                } else {
                    Core.dot_e = Core.rec_len;
                }
            }
        /* does not render the UI if nothing changed or when a recording is
         * playing which means: no active user input
         */
        } while ((r & UPDATE_UI) == 0 || is_playback());

        if (!IsRunning) {
            break;
        }

        /* this combines events originating from a single keybind or an entire
         * recording playback if the selected frame has not changed
         */
        if (old_frame == SelFrame) {
            for (size_t i = first_event + 1; i < SelFrame->buf->event_i; i++) {
                SelFrame->buf->events[i - 1]->flags |= IS_TRANSIENT;
            }
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
