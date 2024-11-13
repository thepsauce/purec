#include "frame.h"
#include "purec.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    struct buf      *buf;
    struct line     *line;

    buf = frame->buf;
    if (frame->cur.line >= buf->text.num_lines) {
        frame->cur.line = buf->text.num_lines - 1;
    }
    line = &buf->text.lines[frame->cur.line];
    frame->cur.col = get_index(line->s, get_mode_line_end(line),
                               buf->rule.tab_size, frame->vct);
}

int adjust_scroll(struct frame *frame)
{
    int             x, y, w, h;
    struct line     *line;
    int             r = 0;
    col_t           v_x;

    get_text_rect(frame, &x, &y, &w, &h);

    line = &frame->buf->text.lines[frame->cur.line];
    v_x = get_advance(line->s, line->n,
                      frame->buf->rule.tab_size, frame->cur.col);

    if (v_x < frame->scroll.col) {
        frame->scroll.col = v_x;
        if ((col_t) MIN(w / 3, 25) >= frame->scroll.col) {
            frame->scroll.col = 0;
        } else {
            frame->scroll.col -= MIN(w / 3, 25);
        }
        r |= 1;
    } else if (v_x >= frame->scroll.col + w) {
        frame->scroll.col = v_x - w + MAX(MIN(w / 3, 25), 1);
        r |= 1;
    }

    if (frame->cur.line < frame->scroll.line) {
        frame->scroll.line = frame->cur.line;
        if ((line_t) (h / 3) >= frame->scroll.line) {
            frame->scroll.line = 0;
        } else {
            frame->scroll.line -= h / 3;
        }
        r |= 1;
    } else if (frame->cur.line >= frame->scroll.line + h) {
        frame->scroll.line = frame->cur.line - 2 * h / 3;
        frame->scroll.line = MIN(frame->scroll.line,
                                 frame->buf->text.num_lines - h);
        r |= 1;
    }
    return r;
}

col_t compute_vct(struct frame *frame, const struct pos *pos)
{
    struct line *line;

    line = &frame->buf->text.lines[pos->line];
    return get_advance(line->s, get_mode_line_end(line),
                       frame->buf->rule.tab_size, pos->col);
}

int scroll_frame(struct frame *frame, line_t dist)
{
    line_t          old_scroll;

    old_scroll = frame->scroll.line;
    if (dist < 0) {
        dist = MIN(frame->scroll.line, -dist);
        frame->scroll.line -= dist;
        frame->cur.line -= dist;
    } else {
        frame->scroll.line = safe_add(frame->scroll.line, dist);
        if (frame->scroll.line >= frame->buf->text.num_lines) {
            frame->scroll.line = frame->buf->text.num_lines - 1;
        }
        frame->cur.line += frame->scroll.line - old_scroll;
        frame->cur.line = MIN(frame->cur.line, frame->buf->text.num_lines - 1);
    }
    if (old_scroll != frame->scroll.line) {
        clip_column(frame);
        return 1;
    }
    return 0;
}

void clip_column(struct frame *frame)
{
    col_t           end;

    end = get_mode_line_end(&frame->buf->text.lines[frame->cur.line]);
    frame->cur.col = MIN(frame->cur.col, end);
}

