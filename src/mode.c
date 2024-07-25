#include "mode.h"
#include "buf.h"

#include <stdio.h>
#include <stdint.h>

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

size_t get_mode_line_end(struct line *line)
{
    if (Mode.type != NORMAL_MODE) {
        return line->n;
    }
    /* adjustment for normal mode */
    return line->n == 0 ? 0 : line->n - 1;
}

void set_mode(int mode)
{
    Mode.type = mode;
    Mode.extra = 0;
    switch (mode) {
    case NORMAL_MODE:
    case VISUAL_MODE:
        /* steady block */
        printf("\x1b[\x30 q");
        break;
    case INSERT_MODE:
        /* vertical bar cursor (blinking) */
        printf("\x1b[\x35 q");
        break;
    default:
        printf("set_mode - invalid mode: %d", mode);
        abort();
    }
    fflush(stdout);
}
