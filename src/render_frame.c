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

#include "syntax/none.h"
#include "syntax/c.h"
#include "syntax/diff.h"
#include "syntax/commit.h"

size_t no_indentor(struct buf *buf, size_t line_i)
{
    return get_line_indent(buf, line_i);
}

struct lang Langs[] = {
    [NO_LANG] = { "none", none_lang_states, no_indentor, "\0" },
    [C_LANG] = { "C", c_lang_states, c_indentor, "c\0h\0cpp\0cxx\0c++\0hpp\0hxx\0h++\0" },
    [DIFF_LANG] = { "diff", diff_lang_states, no_indentor, "diff\0patch\0" },
    [COMMIT_LANG] = { "commit", commit_lang_states, no_indentor, "commit*\0" },
};

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

/**
 * Puts the visual position of given position into `p_x`, `p_y` and returns
 * whether the visual position is visible.
 */
static inline bool translate_pos(struct frame *frame, struct pos *pos,
        int x, int y, int w, int h, int *p_x, int *p_y)
{
    if (pos->col >= frame->scroll.col &&
            pos->line >= frame->scroll.line &&
            pos->col - frame->scroll.col < (size_t) w &&
            pos->line - frame->scroll.line < (size_t) h) {
        *p_x = frame->x + x + pos->col - frame->scroll.col;
        *p_y = frame->y + y + pos->line - frame->scroll.line;
        return true;
    }
    return false;
}

void render_frame(struct frame *frame)
{
    struct buf *buf;
    int perc;
    struct render_info ri;
    size_t line;
    size_t last_line;
    int x, y, w, h;
    int orig_x;
    size_t paren_i, match_i;
    struct pos p;
    int p_x, p_y;

    buf = frame->buf;

    /* get selection */
    ri.cur_line = &buf->lines[frame->cur.line];
    if (SelFrame != frame) {
        ri.sel_exists = false;
    } else {
        ri.sel_exists = get_selection(&ri.sel);
    }

    last_line = frame->scroll.line + frame->h - 1;
    last_line = MIN(last_line, frame->buf->num_lines);

    clean_lines(buf, last_line);

    orig_x = frame->x > 0;
    get_text_rect(frame, &x, &y, &w, &h);

    /* render line number view if there is enough space */
    if (x > 2) {
        set_highlight(stdscr, HI_LINE_NO);
        line = frame->scroll.line + 1;
        for (int i = y; i < h; i++) {
            if (line > buf->num_lines) {
                set_highlight(stdscr, HI_NORMAL);
                mvaddstr(frame->y + i, frame->x + orig_x, " ~");
                for (int j = orig_x + 2; j < x; j++) {
                    addch(' ');
                }
            } else {
                mvprintw(frame->y + i, frame->x + orig_x, " %*zu ",
                        x - orig_x - 2, line);
            }
            line++;
        }
    }

    /* render the lines */
    ri.x = frame->scroll.col;
    ri.w = frame->scroll.col + w;
    for (size_t i = frame->scroll.line; i < last_line; i++) {
        move(frame->y + i - frame->scroll.line, frame->x + x);
        ri.line_i = i;
        ri.line = &buf->lines[i];
        render_line(&ri);
    }

    /* highlight matching parentheses */
    if (frame == SelFrame) {
        paren_i = get_paren(buf, &frame->cur);
        if (paren_i != SIZE_MAX) {
            match_i = get_matching_paren(buf, paren_i);
            set_highlight(stdscr, match_i == SIZE_MAX ? HI_ERROR :
                          HI_PAREN_MATCH);
            p = buf->parens[paren_i].pos;
            if (translate_pos(frame, &p, x, y, w, h, &p_x, &p_y)) {
                mvaddch(p_y, p_x, buf->lines[p.line].s[p.col]);
            }
            if (match_i != SIZE_MAX) {
                p = buf->parens[match_i].pos;
                if (translate_pos(frame, &p, x, y, w, h, &p_x, &p_y)) {
                    mvaddch(p_y, p_x, buf->lines[p.line].s[p.col]);
                }
            }
        }
    }

    /* render vertical bar */
    if (frame->x > 0) {
        set_highlight(stdscr, HI_VERT_SPLIT);
        mvvline(frame->y, frame->x, ACS_VLINE, frame->h);
    }
    perc = 100 * (frame->cur.line + 1) / buf->num_lines;

    /* render the status in two parts:
     * 1. File name on the left
     * 2. File position on the right
     */
    set_highlight(stdscr, HI_STATUS);
    for (int i = orig_x; i < frame->w; i++) {
        mvaddch(frame->y + frame->h - 1, frame->x + i, ' ');
    }

    set_highlight(OffScreen, HI_STATUS);

    mvwprintw(OffScreen, 0, 0, " %s%s",
              get_pretty_path(buf->path),
              buf->event_i == buf->save_event_i ?  "" : "[+]");
    w = getcurx(OffScreen);
    if (w + orig_x > frame->w) {
        w = frame->w - orig_x;
    }
    copywin(OffScreen, stdscr, 0, 0,
            frame->y + frame->h - 1, frame->x + orig_x,
            frame->y + frame->h - 1, frame->x + orig_x + w - 1, 0);

    mvwprintw(OffScreen, 0, 0, "%d%% ¶%zu/%zu☰℅%zu",
              perc, frame->cur.line + 1, buf->num_lines,
              frame->cur.col + 1);
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
