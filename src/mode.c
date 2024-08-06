#include "mode.h"
#include "frame.h"
#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

bool IsRunning;

int ExitCode;

char *Message;

void format_message(const char *fmt, ...)
{
    va_list l;

    free(Message);
    va_start(l, fmt);
    vasprintf(&Message, fmt, l);
    va_end(l);
}

struct mode Mode;

void shift_add_counter(int d)
{
    size_t new_counter;

    /* add a digit to the counter */
    new_counter = Mode.counter * 10 + d;
    /* check for overflow */
    if (new_counter < Mode.counter) {
        new_counter = SIZE_MAX;
    }
    Mode.counter = new_counter;
}

int getch_digit(void)
{
    int c;

    while (c = getch(), (c == '0' && Mode.counter != 0) ||
            (c >= '1' && c <= '9')) {
        shift_add_counter(c - '0');
    }
    return c;
}

size_t get_mode_line_end(struct line *line)
{
    if (Mode.type != NORMAL_MODE) {
        return line->n;
    }
    /* adjustment for normal mode */
    return line->n == 0 ? 0 : line->n - 1;
}

size_t correct_counter(size_t counter)
{
    return counter == 0 ? 1 : counter;
}

void set_mode(int mode)
{
    const char *format_messages[] = {
        [NORMAL_MODE] = "",

        [INSERT_MODE] = "-- INSERT --",

        [VISUAL_MODE] = "-- VISUAL --",
        [VISUAL_LINE_MODE] = "-- VISUAL LINE --",
        [VISUAL_BLOCK_MODE] = "-- VISUAL BLOCK --",
    };

    const char cursors[] = {
        [NORMAL_MODE] = '\x30',

        [INSERT_MODE] = '\x35',

        [VISUAL_MODE] = '\x30',
        [VISUAL_LINE_MODE] = '\x30',
        [VISUAL_BLOCK_MODE] = '\x30',
    };

    if (format_messages[mode] == NULL) {
        endwin();
        fprintf(stderr, "set_mode - invalid mode: %d", mode);
        abort();
    }

    printf("\x1b[%c q", cursors[mode]);
    fflush(stdout);

    if (IS_VISUAL(mode) && !IS_VISUAL(Mode.type)) {
        Mode.pos = SelFrame->cur;
    }

    format_message(format_messages[mode]);

    if (mode == NORMAL_MODE) {
        clip_column(SelFrame);
    } else if (mode == INSERT_MODE) {
        Mode.ev_from_ins = SelFrame->buf->event_i;
    }

    Mode.type = mode;
}

bool get_selection(struct selection *sel)
{
    if (Mode.type == VISUAL_BLOCK_MODE) {
        sel->is_block = true;
        sel->beg.line = MIN(SelFrame->cur.line, Mode.pos.line);
        sel->beg.col = MIN(SelFrame->cur.col, Mode.pos.col);
        sel->end.line = MAX(SelFrame->cur.line, Mode.pos.line);
        sel->end.col = MAX(SelFrame->cur.col, Mode.pos.col);
    } else {
        sel->is_block = false;
        sel->beg = SelFrame->cur;
        sel->end = Mode.pos;
        sort_positions(&sel->beg, &sel->end);
        if (Mode.type == VISUAL_LINE_MODE) {
            sel->beg.col = 0;
            sel->end.col = 0;
            sel->end.line++;
        }
    }
    return IS_VISUAL(Mode.type);
}

bool is_in_selection(const struct selection *sel, const struct pos *pos)
{
    if (sel->is_block) {
        return is_in_block(pos, &sel->beg, &sel->end);
    }
    return is_in_range(pos, &sel->beg, &sel->end);
}
