#include "frame.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

struct frame *SelFrame;

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

int adjust_scroll(struct frame *frame)
{
    if (frame->cur.line < frame->scroll.line) {
        frame->scroll.line = frame->cur.line;
        return 1;
    }
    if (frame->cur.line >= frame->scroll.line + frame->h) {
        frame->scroll.line = frame->cur.line - frame->h + 1;
        return 1;
    }
    return 0;
}

void clip_column(struct frame *frame)
{
    frame->cur.col = MIN(frame->cur.col, get_mode_line_end(
                &frame->buf->lines[frame->cur.line]));
}

void set_cursor(struct frame *frame, const struct pos *pos)
{
    struct line *line;
    struct pos new_cur;

    new_cur = *pos;

    /* clip line */
    if (new_cur.line >= frame->buf->num_lines) {
        new_cur.line = frame->buf->num_lines - 1;
    }

    /* clip column */
    line = &frame->buf->lines[new_cur.line];
    new_cur.col = MIN(new_cur.col, get_mode_line_end(line));

    /* adjust scrolling */
    if (new_cur.line < frame->scroll.line) {
        frame->scroll.line = new_cur.line;
    } else if (new_cur.line >= frame->scroll.line + frame->h) {
        frame->scroll.line = new_cur.line - frame->h + 1;
    }

    /* set vct */
    frame->vct = new_cur.col;

    frame->cur = new_cur;
}

int do_motion(struct frame *frame, int motion)
{
    size_t new_line;

    switch (motion) {
    case MOTION_LEFT:
        return move_horz(frame, correct_counter(Mode.counter), -1);

    case MOTION_RIGHT:
        return move_horz(frame, correct_counter(Mode.counter), 1);

    case MOTION_DOWN:
        return move_vert(frame, correct_counter(Mode.counter), 1);

    case MOTION_UP:
        return move_vert(frame, correct_counter(Mode.counter), -1);

    case MOTION_HOME:
        return move_horz(frame, SIZE_MAX, -1);

    case MOTION_END:
        return move_horz(frame, SIZE_MAX, 1);

    case MOTION_PREV:
        return move_dir(frame, correct_counter(Mode.counter), -1);

    case MOTION_NEXT:
        return move_dir(frame, correct_counter(Mode.counter), 1);

    case MOTION_HOME_SP:
        return move_horz(frame, frame->cur.col -
                get_line_indent(frame->buf, frame->cur.line), -1);

    case MOTION_FILE_BEG:
        return set_vert(frame, correct_counter(Mode.counter) - 1);

    case MOTION_FILE_END:
        new_line = correct_counter(Mode.counter);
        if (new_line >= frame->buf->num_lines) {
            return move_vert(frame, SIZE_MAX, 1);
        }
        new_line = frame->buf->num_lines - new_line;
        return set_vert(frame, new_line);

    case MOTION_PAGE_UP:
        return move_vert(frame, frame->h * 2 / 3, -1);

    case MOTION_PAGE_DOWN:
        return move_vert(frame, frame->h * 2 / 3, 1);

    case MOTION_PARA_UP:
        for (size_t i = SelFrame->cur.line; i > 0; ) {
            i--;
            if (SelFrame->buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                return move_vert(frame, SelFrame->cur.line - i, -1);
            }
        }
        return move_vert(frame, SelFrame->cur.line, -1);

    case MOTION_PARA_DOWN:
        for (size_t i = SelFrame->cur.line + 1; i < SelFrame->buf->num_lines;
                i++) {
            if (SelFrame->buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                return move_vert(SelFrame, i - SelFrame->cur.line, 1);
            }
        }
        return move_vert(SelFrame,
                SelFrame->buf->num_lines - 1 - SelFrame->cur.line, 1);
    }
    return 0;
}

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

    if (adjust_scroll(frame) || old_dist != dist) {
        return 1;
    }
    return 0;
}

int move_vert(struct frame *frame, size_t dist, int dir)
{
    int r = 0;
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
        r = 1;
    }
    return r | adjust_scroll(frame);
}

int set_vert(struct frame *frame, size_t line)
{
    if (line < frame->cur.line) {
        return move_vert(frame, frame->cur.line - line, -1);
    }
    return move_vert(frame, line - frame->cur.line, 1);
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

int set_horz(struct frame *frame, size_t col)
{
    if (col < frame->cur.col) {
        return move_vert(frame, frame->cur.col - col, -1);
    }
    return move_vert(frame, col - frame->cur.col, 1);
}
