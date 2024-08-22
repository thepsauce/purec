#include "color.h"
#include "input.h"
#include "frame.h"
#include "purec.h"
#include "xalloc.h"

#include <locale.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

WINDOW *OffScreen;

bool IsRunning;

int ExitCode;

struct core Core;

void set_message(const char *msg, ...)
{
    va_list l;

    wclear(Core.msg_win);
    set_highlight(Core.msg_win, HI_CMD);
    va_start(l, msg);
    vw_printw(Core.msg_win, msg, l);
    va_end(l);
    Core.msg_state = MSG_OTHER;
}

void set_error(const char *err, ...)
{
    va_list l;

    wclear(Core.msg_win);
    set_highlight(Core.msg_win, HI_ERROR);
    va_start(l, err);
    vw_printw(Core.msg_win, err, l);
    va_end(l);
    Core.msg_state = MSG_OTHER;
}

void yank_data(size_t data_i, int flags)
{
    struct raw_line *lines;
    size_t num_lines;
    struct reg *reg;

    if (Core.user_reg == '+' || Core.user_reg == '*') {
        lines = load_undo_data(data_i, &num_lines);
        copy_clipboard(lines, num_lines, Core.user_reg == '*');
        unload_undo_data(data_i);
    } else {
        reg = &Core.regs[Core.user_reg - '.'];
        reg->flags = flags;
        reg->data_i = data_i;
    }
}

void yank_lines(struct raw_line *lines, size_t num_lines, int flags)
{
    struct reg *reg;

    if (Core.user_reg == '+' || Core.user_reg == '*') {
        copy_clipboard(lines, num_lines, Core.user_reg == '*');
        (void) save_lines(lines, num_lines);
    } else {
        reg = &Core.regs[Core.user_reg - '.'];
        reg->flags = flags;
        reg->data_i = save_lines(lines, num_lines);
    }
}

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
        do {
            c = getch();
            if (c == KEY_RESIZE) {
                update_screen_size();
            }
        } while (c == KEY_RESIZE);
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

static int get_first_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    Core.counter = 1;
    Core.user_reg = '.';
    while (c = get_ch(), (c == '0' && counter != 0) || (c >= '1' && c <= '9') ||
            (c == '\"')) {
        if (c == '\"') {
            if (counter != 0) {
                Core.counter = counter;
                counter = 0;
            }
            c = toupper(get_ch());
            if (!IS_REG_CHAR(c)) {
                return -1;
            }
            Core.user_reg = c;
        } else {
            /* add a digit to the counter */
            new_counter = counter * 10 + c - '0';
            /* check for overflow */
            if (new_counter < counter) {
                new_counter = SIZE_MAX;
            }
            counter = new_counter;
        }
    }

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);
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

    Core.msg_state = MSG_TO_DEFAULT;
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

static void set_message_to_default(void)
{
    static const char *mode_strings[] = {
        [NORMAL_MODE] = "",

        [INSERT_MODE] = "-- INSERT --",

        [VISUAL_MODE] = "-- VISUAL --",
        [VISUAL_LINE_MODE] = "-- VISUAL LINE --",
        [VISUAL_BLOCK_MODE] = "-- VISUAL BLOCK --",
    };

    set_highlight(Core.msg_win, HI_CMD);
    wattr_on(Core.msg_win, A_BOLD, NULL);
    mvwaddstr(Core.msg_win, 0, 0, mode_strings[Core.mode]);
    if (Core.mode == INSERT_MODE && Core.counter > 1) {
        wprintw(Core.msg_win, " REPEAT * %zu", Core.counter);
    }
    wattr_off(Core.msg_win, A_BOLD, NULL);

    if (Core.user_rec_ch != '\0') {
        if (Core.mode != NORMAL_MODE) {
            waddch(Core.msg_win, ' ');
        }
        waddstr(Core.msg_win, "recording @");
        waddch(Core.msg_win, Core.user_rec_ch);
    }
    wclrtoeol(Core.msg_win);
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

   static int (*const input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
        [VISUAL_MODE] = visual_handle_input,
        [VISUAL_BLOCK_MODE] = visual_handle_input,
        [VISUAL_LINE_MODE] = visual_handle_input,
    };

    struct buf *buf;
    struct pos cur;
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
    init_clipboard();

    Core.msg_win = newpad(1, 128);
    OffScreen = newpad(1, 512);

    wbkgdset(stdscr, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(Core.msg_win, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(OffScreen, ' ' | COLOR_PAIR(HI_NORMAL));

    getmaxyx(stdscr, Core.prev_lines, Core.prev_cols);

    //buf = create_buffer("../VM_jsm/cases.h");
    buf = create_buffer("src/main.c");
    //buf = create_buffer(NULL);

    (void) create_frame(NULL, 0, buf);
    FirstFrame->buf = buf;

    set_mode(NORMAL_MODE);

    while (1) {
        curs_set(0);

        erase();

        for (struct frame *f = FirstFrame; f != NULL; f = f->next) {
            render_frame(f);
        }

        if (Core.msg_state == MSG_TO_DEFAULT) {
            set_message_to_default();
            Core.msg_state = MSG_DEFAULT;
        }
        copywin(Core.msg_win, stdscr, 0, 0, LINES - 1, 0, LINES - 1,
                MIN(COLS - 1, getmaxx(Core.msg_win)), 0);

        get_visual_cursor(SelFrame, &cur);
        if (cur.col >= SelFrame->scroll.col &&
                cur.line >= SelFrame->scroll.line) {
            cur.col -= SelFrame->scroll.col;
            cur.line -= SelFrame->scroll.line;
            if (cur.col < (size_t) SelFrame->w &&
                    cur.line < (size_t) SelFrame->h) {
                cur.col += SelFrame->x;
                cur.line += SelFrame->y;
                move(cur.line, cur.col);
                printf("\x1b[%c q", cursors[Core.mode]);
                fflush(stdout);
                curs_set(1);
            }
        }

        old_frame = SelFrame;
        first_event = SelFrame->buf->event_i;

        do {
            next_dot_i = Core.rec_len;
            if (Core.mode == INSERT_MODE) {
                c = get_ch();
            } else {
                Core.move_down_count = 0;
                do {
                    c = get_first_char();
                } while (c == -1);
            }

            old_mode = Core.mode;
            r = input_handlers[Core.mode](c);
            if (!is_playback() && Core.play_i == SIZE_MAX) {
                if (old_mode == NORMAL_MODE) {
                    if ((r & DO_RECORD)) {
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

        if (Core.is_stopped) {
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
    return Core.exit_code;
}
