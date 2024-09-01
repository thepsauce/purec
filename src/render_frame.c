#include "color.h"
#include "frame.h"
#include "lang.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>

struct render_info {
    /// the language to use
    size_t lang;

    /// the origin of the line rendering
    size_t x;
    /// the maximum width of the line rendering
    size_t w;
    /// the line the cursor is on
    struct line *cur_line;

    /// the line to render
    struct line *line;
    /// the index of the line to render
    size_t line_i;

    /// if a selection exists
    bool sel_exists;
    /// the selection
    struct selection sel;
};

#define STATE_NULL      0
#define STATE_START     1

/// if the state always wants to continue to the next line
#define FSTATE_FORCE_MULTI 0x80000000
/// if the state wants to continue to the next line only if a condition is met
#define FSTATE_MULTI 0x40000000

static char *bin_search(const char **strs, size_t num_strs, char *s, size_t s_l)
{
    size_t l, m, r;
    int cmp;

    l = 0;
    r = num_strs;
    while (l < r) {
        m = (l + r) / 2;
        cmp = strncmp(strs[m], s, s_l);
        if (cmp == 0 && strs[m][s_l] != '\0') {
            cmp = (unsigned char) strs[m][s_l];
        }
        if (cmp == 0) {
            return s;
        }
        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    return NULL;
}

/**
 * Marks the current char as paranthesis.
 *
 * @param ctx   The context that includes buffer and line.
 */
static void add_paren(struct state_ctx *ctx)
{
    (void) ctx;
}

#include "syntax/none.h"
#include "syntax/c.h"
#include "syntax/diff.h"

struct lang Langs[] = {
    { "none", none_lang_states, "\0" },
    { "C", c_lang_states, "c\0h\0cpp\0cxx\0c++\0hpp\0hxx\0h++\0" },
    { "diff", diff_lang_states, "diff\0patch\0" },
};

/**
 * Highlights a line with syntax highlighting.
 *
 * @param buf       The buffer containing the line.
 * @param line_i    The index of the line to highlight.
 * @param state     The starting state.
 */
static void highlight_line(struct buf *buf, size_t line_i, size_t state)
{
    struct state_ctx ctx;
    size_t n;
    struct line *line;

    line = &buf->lines[line_i];
    line->attribs = xreallocarray(line->attribs, line->n,
            sizeof(*line->attribs));
    memset(line->attribs, 0, sizeof(*line->attribs) * line->n);

    ctx.buf = buf;
    ctx.state = state;
    ctx.hi = HI_NORMAL;
    ctx.s = line->s;
    ctx.i = 0;
    ctx.n = line->n;

    for (ctx.i = 0; ctx.i < ctx.n; ) {
        n = (*Langs[buf->lang].fsm[ctx.state & 0xff])(&ctx);
        for (; n > 0; n--) {
            line->attribs[ctx.i] = ctx.hi;
            ctx.i++;
        }
    }

    if (!(ctx.state & (FSTATE_MULTI | FSTATE_FORCE_MULTI))) {
        ctx.state = STATE_START;
    }

    line->state = ctx.state & ~FSTATE_MULTI;
}

/**
 * Renders a line using its cached attribute data.
 *
 * @param ri    Render information.
 */
static void render_line(struct render_info *ri)
{
    size_t x;
    char ch;
    int hi;
    struct pos p;

    p.line = ri->line_i;
    for (p.col = 0, x = 0; p.col < ri->line->n && x < ri->w;) {
        ch = ri->line->s[p.col];
        if (x >= ri->x) {
            hi = ri->line->attribs[p.col];
            if (ch < 32 || ch == 0x7f) {
                if (x + 1 >= ri->w) {
                    break;
                }
                attr_set(A_REVERSE, 0, NULL);
                addch('?');
            } else {
                if (ri->sel_exists && is_in_selection(&ri->sel, &p)) {
                    attr_set(get_attrib_of(hi) ^ A_REVERSE, 0, NULL);
                } else {
                    set_highlight(stdscr, hi);
                }
                addch(ch);
            }
        }
        p.col++;
        x++;
    }

    if (x >= ri->x && x < ri->w && ri->sel_exists &&
            is_in_selection(&ri->sel, &p)) {
        attr_set(A_REVERSE, 0, NULL);
        addch(' ');
    }
}

void clean_lines(struct buf *buf)
{
    struct line *line;
    size_t prev_state;

    prev_state = buf->min_dirty_i == 0 ? STATE_START :
        buf->min_dirty_i == SIZE_MAX ? 0 :
        buf->lines[buf->min_dirty_i - 1].state;
    for (size_t i = buf->min_dirty_i;; i++) {
        line = &buf->lines[i];
        if (line->state == 0) {
            highlight_line(buf, i, prev_state);
            if ((line->state != STATE_START ||
                    line->prev_state != STATE_START) &&
                    i + 1 != buf->num_lines) {
                mark_dirty(&line[1]);
            }
        } else if (i > buf->max_dirty_i) {
            break;
        }
        prev_state = line->state;
    }

    buf->min_dirty_i = SIZE_MAX;
    buf->max_dirty_i = 0;
}

void render_frame(struct frame *frame)
{
    struct buf *buf;
    int perc;
    struct render_info ri;
    size_t line;
    size_t last_line;
    size_t prev_state;
    int x, y, w, h;
    int orig_x;

    buf = frame->buf;

    ri.cur_line = &buf->lines[frame->cur.line];
    if (SelFrame != frame) {
        ri.sel_exists = false;
    } else {
        ri.sel_exists = get_selection(&ri.sel);
    }

    last_line = frame->scroll.line + frame->h - 1;
    last_line = MIN(last_line, frame->buf->num_lines);

    prev_state = buf->min_dirty_i == 0 ? STATE_START :
        buf->min_dirty_i == SIZE_MAX ? 0 :
        buf->lines[buf->min_dirty_i - 1].state;
    for (size_t i = buf->min_dirty_i; i < last_line; i++) {
        ri.line_i = i;
        ri.line = &buf->lines[i];
        if (ri.line->state == 0) {
            highlight_line(buf, i, prev_state);
            if (i + 1 != buf->num_lines &&
                    (ri.line->state != STATE_START ||
                     ri.line->prev_state != STATE_START)) {
                mark_dirty(&ri.line[1]);
                buf->max_dirty_i = MAX(buf->max_dirty_i, i + 1);
            }
        }
        prev_state = ri.line->state;
    }

    if (last_line > buf->max_dirty_i) {
        buf->min_dirty_i = SIZE_MAX;
        buf->max_dirty_i = 0;
    } else {
        buf->min_dirty_i = MAX(buf->min_dirty_i, last_line);
    }

    orig_x = frame->x > 0;
    get_text_rect(frame, &x, &y, &w, &h);

    if (x > 2) {
        set_highlight(stdscr, HI_LINE_NO);
        line = frame->scroll.line + 1;
        for (; y < h; y++) {
            if (line > buf->num_lines) {
                set_highlight(stdscr, HI_NORMAL);
                mvaddstr(frame->y + y, frame->x + orig_x, " ~");
                for (int i = orig_x + 2; i < x; i++) {
                    addch(' ');
                }
            } else {
                mvprintw(frame->y + y, frame->x + orig_x, " %*zu ",
                        x - orig_x - 2, line);
            }
            line++;
        }
    }

    ri.x = frame->scroll.col;
    ri.w = frame->scroll.col + w;
    for (size_t i = frame->scroll.line; i < last_line; i++) {
        move(frame->y + i - frame->scroll.line, frame->x + x);
        ri.line_i = i;
        ri.line = &buf->lines[i];
        render_line(&ri);
    }

    if (frame->x > 0) {
        set_highlight(stdscr, HI_VERT_SPLIT);
        mvvline(frame->y, frame->x, ACS_VLINE, frame->h);
    }
    perc = 100 * (frame->cur.line + 1) / buf->num_lines;

    set_highlight(stdscr, HI_STATUS);
    for (int i = orig_x; i < frame->w; i++) {
        mvaddch(frame->y + frame->h - 1, frame->x + i, ' ');
    }

    set_highlight(OffScreen, HI_STATUS);

    mvwprintw(OffScreen, 0, 0, " %s%s",
            buf->path == NULL ? "[No name]" : buf->path,
            buf->event_i == buf->save_event_i ?  "" : "[+]");
    w = getcurx(OffScreen);
    if (w + orig_x > frame->w) {
        w = frame->w - orig_x;
    }
    copywin(OffScreen, stdscr, 0, 0,
            frame->y + frame->h - 1, frame->x + orig_x,
            frame->y + frame->h - 1, frame->x + orig_x + w - 1, 0);

    mvwprintw(OffScreen, 0, 0, "%d%% ¶%zu/%zu☰℅%zu",
            perc, frame->cur.line + 1, buf->num_lines, frame->cur.col + 1);
    w = getcurx(OffScreen);
    if (w + orig_x > frame->w) {
        w = frame->w - orig_x;
    }
    copywin(OffScreen, stdscr, 0, 0,
            frame->y + frame->h - 1, frame->x + frame->w - w,
            frame->y + frame->h - 1, frame->x + frame->w - 1, 0);

    wattr_set(OffScreen, 0, 0, NULL);
}

void get_text_rect(const struct frame *frame,
        int *p_x, int *p_y, int *p_w, int *p_h)
{
    int x, w, h;
    int dg_cnt;
    size_t n;

    dg_cnt = 0;
    n = frame->buf->num_lines;
    while (n > 0) {
        dg_cnt++;
        n /= 10;
    }
    dg_cnt = MAX(dg_cnt, 3);

    w = MIN(frame->x + frame->w, COLS) - frame->x;
    h = MIN(frame->y + frame->h, LINES) - frame->y;

    x = frame->x > 0;
    if (w > 3 + dg_cnt) {
        x += 2 + dg_cnt;
    }

    *p_x = x;
    *p_y = 0;
    *p_w = MAX(w - x, 0);
    *p_h = MAX(h - 1, 0);
}

bool get_visual_cursor(const struct frame *frame, int *p_x, int *p_y)
{
    int x, y, w, h;

    get_text_rect(frame, &x, &y, &w, &h);

    *p_x = frame->x + x + frame->cur.col - frame->scroll.col;
    *p_y = frame->y + y + frame->cur.line - frame->scroll.line;

    return frame->cur.col >= frame->scroll.col &&
            frame->cur.line >= frame->scroll.line &&
            frame->cur.col - frame->scroll.col < (size_t) w &&
            frame->cur.line - frame->scroll.line < (size_t) h;
}
