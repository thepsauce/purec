#include "frame.h"
#include "xalloc.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

struct frame *FirstFrame;
struct frame *SelFrame;

struct frame *create_frame(struct frame *split, int dir, struct buf *buf)
{
    struct frame *frame, *f;
    int w, h;

    frame = xcalloc(1, sizeof(*frame));
    if (buf == NULL) {
        frame->buf = create_buffer(NULL);
    } else {
        frame->buf = buf;
    }

    /* TODO: CHECK for too small */
    switch (dir) {
    case 0:
        frame->x = 0;
        frame->y = 0;
        frame->w = COLS;
        frame->h = LINES - 1; /* minus command line */
        break;

    case SPLIT_LEFT:
        w = split->w / 2;
        frame->x = split->x;
        frame->w = w;
        frame->y = split->y;
        frame->h = split->h;
        split->w -= w;
        split->x += w;
        break;

    case SPLIT_RIGHT:
        w = split->w;
        split->w /= 2;
        frame->x = split->x + split->w;
        frame->w = w - split->w;
        frame->y = split->y;
        frame->h = split->h;
        break;

    case SPLIT_UP:
        h = split->h / 2;
        frame->x = split->x;
        frame->w = split->w;
        frame->y = split->y;
        frame->h = h;
        split->h -= h;
        split->y += h;
        break;

    case SPLIT_DOWN:
        h = split->h;
        split->h /= 2;
        frame->x = split->x;
        frame->w = split->w;
        frame->y = split->y + split->h;
        frame->h = h - split->h;
        break;

    default:
        endwin();
        abort();
    }

    /* add frame to the linked list */
    if (FirstFrame == NULL) {
        frame->next_focus = frame;
        FirstFrame = frame;
        SelFrame = frame;
    } else {
        for (f = FirstFrame; f->next != NULL; ) {
            f = f->next;
        }
        f->next = frame;
    }

    return frame;
}

void destroy_frame(struct frame *frame)
{
    struct frame *f;
    struct frame *prev_focus;

    /* remove from focus chain if the frame is part of one */
    if (frame->next_focus != NULL) {
        for (f = FirstFrame; f->next_focus != frame; ) {
            f = f->next_focus;
        }
        if (f != frame) {
            prev_focus = f;
            if (frame->next_focus == NULL) {
                format_message("NOOOOOOOOO");
            }
            f->next_focus = frame->next_focus;
        } else {
            prev_focus = NULL;
        }
    } else {
        prev_focus = NULL;
    }

    /* remove from linked list */
    if (frame == FirstFrame) {
        FirstFrame = frame->next;
    } else {
        for (f = FirstFrame; f->next != frame; ) {
            f = f->next;
        }
        f->next = frame->next;
    }

    if (FirstFrame == NULL) {
        IsRunning = false;
    } else {
        /* focus last frame if the focused frame is destroyed */
        if (frame == SelFrame) {
            SelFrame = prev_focus == NULL ? FirstFrame : prev_focus;
            if (SelFrame->next_focus == NULL) {
                SelFrame->next_focus = SelFrame;
            }
        }

        /* find frames that can expand into this frame */
        for (f = FirstFrame; f != NULL; f = f->next) {
            if (f->y >= frame->y && f->y + f->h <= frame->y + frame->h) {
                if (f->x + f->w == frame->x) {
                    f->w += frame->w;
                } else if (frame->x + frame->w == f->x) {
                    f->x = frame->x;
                    f->w += frame->w;
                }
            }

            if (f->x >= frame->x && f->x + f->w <= frame->x + frame->w) {
                if (f->y + f->h == frame->y) {
                    f->h += frame->h;
                } else if (frame->y + frame->h == f->y) {
                    f->y = frame->y;
                    f->h += frame->h;
                }
            }
        }
    }

    free(frame);
}

struct frame *frame_at(int x, int y)
{
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (x < frame->x || y < frame->y || x >= frame->x + frame->w ||
                y >= frame->y + frame->h) {
            continue;
        }
        return frame;
    }
    return NULL;
}

void set_frame_buffer(struct frame *frame, struct buf *buf)
{
    frame->buf->save_cur = frame->cur;
    frame->buf->save_scroll = frame->scroll;

    frame->buf = buf;

    frame->cur = buf->save_cur;
    frame->scroll = buf->save_scroll;
    frame->vct = frame->cur.col;
}

struct frame *focus_frame(struct frame *frame)
{
    struct frame *prev;
    struct frame *old_focus;

    if (frame->next_focus != NULL) {
        /* unlink it from the focus list */
        for (prev = FirstFrame; prev->next_focus != frame; ) {
            prev = prev->next_focus;
        }
        prev->next_focus = frame->next_focus;
    }
    /* relink it into the focus list */
    frame->next_focus = SelFrame->next_focus;
    SelFrame->next_focus = frame;

    old_focus = SelFrame;
    SelFrame = frame;
    return old_focus;
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

int adjust_scroll(struct frame *frame)
{
    if (frame->cur.line < frame->scroll.line) {
        frame->scroll.line = frame->cur.line;
        return 1;
    }
    /* note: minus status bar */
    if (frame->cur.line >= frame->scroll.line + frame->h - 1) {
        frame->scroll.line = frame->cur.line - frame->h + 2;
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

    /* set vct */
    frame->vct = new_cur.col;

    frame->cur = new_cur;

    /* adjust scrolling */
    (void) adjust_scroll(frame);
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
        for (size_t i = frame->cur.line; i > 0; ) {
            i--;
            if (frame->buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                return move_vert(frame, frame->cur.line - i, -1);
            }
        }
        return move_vert(frame, frame->cur.line, -1);

    case MOTION_PARA_DOWN:
        for (size_t i = frame->cur.line + 1; i < frame->buf->num_lines;
                i++) {
            if (frame->buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                return move_vert(frame, i - frame->cur.line, 1);
            }
        }
        return move_vert(frame,
                frame->buf->num_lines - 1 - frame->cur.line, 1);
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
