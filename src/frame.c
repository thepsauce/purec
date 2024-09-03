#include "frame.h"
#include "purec.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

struct frame *FirstFrame;
struct frame *SelFrame;

struct frame *create_frame(struct frame *split, int dir, struct buf *buf)
{
    struct frame *frame, *f;
    int w, h;

    if (dir == SPLIT_LEFT || dir == SPLIT_RIGHT) {
        if (split->w == 1) {
            set_error("Not enough room");
            return NULL;
        }
    } else if (dir == SPLIT_UP || dir == SPLIT_DOWN) {
        if (split->h == 1) {
            set_error("Not enough room");
            return NULL;
        }
    }

    frame = xcalloc(1, sizeof(*frame));
    if (buf == NULL) {
        frame->buf = create_buffer(NULL);
    } else {
        frame->buf = buf;
    }

    if (dir == SPLIT_LEFT || dir == SPLIT_RIGHT) {
        split->split_dir = 0;
        frame->split_dir = 0;
    } else if (dir == SPLIT_UP || dir == SPLIT_DOWN) {
        split->split_dir = 1;
        frame->split_dir = 1;
    }

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

/**
 * Fills a gap by expanding frames into it.
 *
 * @param x         The x position of the gap.
 * @param y         The y position of the gap.
 * @param w         The width of the gap.
 * @param h         The height of the gap.
 * @param pref_vert 1 if it preferred to expand frames vertically, 0 otherwise.
 */
static void fill_gap(int x, int y, int w, int h, int pref_vert)
{
    struct frame *frame;
    struct frame *l_f, *r_f, *t_f, *b_f;
    int border;
    int expand;

    /* find frames that can expand into this frame */
    l_f = NULL;
    r_f = NULL;
    t_f = NULL;
    b_f = NULL;
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (frame->y >= y && frame->y + frame->h <= y + h) {
            if (frame->x + frame->w == x) {
                l_f = frame;
            } else if (x + w == frame->x) {
                r_f = frame;
            }
        }

        if (frame->x >= x && frame->x + frame->w <= x + w) {
            if (frame->y + frame->h == y) {
                t_f = frame;
            } else if (y + h == frame->y) {
                b_f = frame;
            }
        }
    }

    if (l_f == NULL && r_f == NULL && t_f == NULL && b_f == NULL) {
        return;
    }

    border = 0;
    /* take preference into account */
    if (pref_vert == 0 && (r_f != NULL || l_f != NULL)) {
        t_f = NULL;
        b_f = NULL;
    } else if (pref_vert == 1 && (t_f != NULL || b_f != NULL)) {
        l_f = NULL;
        r_f = NULL;
    }

    if (l_f != NULL || r_f != NULL) {
        if (l_f != NULL && r_f != NULL) {
           border = x + w / 2;
        } else if (l_f != NULL) {
           border = x + w;
        } else {
           border = x;
        }
        expand = 0;
    } else if (t_f != NULL || b_f != NULL) {
        if (t_f != NULL && b_f != NULL) {
           border = y + h / 2;
        } else if (t_f != NULL) {
           border = y + h;
        } else {
           border = y;
        }
        expand = 1;
    }

    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (expand == 0 && frame->y >= y &&
                frame->y + frame->h <= y + h) {
            if (frame->x + frame->w == x) {
                frame->w += border - x;
            } else if (x + w == frame->x) {
                frame->x = border;
                frame->w += x + w - border;
            }
        } else if (expand == 1 && frame->x >= x &&
                frame->x + frame->w <= x + w) {
            if (frame->y + frame->h == y) {
                frame->h += border - y;
            } else if (y + h == frame->y) {
                frame->y = border;
                frame->h += y + h - border;
            }
        }
    }
}

void destroy_frame(struct frame *frame)
{
    struct frame *f;

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
        fill_gap(frame->x, frame->y, frame->w, frame->h, frame->split_dir);

        if (frame == SelFrame) {
            /* focus frame that is now at the same position */
            SelFrame = get_frame_at(frame->x, frame->y);
            if (SelFrame->buf == frame->buf) {
                /* clip the cursor */
                set_cursor(SelFrame, &SelFrame->cur);
            }
        }
    }

    free(frame);
}

