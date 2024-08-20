#include "frame.h"
#include "purec.h"
#include "util.h"
#include "xalloc.h"

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
    struct frame *f, *l_f, *r_f, *t_f, *b_f;
    int border, expand;

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
        Core.is_stopped = true;
    } else {
        /* find frames that can expand into this frame */
        l_f = NULL;
        r_f = NULL;
        t_f = NULL;
        b_f = NULL;
        for (f = FirstFrame; f != NULL; f = f->next) {
            if (f->y >= frame->y && f->y + f->h <= frame->y + frame->h) {
                if (f->x + f->w == frame->x) {
                    l_f = f;
                } else if (frame->x + frame->w == f->x) {
                    r_f = f;
                }
            }

            if (f->x >= frame->x && f->x + f->w <= frame->x + frame->w) {
                if (f->y + f->h == frame->y) {
                    t_f = f;
                } else if (frame->y + frame->h == f->y) {
                    b_f = f;
                }
            }
        }

        border = 0;
        if (t_f != NULL || b_f != NULL) {
            if (t_f != NULL && b_f != NULL) {
               border = frame->y + frame->h / 2;
            } else if (t_f != NULL) {
               border = frame->y + frame->h;
            } else {
               border = frame->y;
            }
            expand = 1;
        } else if (l_f != NULL || r_f != NULL) {
            if (l_f != NULL && r_f != NULL) {
               border = frame->x + frame->w / 2;
            } else if (l_f != NULL) {
               border = frame->x + frame->w;
            } else {
               border = frame->x;
            }
            expand = 0;
        } else {
            expand = -1;
        }

        for (f = FirstFrame; f != NULL; f = f->next) {
            if (expand == 0 && f->y >= frame->y &&
                    f->y + f->h <= frame->y + frame->h) {
                if (f->x + f->w == frame->x) {
                    f->w += border - frame->x;
                } else if (frame->x + frame->w == f->x) {
                    f->x = border;
                    f->w += frame->x + frame->w - border;
                }
            }

            if (expand == 1 && f->x >= frame->x &&
                    f->x + f->w <= frame->x + frame->w) {
                if (f->y + f->h == frame->y) {
                    f->h += border - frame->y;
                } else if (frame->y + frame->h == f->y) {
                    f->y = border;
                    f->h += frame->y + frame->h - border;
                }
            }
        }

        if (frame == SelFrame) {
            /* focus frame that is now at the same position */
            SelFrame = frame_at(frame->x, frame->y);
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

void update_screen_size(void)
{
    int x, y;
    int cols, lines;
    struct frame *frame;

    getmaxyx(stdscr, lines, cols);
    if (cols > Core.prev_cols) {
        x = Core.prev_cols - 1;
        y = 0;
        while (y < Core.prev_lines - 1) {
            frame = frame_at(x, y);
            frame->w += cols - Core.prev_cols;
            y += frame->h;
        }
    }
    if (lines > Core.prev_lines) {
        x = 0;
        y = Core.prev_lines - 2;
        while (x < Core.prev_cols) {
            frame = frame_at(x, y);
            frame->h += lines - Core.prev_lines;
            x += frame->w;
        }
    } else if (lines < Core.prev_lines) {

    }
    Core.prev_cols = cols;
    Core.prev_lines = lines;
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
    int r = 0;
    int x, y, w, h;

    get_text_rect(frame, &x, &y, &w, &h);

    if (frame->cur.col < frame->scroll.col) {
        frame->scroll.col = frame->cur.col;
        if ((size_t) MIN(w / 3, 25) >= frame->scroll.col) {
            frame->scroll.col = 0;
        } else {
            frame->scroll.col -= MIN(w / 3, 25);
        }
        r |= 1;
    } else if (frame->cur.col >= frame->scroll.col + w) {
        frame->scroll.col = frame->cur.col - w + MIN(w / 3, 25);
        r |= 1;
    }

    if (frame->cur.line < frame->scroll.line) {
        frame->scroll.line = frame->cur.line;
        if ((size_t) (h / 3) >= frame->scroll.line) {
            frame->scroll.line = 0;
        } else {
            frame->scroll.line -= h / 3;
        }
        r |= 1;
    } else if (frame->cur.line >= frame->scroll.line + h) {
        frame->scroll.line = frame->cur.line - 2 * h / 3;
        frame->scroll.line = MIN(frame->scroll.line, frame->buf->num_lines - h);
        r |= 1;
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

int get_char_state(char ch)
{
    if (isblank(ch)) {
        return 2;
    }
    if (isalnum(ch) || ch == '_') {
        return 1;
    }
    return 0;
}

int do_motion(struct frame *frame, int motion)
{
    size_t new_line;
    struct pos pos;
    struct line *line;
    int c;
    int s, o_s;

    switch (motion) {
    case MOTION_LEFT:
        return move_horz(frame, Core.counter, -1);

    case MOTION_RIGHT:
        return move_horz(frame, Core.counter, 1);

    case MOTION_DOWN:
        return move_vert(frame, Core.counter, 1);

    case MOTION_UP:
        return move_vert(frame, Core.counter, -1);

    case MOTION_HOME:
        return move_horz(frame, SIZE_MAX, -1);

    case MOTION_END:
        return move_horz(frame, SIZE_MAX, 1);

    case MOTION_PREV:
        return move_dir(frame, Core.counter, -1);

    case MOTION_NEXT:
        return move_dir(frame, Core.counter, 1);

    case MOTION_BEG_FRAME:
        if (frame->cur.line == frame->scroll.line) {
            return move_vert(frame, 1, -1);
        }
        return set_vert(frame, frame->scroll.line);

    case MOTION_MIDDLE_FRAME:
        return set_vert(frame, frame->scroll.line + frame->h / 2);

    case MOTION_END_FRAME:
        if (frame->cur.line == frame->scroll.line + frame->h - 2) {
            return move_vert(frame, 1, 1);
        }
        return set_vert(frame, frame->scroll.line + frame->h - 2);

    case MOTION_HOME_SP:
        return move_horz(frame, frame->cur.col -
                get_line_indent(frame->buf, frame->cur.line), -1);

    case MOTION_FILE_BEG:
        return set_vert(frame, Core.counter - 1);

    case MOTION_FILE_END:
        new_line = Core.counter;
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
                if (Core.counter > 1) {
                    Core.counter--;
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
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                return move_vert(frame, i - frame->cur.line, 1);
            }
        }
        return move_vert(frame,
                frame->buf->num_lines - 1 - frame->cur.line, 1);

    case MOTION_FIND_NEXT:
    case MOTION_FIND_EXCL_NEXT:
        c = get_ch();
        pos = frame->cur;
        line = &frame->buf->lines[pos.line];
        while (pos.col++, pos.col < line->n) {
            if (line->s[pos.col] == c) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                if (motion == MOTION_FIND_EXCL_NEXT) {
                    pos.col--;
                }
                return move_horz(frame, pos.col - frame->cur.col, 1);
            }
        }
        break;

    case MOTION_FIND_PREV:
    case MOTION_FIND_EXCL_PREV:
        c = get_ch();
        pos = frame->cur;
        line = &frame->buf->lines[pos.line];
        while (pos.col > 0) {
            if (line->s[--pos.col] == c) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                if (motion == MOTION_FIND_EXCL_PREV) {
                    pos.col++;
                }
                return move_horz(frame, frame->cur.col - pos.col, -1);
            }
        }
        break;

    case MOTION_NEXT_WORD:
        pos = frame->cur;
        line = &frame->buf->lines[pos.line];

    again_next:
        s = -1;
        while (pos.col < line->n) {
            o_s = get_char_state(line->s[pos.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            pos.col++;
            s = o_s;
        }

        if (pos.col == line->n) {
            if (pos.line + 1 < frame->buf->num_lines) {
                pos.line++;
                pos.col = 0;
                line++;
            }
        }

        for (; pos.col < line->n; pos.col++) {
            if (!isblank(line->s[pos.col])) {
                break;
            }
        }

        if (is_point_equal(&frame->cur, &pos)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_next;
        }

        set_cursor(frame, &pos);
        return 1;

    case MOTION_END_WORD:
        pos = frame->cur;
        line = &frame->buf->lines[pos.line];

    again_end:
        pos.col++;
        s = -1;
        while (1) {
            for (; pos.col < line->n; pos.col++) {
                if (!isblank(line->s[pos.col])) {
                    break;
                }
            }

            if (pos.col < line->n) {
                break;
            }

            if (pos.line + 1 >= frame->buf->num_lines) {
                break;
            }

            pos.line++;
            pos.col = 0;
            line++;
        }

        while (pos.col < line->n) {
            o_s = get_char_state(line->s[pos.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            s = o_s;
            pos.col++;
        }

        if (pos.col > 0) {
            pos.col--;
        }

        if (is_point_equal(&frame->cur, &pos)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_end;
        }

        set_cursor(frame, &pos);
        return 1;

    case MOTION_PREV_WORD:
        pos = frame->cur;
        line = &frame->buf->lines[pos.line];

    again_prev:
        for (; pos.col > 0; pos.col--) {
            if (!isblank(line->s[pos.col - 1])) {
                break;
            }
        }

        if (pos.col == 0) {
            if (pos.line > 0) {
                pos.line--;
                pos.col = frame->buf->lines[pos.line].n;
                line--;
            }
        }

        for (; pos.col > 0; pos.col--) {
            if (!isblank(line->s[pos.col - 1])) {
                break;
            }
        }

        s = -1;
        while (pos.col > 0) {
            o_s = get_char_state(line->s[pos.col - 1]);
            if (s >= 0 && s != o_s) {
                break;
            }
            pos.col--;
            s = o_s;
        }

        if (is_point_equal(&frame->cur, &pos)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_prev;
        }

        set_cursor(frame, &pos);
        return 1;
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

    frame->vct = frame->cur.col;

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
    return ((old_col != frame->cur.col) | adjust_scroll(frame));
}

int set_horz(struct frame *frame, size_t col)
{
    if (col < frame->cur.col) {
        return move_vert(frame, frame->cur.col - col, -1);
    }
    return move_vert(frame, col - frame->cur.col, 1);
}
