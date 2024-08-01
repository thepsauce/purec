#include "mode.h"
#include "frame.h"
#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

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
    Mode.type = mode;
    switch (mode) {
    case NORMAL_MODE:
        /* steady block */
        printf("\x1b[\x30 q");
        break;

    case VISUAL_MODE:
        /* steady block */
        printf("\x1b[\x30 q");
        format_message("-- VISUAL --");
        Mode.pos = SelFrame->cur;
        break;

    case INSERT_MODE:
        /* vertical bar cursor (blinking) */
        printf("\x1b[\x35 q");
        format_message("-- INSERT --");
        break;

    default:
        printf("set_mode - invalid mode: %d", mode);
        abort();
    }
    fflush(stdout);
}