size_t get_frame_count(void)
{
    struct frame *frame;
    size_t count;

    count = 0;
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        count++;
    }
    return count;
}

struct frame *get_frame_at(int x, int y)
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

void set_only_frame(struct frame *frame)
{
    struct frame *f, *n_f;

    for (f = FirstFrame; f != NULL; f = n_f) {
        n_f = f->next;
        if (f == frame) {
            continue;
        }
        free(f);
    }
    FirstFrame = frame;
    SelFrame = frame;
    frame->x = 0;
    frame->y = 0;
    frame->w = COLS;
    frame->h = LINES - 1;
    frame->next = NULL;
    /* clip the cursor */
    set_cursor(frame, &frame->cur);
}

bool is_frame_in(int x, int y, int w, int h)
{
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (x < frame->x + frame->w && frame->x < x + w &&
                y < frame->y + frame->h && frame->y < y + h) {
            return true;
        }
    }
    return false;
}

void update_screen_size(void)
{
    struct frame *frame;

    /* expand frames that have no frame on the right */
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (is_frame_in(frame->x + frame->w, frame->y, INT_MAX / 2, frame->h)) {
            continue;
        }
        if (frame->x + 1 < COLS) {
            frame->w = COLS - frame->x;
        }
    }

    /* expand frame at the bottom edge */
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (is_frame_in(frame->x, frame->y + frame->h, frame->w, INT_MAX / 2)) {
            continue;
        }
        if (frame->y + 2 < LINES) {
            frame->h = LINES - 1 - frame->y;
        }
    }
}

/**
 * Counts how many frames are in a rectangular region.
 *
 * @param x The x position.
 * @param y The y position.
 * @param w The width.
 * @param h The height.
 *
 * @return The number of frames.
 */
static size_t count_frames(int x, int y, int w, int h)
{
    size_t c = 0;

    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (x < frame->x + frame->w && frame->x < x + w &&
                y < frame->y + frame->h && frame->y < y + h) {
            c++;
        }
    }
    return c;
}

/**
 * Returns all frames within a rectangular region.
 *
 * @param x         The x position.
 * @param y         The y position.
 * @param w         The width.
 * @param h         The height.
 * @param p_frames  The destination of the frames (must be big enough).
 */
static void get_frames(int x, int y, int w, int h, struct frame **p_frames)
{
    size_t i = 0;

    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (x < frame->x + frame->w && frame->x < x + w &&
                y < frame->y + frame->h && frame->y < y + h) {
            p_frames[i++] = frame;
        }
    }
}

static int push_left(struct frame *frame, int amount)
{
    int a;

    a = MIN(amount, frame->w - 2);
    amount -= a;
    if (amount != 0) {
        a += move_left_edge(frame, amount);
    }
    frame->w -= a;
    return a;
}

int move_left_edge(struct frame *frame, int amount)
{
    size_t c;
    struct frame **r_frames;
    int a, min_a;

    c = count_frames(frame->x - 1, frame->y, 1, frame->h);
    if (c == 0) {
        return 0;
    }

    r_frames = xreallocarray(NULL, c, sizeof(*r_frames));
    get_frames(frame->x - 1, frame->y, 1, frame->h, r_frames);

    min_a = amount;
    for (size_t i = 0; i < c; i++) {
        a = push_left(r_frames[i], amount);
        min_a = MIN(min_a, a);
    }

    frame->x -= min_a;
    frame->w += min_a;

    free(r_frames);
    return min_a;
}

/**
 * Increases the x position of a frame and recursively resolves overlaps and
 * gaps.
 *
 * @param frame     The frame whose x position to increase.
 * @param amount    The amount to add to the x position.
 *
 * @return The amount x was actually increased by.
 */
static int push_right(struct frame *frame, int amount)
{
    int a;

    a = MIN(amount, frame->w - MIN(frame->w, 2));
    amount -= a;
    if (amount != 0) {
        a += move_right_edge(frame, amount);
    }
    frame->x += a;
    frame->w -= a;
    return a;
}

