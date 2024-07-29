#include "mode.h"
#include "buf.h"

#include <stdio.h>
#include <stdint.h>

#include <ncurses.h>

void sort_positions(struct pos *p1, struct pos *p2)
{
    struct pos tmp;

    if (p1->line > p2->line) {
        tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
    } else if (p1->line == p2->line) {
        if (p1->col > p2->col) {
            tmp.col = p1->col;
            p1->col = p2->col;
            p2->col = tmp.col;
        }
    }
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