void set_cursor(struct frame *frame, const struct pos *pos)
{
    struct line     *line;
    struct pos      new_cur;
    col_t           n;

    new_cur = *pos;

    /* clip line */
    if (new_cur.line >= frame->buf->text.num_lines) {
        new_cur.line = frame->buf->text.num_lines - 1;
    }

    /* clip column */
    line = &frame->buf->text.lines[new_cur.line];
    n = get_mode_line_end(line);
    new_cur.col = MIN(new_cur.col, n);

    /* set vct */
    frame->vct = get_advance(line->s, n,
                             frame->buf->rule.tab_size, new_cur.col);

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

static int move_left(struct frame *frame)
{
    col_t           col;
    struct line     *line;

    col = frame->cur.col;
    if (col == 0) {
        return 0;
    }
    frame->next_cur.line = frame->cur.line;
    line = &frame->buf->text.lines[frame->next_cur.line];
    for (; Core.counter > 0; Core.counter--) {
        if (col == 0) {
            break;
        }
        col = move_back_glyph(line->s, col);
    }
    frame->next_cur.col = col;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_right(struct frame *frame)
{
    col_t           col;
    struct line     *line;

    col = frame->cur.col;
    line = &frame->buf->text.lines[frame->cur.line];
    if (col == line->n) {
        return 0;
    }
    for (; Core.counter > 0; Core.counter--) {
        if (col == line->n) {
            break;
        }
        col += get_glyph_count(&line->s[col], line->n - col);
    }
    frame->next_cur.col = col;
    frame->next_cur.line = frame->cur.line;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_up(struct frame *frame)
{
    struct line     *line;

    if (frame->cur.line == 0) {
        return 0;
    }
    if ((size_t) frame->cur.line < Core.counter) {
        frame->next_cur.line = 0;
    } else {
        frame->next_cur.line = frame->cur.line - Core.counter;
    }
    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col = get_index(line->s, line->n,
                                    frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_down(struct frame *frame)
{
    struct line     *line;

    if (SIZE_MAX - Core.counter < (size_t) frame->cur.line) {
        frame->next_cur.line = frame->buf->text.num_lines - 1;
    } else {
        frame->next_cur.line = frame->cur.line + Core.counter;
        frame->next_cur.line = MIN(frame->next_cur.line,
                                   frame->buf->text.num_lines - 1);
    }
    if (frame->next_cur.line == frame->cur.line) {
        return 0;
    }
    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col = get_index(line->s, line->n,
                                    frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_home(struct frame *frame)
{
    if (frame->cur.col == 0) {
        return 0;
    }
    frame->next_cur.col  = 0;
    frame->next_cur.line = frame->cur.line;
    frame->next_vct = 0;
    return UPDATE_UI;
}

static int move_end(struct frame *frame)
{
    col_t           n;

    frame->next_vct = SIZE_MAX;
    n = frame->buf->text.lines[frame->cur.line].n;
    if (frame->cur.col == n) {
        return 0;
    }
    frame->next_cur.col  = n;
    frame->next_cur.line = frame->cur.line;
    return UPDATE_UI;
}

static int move_prev(struct frame *frame)
{
    col_t           col;
    line_t          line_i;
    struct line     *line;

    if (frame->cur.col == 0 && frame->cur.line == 0) {
        return 0;
    }

    col = frame->cur.col;
    line_i = frame->cur.line;
    line = &frame->buf->text.lines[line_i];
    while (1) {
        for (; Core.counter > 0; Core.counter--) {
            if (col == 0) {
                break;
            }
            col = move_back_glyph(line->s, col);
        }
        if (Core.counter == 0 || line_i == 0) {
            break;
        }
        line_i--;
        line--;
        col = get_mode_line_end(line);
        Core.counter--;
    }
    frame->next_cur.col = col;
    frame->next_cur.line = line_i;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_next(struct frame *frame)
{
    col_t           col;
    line_t          line_i;
    struct line     *line;

    col = frame->cur.col;
    line_i = frame->cur.line;
    line = &frame->buf->text.lines[line_i];
    while (line_i != frame->buf->text.num_lines - 1) {
        for (; Core.counter > 0; Core.counter--) {
            if (col == get_mode_line_end(line)) {
                break;
            }
            col += get_glyph_count(&line->s[col], line->n - col);
        }
        if (Core.counter == 0) {
            break;
        }
        col = 0;
        line_i++;
        line++;
        Core.counter--;
    }
    if (col == frame->cur.col && line_i == frame->cur.line) {
        return 0;
    }
    frame->next_cur.col = col;
    frame->next_cur.line = line_i;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_frame_start(struct frame *frame)
{
    struct line     *line;

    if (frame->cur.line == frame->scroll.line) {
        return 0;
    }
    frame->next_cur.line = frame->scroll.line;

    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col  = get_index(line->s, line->n,
                                     frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_frame_middle(struct frame *frame)
{
    struct line     *line;

    frame->next_cur.line = frame->scroll.line + frame->h / 2;
    frame->next_cur.line = MIN(frame->next_cur.line,
                               frame->buf->text.num_lines - 1);
    if (frame->next_cur.line == frame->cur.line) {
        return 0;
    }

    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col  = get_index(line->s, line->n,
                                     frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_frame_end(struct frame *frame)
{
    struct line     *line;

    frame->next_cur.line = frame->scroll.line + frame->h - 2;
    frame->next_cur.line = MIN(frame->next_cur.line,
                               frame->buf->text.num_lines - 1);
    if (frame->next_cur.line == frame->cur.line) {
        return 0;
    }

    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col  = get_index(line->s, line->n,
                                     frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_to_line_indent(struct frame *frame)
{
    (void) get_line_indent(frame->buf, frame->cur.line, &frame->next_cur.col);
    frame->next_cur.line = frame->cur.line;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_to_start(struct frame *frame)
{
    struct line     *line;

    Core.counter = MIN(Core.counter - 1,
                       (size_t) frame->buf->text.num_lines - 1);
    if ((size_t) frame->cur.line == Core.counter) {
        return 0;
    }
    frame->next_cur.line = Core.counter;
    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col = get_index(line->s, line->n,
                                    frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_to_end(struct frame *frame)
{
    struct line     *line;

    if (Core.counter >= (size_t) frame->buf->text.num_lines) {
        frame->next_cur.line = 0;
    } else {
        frame->next_cur.line = frame->buf->text.num_lines - Core.counter;
    }
    if (frame->next_cur.line == frame->cur.line) {
        return 0;
    }
    line = &frame->buf->text.lines[frame->next_cur.line];
    frame->next_cur.col = get_index(line->s, line->n,
                                    frame->buf->rule.tab_size, frame->vct);
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_page_up(struct frame *frame)
{
    line_t          n;

    if (frame->next_cur.line == 0) {
        return 0;
    }
    n = frame->h * 2 / 3;
    n = MAX(n, 1);
    n = safe_mul(Core.counter, n);
    if (frame->next_cur.line <= n) {
        frame->next_cur.line = 0;
    } else {
        frame->next_cur.line -= n;
    }
    frame->next_cur.col = frame->cur.col;
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_page_down(struct frame *frame)
{
    line_t          n;

    n = frame->h * 2 / 3;
    n = MAX(n, 1);
    n = safe_mul(Core.counter, n);
    if (LINE_MAX - n < frame->next_cur.line) {
        frame->next_cur.line = frame->buf->text.num_lines - 1;
    } else {
        frame->next_cur.line += n;
        frame->next_cur.line = MIN(frame->next_cur.line,
                                  frame->buf->text.num_lines - 1);
    }
    if (frame->next_cur.line == frame->cur.line) {
        return 0;
    }
    frame->next_cur.col = frame->cur.col;
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_up_paragraph(struct frame *frame)
{
    line_t          line;

    line = frame->cur.line;
    if (line == 0) {
        return 0;
    }
    while (line > 0) {
        line--;
        if (frame->buf->text.lines[line].n == 0) {
            if (Core.counter > 1) {
                Core.counter--;
                continue;
            }
            break;
        }
    }
    frame->next_cur.col = 0;
    frame->next_cur.line = line;
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int move_down_paragraph(struct frame *frame)
{
    line_t          line;

    line = frame->cur.line;
    if (line + 1 == frame->buf->text.num_lines) {
        return 0;
    }
    while (line + 1 != frame->buf->text.num_lines) {
        line++;
        if (frame->buf->text.lines[line].n == 0) {
            if (Core.counter > 1) {
                Core.counter--;
                continue;
            }
            break;
        }
    }
    frame->next_cur.col = 0;
    frame->next_cur.line = line;
    frame->next_vct = frame->vct;
    return UPDATE_UI;
}

static int get_additional_char(char *buf)
{
    int             c;
    int             e;
    int             n;

    c = get_char();
    if (c >= 0x100) {
        return -1;
    }

    e = get_expected_bytes(c);
    n = 0;
    while (1) {
        buf[n++] = c;
        if (n == e) {
            break;
        }
        do {
            c = get_ch();
        } while (c == -1);
    }
    return n;
}

static int find_prev_char(struct frame *frame, bool excl)
{
    char            buf[4];
    int             n;
    col_t           col;
    struct line     *line;
    col_t           new_col;

    /* the glyph to look for */
    n = get_additional_char(buf);
    if (n < 0) {
        return -1;
    }

    col = frame->cur.col;
    new_col = col;
    line = &frame->buf->text.lines[frame->cur.line];
    if (excl && col > 0) {
        col = move_back_glyph(line->s, col);
    }
    while (col >= (col_t) n) {
        if (memcmp(&line->s[col - n], buf, n) == 0) {
            if (Core.counter > 1) {
                Core.counter--;
                continue;
            }
            new_col = col;
            if (!excl) {
                new_col -= n;
            }
            break;
        }
        col--;
    }
    if (new_col == frame->cur.col) {
        return 0;
    }
    frame->next_cur.col = new_col;
    frame->next_cur.line = frame->cur.line;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int find_prev_character(struct frame *frame)
{
    return find_prev_char(frame, false);
}

static int find_prev_character_excl(struct frame *frame)
{
    return find_prev_char(frame, true);
}

static int find_next_char(struct frame *frame, bool excl)
{
    char            buf[4];
    int             n;
    col_t           col;
    struct line     *line;
    col_t           new_col;

    /* the glyph to look for */
    n = get_additional_char(buf);
    if (n < 0) {
        return -1;
    }

    line = &frame->buf->text.lines[frame->cur.line];
    col = frame->cur.col;
    new_col = col;
    if (excl) {
        /* make sure when the cursor is right before a match already, that
         * it is not taken again
         */
        col += get_glyph_count(&line->s[col], line->n - col);
    }
    while (col++, col + n <= line->n) {
        if (memcmp(&line->s[col], buf, n) == 0) {
            if (Core.counter > 1) {
                Core.counter--;
                continue;
            }
            new_col = col;
            if (excl) {
                new_col = move_back_glyph(line->s, new_col);
            }
            break;
        }
    }
    if (new_col == frame->cur.col) {
        return 0;
    }
    frame->next_cur.col = new_col;
    frame->next_cur.line = frame->cur.line;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return 2;
}

static int find_next_character(struct frame *frame)
{
    return find_next_char(frame, false);
}

static int find_next_character_excl(struct frame *frame)
{
    return find_next_char(frame, true);
}

static int move_to_prev_word_excl(struct frame *frame)
{
    struct pos      p;
    struct line     *line;
    int             s, o_s;

    if (SelFrame->cur.col == 0 && SelFrame->cur.line == 0) {
        return 0;
    }

    p = frame->cur;
    line = &frame->buf->text.lines[p.line];

    do {
        s = -1;
        if (p.col > 0 && p.col == line->n) {
            p.col--;
        }
        while (p.col > 0) {
            o_s = get_char_state(line->s[p.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            s = o_s;
            p.col--;
        }

        while (1) {
            for (; p.col > 0; p.col--) {
                if (!isblank(line->s[p.col])) {
                    break;
                }
            }

            if (p.col > 0) {
                break;
            }

            if (p.line == 0) {
                break;
            }

            p.line--;
            line--;
            p.col = line->n == 0 ? 0 : line->n - 1;
        }

        Core.counter--;
    } while (Core.counter > 0 && (p.col > 0 || p.line > 0));

    frame->next_cur = p;
    frame->next_vct = compute_vct(frame, &p);
    return 2;
}

static int move_to_next_word_excl(struct frame *frame)
{
    struct pos      p;
    struct line     *line;
    int             s, o_s;

    p = frame->cur;
    line = &frame->buf->text.lines[p.line];

    do {
        p.col++;
        while (1) {
            for (; p.col < line->n; p.col++) {
                if (!isblank(line->s[p.col])) {
                    break;
                }
            }

            if (p.col < line->n) {
                break;
            }

            if (p.line + 1 >= frame->buf->text.num_lines) {
                break;
            }

            p.line++;
            p.col = 0;
            line++;
        }

        s = -1;
        while (p.col < line->n) {
            o_s = get_char_state(line->s[p.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            s = o_s;
            p.col++;
        }

        if (p.col > 0) {
            p.col--;
        }

        Core.counter--;
    } while (Core.counter > 0 && (p.col + 1 < line->n ||
                                  p.line + 1 < frame->buf->text.num_lines));

    if (is_point_equal(&frame->cur, &p)) {
        return 0;
    }
    frame->next_cur = p;
    frame->next_vct = compute_vct(frame, &p);
    return 2;
}

static int move_to_prev_word(struct frame *frame)
{
    struct pos      p;
    struct line     *line;
    int             s, o_s;

    p = frame->cur;
    line = &frame->buf->text.lines[p.line];

    do {
        for (; p.col > 0; p.col--) {
            if (!isblank(line->s[p.col - 1])) {
                break;
            }
        }

        if (p.col == 0) {
            if (p.line > 0) {
                line--;
                p.line--;
                p.col = line->n;
            }
        }

        for (; p.col > 0; p.col--) {
            if (!isblank(line->s[p.col - 1])) {
                break;
            }
        }

        s = -1;
        while (p.col > 0) {
            o_s = get_char_state(line->s[p.col - 1]);
            if (s >= 0 && s != o_s) {
                break;
            }
            p.col--;
            s = o_s;
        }
        Core.counter--;
    } while (Core.counter > 0 && (p.line > 0 || p.col > 0));

    frame->next_cur = p;
    if (is_point_equal(&frame->cur, &p)) {
        return 0;
    }
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

static int move_to_next_word(struct frame *frame)
{
    struct pos      p;
    struct line     *line;
    int             s, o_s;

    p = frame->cur;
    line = &frame->buf->text.lines[p.line];

    do {
        s = -1;
        while (p.col < line->n) {
            o_s = get_char_state(line->s[p.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            p.col++;
            s = o_s;
        }

        if (p.col == line->n) {
            if (p.line + 1 < frame->buf->text.num_lines) {
                p.line++;
                p.col = 0;
                line++;
            }
        }

        for (; p.col < line->n; p.col++) {
            if (!isblank(line->s[p.col])) {
                break;
            }
        }

        Core.counter--;
    } while (Core.counter > 0 && (p.col < line->n ||
                                 p.line + 1 < frame->buf->text.num_lines));

    if (is_point_equal(&frame->cur, &p)) {
        return 0;
    }
    frame->next_cur = p;
    frame->next_vct = compute_vct(frame, &p);
    return UPDATE_UI;
}

static int move_to_extended(struct frame *frame)
{
    int             c;

    c = get_extra_char();
    switch (c) {
    case 'g':
        return move_to_start(frame);
    case 'G':
        return move_to_end(frame);
    case 'e':
        return move_to_prev_word_excl(frame);
    case 'f':
        jump_to_file(frame->buf, &frame->cur);
        SelFrame->next_cur = SelFrame->cur;
        SelFrame->next_vct = SelFrame->vct;
        return UPDATE_UI;
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

static int _find_prev_match(struct frame *frame)
{
    size_t          index;
    struct pos      p;

    index = find_current_match(frame->buf, &frame->cur);
    p = frame->buf->matches[index].from;
    if (p.line != frame->cur.line || p.col != frame->cur.col) {
        Core.counter--;
    }
    Core.counter %= frame->buf->num_matches;
    if ((size_t) Core.counter > index) {
        index = frame->buf->num_matches - Core.counter;
    } else {
        index -= Core.counter;
    }
    index %= frame->buf->num_matches;
    frame->next_cur = frame->buf->matches[index].from;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    set_message("%s [%zu/%zu]", frame->buf->search_pat, index + 1,
                frame->buf->num_matches);
    return UPDATE_UI;
}

static int _find_next_match(struct frame *frame)
{
    size_t          index;
    struct pos      p;

    index = find_current_match(frame->buf, &frame->cur);
    p = frame->buf->matches[(index + 1) % frame->buf->num_matches].from;
    if (frame->cur.line == p.line && frame->cur.col + 1 == p.col &&
            p.col == frame->buf->text.lines[p.line].n) {
        Core.counter = safe_add(Core.counter, 1);
    }
    index += Core.counter % frame->buf->num_matches;
    index %= frame->buf->num_matches;
    frame->next_cur = frame->buf->matches[index].from;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    set_message("%s [%zu/%zu]", frame->buf->search_pat, index + 1,
                frame->buf->num_matches);
    return UPDATE_UI;
}

static int find_prev_match(struct frame *frame)
{
    if (frame->buf->num_matches == 0) {
        set_message("no matches");
        return UPDATE_UI;
    }
    if (Core.search_dir == -1) {
        return _find_next_match(frame);
    }
    return _find_prev_match(frame);
}

static int find_next_match(struct frame *frame)
{
    if (frame->buf->num_matches == 0) {
        set_message("no matches");
        return UPDATE_UI;
    }
    if (Core.search_dir == -1) {
        return _find_prev_match(frame);
    }
    return _find_next_match(frame);
}

static int goto_matching_paren(struct frame *frame)
{
    size_t          index;

    index = get_paren(frame->buf, &frame->next_cur);
    if (index == SIZE_MAX) {
        return 0;
    }
    index = get_matching_paren(frame->buf, index);
    if (index == SIZE_MAX) {
        return 0;
    }
    frame->next_cur = frame->buf->parens[index].pos;
    frame->next_vct = compute_vct(frame, &frame->next_cur);
    return UPDATE_UI;
}

int prepare_motion(struct frame *frame, int motion_key)
{
    static int (*const binds[])(struct frame *frame) = {
        [KEY_LEFT]      = move_left,
        ['h']           = move_left,
        [KEY_RIGHT]     = move_right,
        ['l']           = move_right,
        [KEY_UP]        = move_up,
        ['k']           = move_up,
        [KEY_DOWN]      = move_down,
        ['j']           = move_down,
        [KEY_HOME]      = move_home,
        ['0']           = move_home,
        [KEY_END]       = move_end,
        ['$']           = move_end,
        [0x7f]          = move_prev,
        [KEY_BACKSPACE] = move_prev,
        [' ']           = move_next,
        ['H']           = move_frame_start,
        ['M']           = move_frame_middle,
        ['L']           = move_frame_end,
        ['I']           = move_to_line_indent,
        ['g']           = move_to_extended,
        ['G']           = move_to_end,
        [KEY_PPAGE]     = move_page_up,
        [KEY_NPAGE]     = move_page_down,
        ['{']           = move_up_paragraph,
        ['}']           = move_down_paragraph,
        ['F']           = find_prev_character,
        ['T']           = find_prev_character_excl,
        ['f']           = find_next_character,
        ['t']           = find_next_character_excl,
        ['B']           = move_to_prev_word,
        ['b']           = move_to_prev_word,
        ['\b']          = move_to_prev_word,
        ['W']           = move_to_next_word,
        ['w']           = move_to_next_word,
        ['E']           = move_to_next_word_excl,
        ['e']           = move_to_next_word_excl,
        ['N']           = find_prev_match,
        ['n']           = find_next_match,
        ['%']           = goto_matching_paren,
    };
    return motion_key >= (int) ARRAY_SIZE(binds) ||
            binds[motion_key] == NULL ? -1 : binds[motion_key](frame);
}

int apply_motion(struct frame *frame)
{
    col_t           max_col;

    /* save the cursor if it moved enough */
    if (frame->cur.line > frame->next_cur.line) {
        if (frame->cur.line - frame->next_cur.line > MAX(frame->h, 2)) {
            frame->prev_cur = frame->cur;
        }
    } else if (frame->next_cur.line - frame->cur.line > MAX(frame->h, 2)) {
        frame->prev_cur = frame->cur;
    }
    max_col = get_mode_line_end(&frame->buf->text.lines[frame->next_cur.line]);
    frame->cur.col = MIN(frame->next_cur.col, max_col);
    frame->cur.line = frame->next_cur.line;
    frame->vct = frame->next_vct;
    adjust_scroll(frame);
    return 1;
}

int do_action_till(struct frame *frame, int action, line_t min_line,
                   line_t max_line)
{
    struct buf          *buf;
    bool                update;
    col_t               n;
    line_t              i;
    col_t               c;
    struct pos          p1, p2;
    struct line         *line;
    struct text         text;
    struct undo_event   *ev;

    buf = frame->buf;
    update = false;
    if (action == '>') {
        text.num_lines = max_line - min_line + 1;
        text.lines = xmalloc(sizeof(*text.lines) * text.num_lines);
        for (i = min_line; i <= max_line; i++) {
            line = &text.lines[i - min_line];
            if (buf->text.lines[i].n == 0) {
                /* do not indent empty lines */
                line->s = NULL;
                line->n = 0;
                continue;
            }
            update = true;
            if (buf->rule.use_spaces) {
                line->n = buf->rule.tab_size;
                line->s = xmalloc(buf->rule.tab_size);
                for (c = 0; c < buf->rule.tab_size; c++) {
                    line->s[c] = ' ';
                }
            } else {
                line->n = 1;
                line->s = xmalloc(1);
                line->s[0] = '\t';
            }
        }
        if (!update) {
            /* if all lines are empty, there is nothing to indent */
            free(text.lines);
            return 0;
        }
        p1.col = 0;
        p1.line = min_line;
        ev = _insert_block(buf, &p1, &text);
        ev->cur = frame->cur;
        if (buf->text.lines[frame->cur.line].n > 0) {
            frame->cur.col += buf->rule.tab_size;
        }
        return UPDATE_UI;
    } else if (action == '<') {
        if (frame->cur.col > buf->rule.tab_size) {
            frame->cur.col -= buf->rule.tab_size;
        } else {
            frame->cur.col = 0;
        }
    }

    for (; min_line <= max_line; min_line++) {
        if (buf->text.lines[min_line].n == 0) {
            /* skip empty lines */
            continue;
        }

        switch (action) {
        case '=':
            ev = indent_line(buf, min_line);
            if (ev != NULL) {
                update = true;
                ev->cur = frame->cur;
                if (min_line == frame->cur.line) {
                    if ((ev->flags & IS_DELETION)) {
                        if (ev->seg->data_len > (size_t) frame->cur.col) {
                            frame->cur.col = 0;
                        } else {
                            frame->cur.col -= ev->seg->data_len;
                        }
                    } else {
                        frame->cur.col += ev->seg->data_len;
                    }
                    frame->vct = frame->cur.col;
                    adjust_scroll(frame);
                }
            }
            break;

        case '<':
            if (buf->text.lines[min_line].s[0] == '\t') {
                n = 1;
            } else {
                for (n = 0; n < buf->text.lines[min_line].n; n++) {
                    if (n == buf->rule.tab_size) {
                        break;
                    }
                    if (buf->text.lines[min_line].s[n] != ' ') {
                        break;
                    }
                }
            }
            if (n > 0) {
                p1.col = 0;
                p1.line = min_line;
                p2.col = n;
                p2.line = min_line;
                ev = delete_range(buf, &p1, &p2);
                ev->cur = frame->cur;
                update = true;
            }
            break;
        }
    }
    if (update) {
        return UPDATE_UI;
    }
    return 0;
}