int move_right_edge(struct frame *frame, int amount)
{
    size_t c;
    struct frame **r_frames;
    int a, min_a;

    c = count_frames(frame->x + frame->w, frame->y, 1, frame->h);
    if (c == 0) {
        return 0;
    }

    r_frames = xreallocarray(NULL, c, sizeof(*r_frames));
    get_frames(frame->x + frame->w, frame->y, 1, frame->h, r_frames);

    min_a = amount;
    for (size_t i = 0; i < c; i++) {
        a = push_right(r_frames[i], amount);
        min_a = MIN(min_a, a);
    }

    frame->w += min_a;

    free(r_frames);
    return min_a;
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
        frame->scroll.col = frame->cur.col - w + MAX(MIN(w / 3, 25), 1);
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
    return r;
}

int scroll_frame(struct frame *frame, size_t dist, int dir)
{
    size_t old_scroll;

    old_scroll = frame->scroll.line;
    if (dir < 0) {
        dist = MIN(frame->scroll.line, dist);
        frame->scroll.line -= dist;
        frame->cur.line -= dist;
    } else {
        frame->scroll.line = safe_add(frame->scroll.line, dist);
        if (frame->scroll.line >= frame->buf->num_lines) {
            frame->scroll.line = frame->buf->num_lines - 1;
        }
        frame->cur.line += frame->scroll.line - old_scroll;
        frame->cur.line = MIN(frame->cur.line, frame->buf->num_lines - 1);
    }
    if (old_scroll != frame->scroll.line) {
        clip_column(frame);
        return 1;
    }
    return 0;
}

