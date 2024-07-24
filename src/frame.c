#include "frame.h"
#include "macros.h"

#include <ncurses.h>

#include <stdint.h>
#include <stdio.h>

static void shift_add_counter(int d)
{
    size_t new_counter;

    new_counter = Normal.counter * 10 + d;
    if (new_counter < Normal.counter) {
        new_counter = SIZE_MAX;
    }
    Normal.counter = new_counter;
}

static size_t get_line_mode_end(struct line *line)
{
    if (Mode == INSERT_MODE) {
        return line->n;
    }
    return line->n == 0 ? 0 : line->n - 1;
}

/**
 * Moves the cursor in vertical direction and returns true if there was
 * movement.
 */
static int move_vert(struct frame *frame, size_t dist, int dir)
{
    struct buf *buf;
    size_t old_line;
    struct line *line;

    buf = frame->buf;
    old_line = frame->cur.line;
    if (dir < 0) {
        if (dist > frame->cur.line) {
            frame->cur.line = 0;
        } else {
            frame->cur.line -= dist;
        }
    } else {
        /* check for overflow */
        if (SIZE_MAX - dist < frame->cur.line) {
            frame->cur.line = buf->num_lines - 1;
        } else {
            frame->cur.line = MIN(dist + frame->cur.line, buf->num_lines - 1);
        }
    }
    if (old_line != frame->cur.line) {
        line = &buf->lines[frame->cur.line];
        frame->cur.col = MIN(frame->vct, get_line_mode_end(line));
        return 1;
    }
    return 0;
}

/**
 * Moves the cursor in horizontal direction and returns true if there was
 * movement.
 */
static int move_horz(struct frame *frame, size_t dist, int dir)
{
    struct buf *buf;
    size_t old_col;
    struct line *line;

    buf = frame->buf;
    old_col = frame->cur.col;
    line = &buf->lines[frame->cur.line];
    if (dir < 0) {
        if (dist > frame->cur.col) {
            frame->cur.col = 0;
        } else {
            frame->cur.col -= dist;
        }
        frame->vct = frame->cur.col;
    } else {
        /* check for overflow */
        if (SIZE_MAX - dist < frame->cur.col) {
            frame->cur.col = get_line_mode_end(line);
        } else {
            frame->cur.col = MIN(dist + frame->cur.col,
                    get_line_mode_end(line));
            frame->vct = frame->cur.col;
        }
    }
    if (dist == SIZE_MAX && dir == 1) {
        frame->vct = SIZE_MAX;
    } else {
        frame->vct = frame->cur.col;
    }
    if (old_col != frame->cur.col) {
        return 1;
    }
    return 0;
}

int handle_input(struct frame *frame, int c)
{
    int r = 0;

    switch (c) {
    case '\x1b':
        move_horz(frame, 1, -1);
        set_mode(NORMAL_MODE);
        r = 1;
        break;

    case 'I':
        set_mode(INSERT_MODE);
        /* fall through */
    case KEY_HOME:
    case '0':
        if (Normal.counter != 0) {
            shift_add_counter(0);
            return 1;
        }
        r = move_horz(frame, SIZE_MAX, -1);
        break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        shift_add_counter(c - '0');
        return 1;

    case 'A':
        set_mode(INSERT_MODE);
        /* fall through */
    case KEY_END:
    case '$':
        r = move_horz(frame, SIZE_MAX, 1);
        break;

    case KEY_LEFT:
    case 'h':
        r = move_horz(frame, Normal.counter == 0 ? 1 : Normal.counter, -1);
        break;

    case KEY_DOWN:
    case 'j':
        r = move_vert(frame, Normal.counter == 0 ? 1 : Normal.counter, 1);
        break;

    case KEY_UP:
    case 'k':
        r = move_vert(frame, Normal.counter == 0 ? 1 : Normal.counter, -1);
        break;

    case 'a':
        set_mode(INSERT_MODE);
        /* fall through */
    case KEY_RIGHT:
    case 'l':
        r = move_horz(frame, Normal.counter == 0 ? 1 : Normal.counter, 1);
        break;

    case 'i':
        set_mode(INSERT_MODE);
        break;
    }
    Normal.counter = 0;
    return r;
}
