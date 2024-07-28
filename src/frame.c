#include "frame.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

struct frame *SelFrame;

int move_dir(struct frame *frame, size_t dist, int dir)
{
    struct buf *buf;
    size_t old_dist;
    size_t end, right;

    buf = frame->buf;
    old_dist = dist;
    if (dir < 0) {
        while (dist > 0) {
            if (frame->cur.col == 0) {
                if (frame->cur.line == 0) {
                    break;
                }
                dist--;
                frame->cur.line--;
                frame->cur.col =
                    get_mode_line_end(&buf->lines[frame->cur.line]);
                continue;
            }
            if (dist >= frame->cur.col) {
                dist -= frame->cur.col;
                frame->cur.col = 0;
            } else {
                frame->cur.col -= dist;
                dist = 0;
            }
        }
    } else {

        while (dist > 0) {
            end = get_mode_line_end(&buf->lines[frame->cur.line]);
            right = end - frame->cur.col;
            if (right == 0) {
                if (frame->cur.line + 1 == buf->num_lines) {
                    break;
                }
                dist--;
                frame->cur.line++;
                frame->cur.col = 0;
                continue;
            }
            if (dist >= right) {
                frame->cur.col = end;
                dist -= right;
            } else {
                frame->cur.col += dist;
                dist = 0;
            }
        }
    }

    if (old_dist != dist) {
        return 1;
    }
    return 0;
}

void adjust_cursor(struct frame *frame)
{
    struct buf *buf;
    struct line *line;

    buf = frame->buf;
    if (frame->cur.line >= buf->num_lines) {
        frame->cur.line = buf->num_lines - 1;
    }
    line = &buf->lines[frame->cur.line];
    frame->cur.col = MIN(frame->vct, get_mode_line_end(line));
}

int do_motion(struct frame *frame, int motion)
{
    int r = 0;

    switch (motion) {
    case MOTION_LEFT:
        r = move_horz(frame, correct_counter(Mode.counter), -1);
        break;

    case MOTION_RIGHT:
        r = move_horz(frame, correct_counter(Mode.counter), 1);
        break;

    case MOTION_DOWN:
        r = move_vert(frame, correct_counter(Mode.counter), 1);
        break;

    case MOTION_UP:
        r = move_vert(frame, correct_counter(Mode.counter), -1);
        break;

    case MOTION_HOME:
        r = move_horz(frame, SIZE_MAX, -1);
        break;

    case MOTION_END:
        r = move_horz(frame, SIZE_MAX, 1);
        break;

    case MOTION_PREV:
        r = move_dir(frame, correct_counter(Mode.counter), -1);
        break;

    case MOTION_NEXT:
        r = move_dir(frame, correct_counter(Mode.counter), 1);
        break;

    case MOTION_HOME_SP:
        r = move_horz(frame, frame->cur.col -
                get_line_indent(frame->buf, frame->cur.line), -1);
        break;

    case MOTION_FILE_BEG:
        r = move_vert(frame, SIZE_MAX, -1);
        break;

    case MOTION_FILE_END:
        r = move_vert(frame, SIZE_MAX, 1);
        break;

    case MOTION_PAGE_UP:
        r = move_vert(frame, frame->h * 2 / 3, -1);
        break;

    case MOTION_PAGE_DOWN:
        r = move_vert(frame, frame->h * 2 / 3, 1);
        break;
    }
    return r;
}

int move_vert(struct frame *frame, size_t dist, int dir)
{
    struct buf *buf;
    size_t old_line;

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
        adjust_cursor(frame);
        return 1;
    }
    return 0;
}

int move_horz(struct frame *frame, size_t dist, int dir)
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
            frame->cur.col = get_mode_line_end(line);
        } else {
            frame->cur.col = MIN(dist + frame->cur.col,
                    get_mode_line_end(line));
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