void clip_column(struct frame *frame)
{
    size_t end;

    end = get_mode_line_end(&frame->buf->lines[frame->cur.line]);
    frame->cur.col = MIN(frame->cur.col, end);
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

    frame->prev_cur = frame->cur;
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

/**
 * Finds the index of the match that would come before the cursor.
 *
 * @param buf   The buffer containing all matches.
 * @param pos   The current position.
 *
 * @return The index of the match.
 */
static size_t find_current_match(struct buf *buf, struct pos *pos)
{
    size_t l, m, r;
    struct match *match;
    int cmp;

    l = 0;
    r = buf->num_matches;
    while (l < r) {
        m = (l + r) / 2;
        match = &buf->matches[m];
        if (match->from.line == pos->line) {
            cmp = match->from.col < pos->col ? -1 :
                match->from.col > pos->col ? 1 : 0;
        } else if (match->from.line < pos->line) {
            cmp = -1;
        } else {
            cmp = 1;
        }

        if (cmp == 0) {
            l = m + 1;
            break;
        }

        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    return l == 0 ? buf->num_matches - 1 : l - 1;
}

int do_motion(struct frame *frame, int motion)
{
    size_t new_line;
    struct pos pos;
    struct line *line;
    int c;
    int s, o_s;
    size_t index;

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
        if (motion == MOTION_FIND_EXCL_NEXT) {
            /* make sure when the cursor is right before a match already, that
             * it is not taken again
             */
            pos.col++;
        }
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
        if (motion == MOTION_FIND_EXCL_PREV && pos.col > 0) {
            pos.col--;
        }
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

    case MOTION_NEXT_OCCUR:
        if (frame->buf->num_matches == 0) {
            set_message("no matches");
            return 0;
        }
        index = find_current_match(frame->buf, &frame->cur);
        index += Core.counter % frame->buf->num_matches;
        index %= frame->buf->num_matches;
        set_cursor(frame, &frame->buf->matches[index].from);
        set_message("%s [%zu/%zu]", frame->buf->search_pat, index + 1,
                frame->buf->num_matches);
        return 1;

    case MOTION_PREV_OCCUR:
        if (frame->buf->num_matches == 0) {
            return 0;
        }
        index = find_current_match(frame->buf, &frame->cur);
        pos = frame->buf->matches[index].from;
        if (pos.line != frame->cur.line || pos.col != frame->cur.col) {
            Core.counter--;
        }
        Core.counter %= frame->buf->num_matches;
        if (Core.counter > index) {
            index = frame->buf->num_matches - Core.counter;
        } else {
            index -= Core.counter;
        }
        index %= frame->buf->num_matches;
        set_cursor(frame, &frame->buf->matches[index].from);
        set_message("%s [%zu/%zu]", frame->buf->search_pat, index + 1,
                frame->buf->num_matches);
        return 1;

    case MOTION_SCROLL_UP:
        return scroll_frame(SelFrame, Core.counter, -1);

    case MOTION_SCROLL_DOWN:
        return scroll_frame(SelFrame, Core.counter, 1);

    case MOTION_HALF_UP:
        Core.counter = safe_mul(Core.counter, MAX(frame->h / 2, 1));
        return scroll_frame(frame, Core.counter, -1);

    case MOTION_HALF_DOWN:
        Core.counter = safe_mul(Core.counter, MAX(frame->h / 2, 1));
        return scroll_frame(frame, Core.counter, 1);

    case MOTION_PAREN:
        index = get_paren(frame->buf, &frame->cur);
        if (index != SIZE_MAX && get_matching_paren(frame->buf, index, &pos)) {
            set_cursor(frame, &pos);
            return 1;
        }
        break;
    }
    return 0;
}

int get_binded_motion(int c)
{
    static int motions[KEY_MAX] = {
        [KEY_LEFT] = MOTION_LEFT,
        [KEY_RIGHT] = MOTION_RIGHT,
        [KEY_UP] = MOTION_UP,
        [KEY_DOWN] = MOTION_DOWN,

        ['h'] = MOTION_LEFT,
        ['l'] = MOTION_RIGHT,
        ['k'] = MOTION_UP,
        ['j'] = MOTION_DOWN,

        ['0'] = MOTION_HOME,
        ['$'] = MOTION_END,

        ['H'] = MOTION_BEG_FRAME,
        ['M'] = MOTION_MIDDLE_FRAME,
        ['L'] = MOTION_END_FRAME,

        ['f'] = MOTION_FIND_NEXT,
        ['F'] = MOTION_FIND_PREV,

        ['t'] = MOTION_FIND_EXCL_NEXT,
        ['T'] = MOTION_FIND_EXCL_PREV,

        ['W'] = MOTION_NEXT_WORD,
        ['w'] = MOTION_NEXT_WORD,

        ['E'] = MOTION_END_WORD,
        ['e'] = MOTION_END_WORD,

        ['B'] = MOTION_PREV_WORD,
        ['b'] = MOTION_PREV_WORD,

        [KEY_HOME] = MOTION_HOME,
        [KEY_END] = MOTION_END,

        ['g'] = MOTION_FILE_BEG,
        ['G'] = MOTION_FILE_END,

        [0x7f] = MOTION_PREV,
        [KEY_BACKSPACE] = MOTION_PREV,
        ['\b'] = MOTION_PREV,
        [' '] = MOTION_NEXT,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,

        ['{'] = MOTION_PARA_UP,
        ['}'] = MOTION_PARA_DOWN,

        ['n'] = MOTION_NEXT_OCCUR,
        ['N'] = MOTION_PREV_OCCUR,

        ['%'] = MOTION_PAREN,

        ['K'] = MOTION_SCROLL_UP,
        ['J'] = MOTION_SCROLL_DOWN,

        [CONTROL('U')] = MOTION_HALF_UP,
        [CONTROL('D')] = MOTION_HALF_DOWN,
    };

    switch (c) {
    case 'g':
        c = get_extra_char();
        switch (c) {
        case 'G':
        case 'g':
            break;

        default:
            c = 0;
        }
    }

    return motions[c];
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
    struct pos prev_cur;

    buf = frame->buf;
    prev_cur = frame->cur;
    if (dir < 0) {
        if (dist > frame->cur.line) {
            dist = frame->cur.line;
        }
        frame->cur.line -= dist;
    } else {
        /* check for overflow */
        if (SIZE_MAX - dist < frame->cur.line ||
                dist + frame->cur.line >= buf->num_lines) {
            dist = buf->num_lines - 1 - frame->cur.line;
        }
        frame->cur.line += dist;
    }

    if (prev_cur.line != frame->cur.line) {
        adjust_cursor(frame);
        r = 1;
    }
    /* set the previous cursor if a significant jump was made */
    if (dist > (size_t) MAX(frame->h / 2, 3)) {
        frame->prev_cur = prev_cur;
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
