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

int prepare_motion(struct frame *frame, int motion_key)
{
    struct pos new_cur;
    size_t new_vct;
    struct buf *buf;
    size_t n;
    struct line *line;
    int c;
    int s, o_s;
    size_t index;
    size_t col;

    new_cur = frame->cur;
    new_vct = frame->vct;
    buf = frame->buf;
    switch (motion_key) {
    /* move {count} times to the left */
    case KEY_LEFT:
    case 'h':
        if (new_cur.col == 0) {
            return 0;
        }
        if (new_cur.col < Core.counter) {
            new_cur.col = 0;
        } else {
            new_cur.col -= Core.counter;
        }
        new_vct = new_cur.col;
        break;

    /* move {count} times to the right */
    case KEY_RIGHT:
    case 'l':
    case 'a':
        n = buf->lines[new_cur.line].n;
        if (SIZE_MAX - Core.counter < new_cur.col) {
            new_cur.col = n;
        } else {
            new_cur.col += Core.counter;
            new_cur.col = MIN(new_cur.col, n);
        }
        if (new_cur.col == frame->cur.col) {
            return 0;
        }
        new_vct = new_cur.col;
        break;

    /* move {count} times up */
    case KEY_UP:
    case 'k':
        if (new_cur.line == 0) {
            return 0;
        }
        if (new_cur.line < Core.counter) {
            new_cur.line = 0;
        } else {
            new_cur.line -= Core.counter;
        }
        new_cur.col = frame->vct;
        break;

    /* move {count} times down */
    case KEY_DOWN:
    case 'j':
        if (SIZE_MAX - Core.counter < new_cur.line) {
            new_cur.line = buf->num_lines - 1;
        } else {
            new_cur.line += Core.counter;
            new_cur.line = MIN(new_cur.line, buf->num_lines - 1);
        }
        if (new_cur.line == frame->cur.line) {
            return 0;
        }
        new_cur.col = frame->vct;
        break;

    /* move to the start of the line */
    case KEY_HOME:
    case '0':
        if (new_cur.col == 0) {
            return 0;
        }
        new_cur.col = 0;
        new_vct = 0;
        break;

    /* move to the end of the line */
    case KEY_END:
    case '$':
    case 'A':
        n = buf->lines[new_cur.line].n;
        new_cur.col = n;
        new_vct = SIZE_MAX;
        break;

    /* move back {count} characters, a line end counts as one character */
    case KEY_BACKSPACE:
        if (new_cur.col == 0 && new_cur.line == 0) {
            return 0;
        }

        while (new_cur.col < Core.counter) {
            Core.counter -= new_cur.col + 1;
            if (new_cur.line == 0) {
                break;
            }
            new_cur.line--;
            new_cur.col = get_mode_line_end(&buf->lines[new_cur.line]);
        }
        if (new_cur.col < Core.counter) {
            new_cur.col = 0;
        } else {
            new_cur.col -= Core.counter;
        }
        new_vct = new_cur.col;
        break;

    /* move forward {count} characters, a line end counts as one character */
    case ' ':
        while (new_cur.line != buf->num_lines - 1) {
            n = get_mode_line_end(&buf->lines[new_cur.line]);
            n -= new_cur.col;
            if (Core.counter <= n) {
                break;
            }
            Core.counter -= n + 1;
            new_cur.col = 0;
            new_cur.line++;
        }
        n = buf->lines[new_cur.line].n;
        if (SIZE_MAX - Core.counter < new_cur.col) {
            new_cur.col = n;
        } else {
            new_cur.col += Core.counter;
            new_cur.col = MIN(new_cur.col, n);
        }
        if (new_cur.col == frame->cur.col && new_cur.line == frame->cur.line) {
            return 0;
        }
        new_vct = new_cur.col;
        break;

    /* move to the start of the frame */
    case 'H':
        if (new_cur.line == frame->scroll.line) {
            return 0;
        }
        /* no bounds check needed */
        new_cur.line = frame->scroll.line;
        break;

    /* move to the middle of the frame */
    case 'M':
        new_cur.line = frame->scroll.line + frame->h / 2;
        new_cur.line = MIN(new_cur.line, buf->num_lines - 1);
        if (new_cur.line == frame->cur.line) {
            return 0;
        }
        break;

    /* move to the end of the frame */
    case 'L':
        new_cur.line = frame->scroll.line + frame->h - 2;
        new_cur.line = MIN(new_cur.line, buf->num_lines - 1);
        if (new_cur.line == frame->cur.line) {
            return 0;
        }
        break;

    /* move to the start of the line but skip all indentation */
    case 'I':
        new_cur.col = get_line_indent(buf, new_cur.line);
        if (new_cur.col == frame->cur.col) {
            return 0;
        }
        break;

    /* go to ... */
    case 'g':
        motion_key = get_extra_char();
        /* fall through */
    case 'G':
        switch (motion_key) {
        /* ...the line at {counter} */
        case 'g':
            Core.counter--;
            if (new_cur.line == Core.counter) {
                return 0;
            }
            new_cur.col = frame->vct;
            new_cur.line = Core.counter;
            break;

        case 'G':
            if (Core.counter >= buf->num_lines) {
                new_cur.line = 0;
            } else {
                new_cur.line = buf->num_lines - Core.counter;
            }
            if (new_cur.line == frame->cur.line) {
                return 0;
            }
            new_cur.col = frame->vct;
            break;
            
        default:
            return 0;
        }
        break;

    /* move up {count} pages where a page is 2/3 of the frame height */
    case KEY_PPAGE:
        if (new_cur.line == 0) {
            return 0;
        }
        n = frame->h * 2 / 3;
        n = MAX(n, 1);
        n = safe_mul(Core.counter, n);
        if (new_cur.line <= n) {
            new_cur.line = 0;
        } else {
            new_cur.line -= n;
        }
        break;

    /* move down {count} pages where a page is 2/3 of the frame height */
    case KEY_NPAGE:
        n = frame->h * 2 / 3;
        n = MAX(n, 1);
        n = safe_mul(Core.counter, n);
        if (SIZE_MAX - n < new_cur.line) {
            new_cur.line = buf->num_lines - 1;
        } else {
            new_cur.line += n;
            new_cur.line = MIN(new_cur.line, buf->num_lines - 1);
        }
        if (new_cur.line == frame->cur.line) {
            return 0;
        }
        break;

    /* move up {count} paragraphs */
    case '{':
        if (frame->cur.line == 0) {
            return 0;
        }
        while (new_cur.line > 0) {
            new_cur.line--;
            if (buf->lines[new_cur.line].n == 0) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                break;
            }
        }
        break;

    /* move down {count} paragraphs */
    case '}':
        if (new_cur.line + 1 == buf->num_lines) {
            return 0;
        }
        while (new_cur.line + 1 != buf->num_lines) {
            new_cur.line++;
            if (buf->lines[new_cur.line].n == 0) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                break;
            }
        }
        break;

    /* find the {count}'th character */
    case 'f':
    case 't': /* this moves in an exclusive way */
        /* the character to look for */
        do {
            c = get_ch();
            /* skip over rezise events */
        } while (c == -1);
        col = new_cur.col;
        if (motion_key == 't') {
            /* make sure when the cursor is right before a match already, that
             * it is not taken again
             */
            col++;
        }
        line = &buf->lines[new_cur.line];
        while (col++, col < line->n) {
            if (line->s[col] == c) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                new_cur.col = col;
                if (motion_key == 't') {
                    new_cur.col--;
                }
                break;
            }
        }
        if (new_cur.col == frame->cur.col) {
            return 0;
        }
        new_vct = new_cur.col;
        break;

    /* find the {count}'th character backwards */
    case 'F':
    case 'T': /* this moves in an exclusive way */
        /* the character to look for */
        do {
            c = get_ch();
            /* skip over resize events */
        } while (c == -1);
        col = new_cur.col;
        if (motion_key == 'T' && col > 0) {
            col--;
        }
        line = &buf->lines[new_cur.line];
        while (col > 0) {
            col--;
            if (line->s[col] == c) {
                if (Core.counter > 1) {
                    Core.counter--;
                    continue;
                }
                new_cur.col = col;
                if (motion_key == 'T') {
                    new_cur.col++;
                }
                break;
            }
        }
        if (new_cur.col == frame->cur.col) {
            return 0;
        }
        new_vct = new_cur.col;
        break;

    /* move forward {count} words, skip space after a word */
    case 'W':
    case 'w':
        line = &frame->buf->lines[new_cur.line];

    again_next:
        s = -1;
        while (new_cur.col < line->n) {
            o_s = get_char_state(line->s[new_cur.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            new_cur.col++;
            s = o_s;
        }

        if (new_cur.col == line->n) {
            if (new_cur.line + 1 < frame->buf->num_lines) {
                new_cur.line++;
                new_cur.col = 0;
                line++;
            }
        }

        for (; new_cur.col < line->n; new_cur.col++) {
            if (!isblank(line->s[new_cur.col])) {
                break;
            }
        }

        if (is_point_equal(&frame->cur, &new_cur)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_next;
        }
        new_vct = new_cur.col;
        break;

    /* move forward {count} words, skip space before a word */
    case 'E':
    case 'e':
        new_cur = frame->cur;
        line = &frame->buf->lines[new_cur.line];

    again_end:
        new_cur.col++;
        s = -1;
        while (1) {
            for (; new_cur.col < line->n; new_cur.col++) {
                if (!isblank(line->s[new_cur.col])) {
                    break;
                }
            }

            if (new_cur.col < line->n) {
                break;
            }

            if (new_cur.line + 1 >= frame->buf->num_lines) {
                break;
            }

            new_cur.line++;
            new_cur.col = 0;
            line++;
        }

        while (new_cur.col < line->n) {
            o_s = get_char_state(line->s[new_cur.col]);
            if (s >= 0 && s != o_s) {
                break;
            }
            s = o_s;
            new_cur.col++;
        }

        if (new_cur.col > 0) {
            new_cur.col--;
        }

        if (is_point_equal(&frame->cur, &new_cur)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_end;
        }
        new_vct = new_cur.col;
        break;

    /* move back {count} words */
    case 'B':
    case 'b':
        line = &frame->buf->lines[new_cur.line];

    again_prev:
        for (; new_cur.col > 0; new_cur.col--) {
            if (!isblank(line->s[new_cur.col - 1])) {
                break;
            }
        }

        if (new_cur.col == 0) {
            if (new_cur.line > 0) {
                new_cur.line--;
                new_cur.col = frame->buf->lines[new_cur.line].n;
                line--;
            }
        }

        for (; new_cur.col > 0; new_cur.col--) {
            if (!isblank(line->s[new_cur.col - 1])) {
                break;
            }
        }

        s = -1;
        while (new_cur.col > 0) {
            o_s = get_char_state(line->s[new_cur.col - 1]);
            if (s >= 0 && s != o_s) {
                break;
            }
            new_cur.col--;
            s = o_s;
        }

        if (is_point_equal(&frame->cur, &new_cur)) {
            return 0;
        }

        if (Core.counter > 1) {
            Core.counter--;
            goto again_prev;
        }
        new_vct = new_cur.col;
        break;

    /* go forward {count} matches */
    case 'n':
        if (buf->num_matches == 0) {
            set_message("no matches");
            return 0;
        }
        index = find_current_match(buf, &new_cur);
        index += Core.counter % buf->num_matches;
        index %= buf->num_matches;
        new_cur = buf->matches[index].from;
        new_vct = new_cur.col;
        set_message("%s [%zu/%zu]", buf->search_pat, index + 1,
                buf->num_matches);
        break;

    /* go backwards {count} matches */
    case 'N':
        if (buf->num_matches == 0) {
            return 0;
        }
        index = find_current_match(buf, &new_cur);
        new_cur = buf->matches[index].from;
        if (new_cur.line != frame->cur.line || new_cur.col != frame->cur.col) {
            Core.counter--;
        }
        Core.counter %= buf->num_matches;
        if (Core.counter > index) {
            index = buf->num_matches - Core.counter;
        } else {
            index -= Core.counter;
        }
        index %= frame->buf->num_matches;
        new_cur = buf->matches[index].from;
        new_vct = new_cur.col;
        set_message("%s [%zu/%zu]", buf->search_pat, index + 1,
                buf->num_matches);
        break;

    /* go to the matching parenthesis */
    case '%':
        index = get_paren(frame->buf, &new_cur);
        if (index == SIZE_MAX) {
            return 0;
        }
        index = get_matching_paren(buf, index);
        if (index == SIZE_MAX) {
            return 0;
        }
        new_cur = buf->parens[index].pos;
        new_vct = new_cur.col;
        break;

    default:
        return 0;
    }
    frame->next_cur = new_cur;
    frame->next_vct = new_vct;
    return 1;
}

int apply_motion(struct frame *frame)
{
    size_t max_col;

    max_col = get_mode_line_end(&frame->buf->lines[frame->next_cur.line]);
    frame->cur.col = MIN(frame->next_cur.col, max_col);
    frame->cur.line = frame->next_cur.line;
    frame->vct = frame->next_vct;
    adjust_scroll(frame);
    return 1;
}
