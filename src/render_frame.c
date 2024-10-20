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

    /// the offset in x direction of the rendering
    int off_x;
    /// the offset in y direction of the rendering
    int off_y;

    /// the origin of the line rendering
    col_t x;
    /// the maximum width of the line rendering
    col_t w;
    /// the line the cursor is on
    struct line *cur_line;

    /// the line to render
    struct line *line;
    /// the index of the line to render
    line_t line_i;
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
#include "syntax/make.h"

col_t no_indentor(struct buf *buf, line_t line_i)
{
    return get_line_indent(buf, line_i);
}

struct lang Langs[] = {
    [NO_LANG] = { "None", none_lang_states, no_indentor, "\0" },
    [C_LANG] = { "C", c_lang_states, c_indentor,
                "*.c\0*.h\0*.cpp\0*.cxx\0*.c++\0*.hpp\0*.hxx\0*.h++\0" },
    [DIFF_LANG] = { "Diff", diff_lang_states, no_indentor, "*.diff\0*.patch\0" },
    [COMMIT_LANG] = { "Commit", commit_lang_states, no_indentor, "\\i*.commit*\0" },
    [MAKE_LANG] = { "Make", make_lang_states, make_indentor,
                   "makefile\0Makefile\0GNUmakefile\0" },
};

/**
 * Renders a line using its cached attribute data.
 *
 * @param ri    Render information.
 */
static void render_line(struct render_info *ri)
{
    col_t           x;
    int             hi;
    struct pos      p;
    struct glyph    g;
    bool            err;
    int             c;

    p.line = ri->line_i;
    for (p.col = 0, x = 0; p.col < ri->line->n && x < ri->w;) {
        err = get_glyph(&ri->line->s[p.col], ri->line->n - p.col, &g) == -1;
        if (x + g.w > ri->x) {
            hi = ri->line->attribs[p.col];
            c = ri->line->s[p.col];
            if (c == '\t') {
                p.col++;
                x += Core.tab_size - x % Core.tab_size;
                continue;
            }

            if (x < ri->x) {
                set_highlight(stdscr, HI_COMMENT);
                mvaddch(ri->off_y, ri->off_x + ri->x, '<');
                p.col += g.n;
                x = ri->x + 1;
                continue;
            }

            if (x + g.w > ri->w) {
                set_highlight(stdscr, HI_COMMENT);
                mvaddch(ri->off_y, ri->off_x + x, '>');
                break;
            }

            if (err) {
                set_highlight(stdscr, HI_COMMENT);
                mvaddch(ri->off_y, ri->off_x + x, '?');
            } else if ((c >= '\0' && c < ' ') || c == 0x7f) {
                set_highlight(stdscr, HI_COMMENT);
                mvaddch(ri->off_y, ri->off_x + x, '^');
                addch(c == 0x7f ? '?' : c + '@');
            } else {
                set_highlight(stdscr, hi);
                mvaddnstr(ri->off_y, ri->off_x + x, &ri->line->s[p.col], g.n);
            }
        }
        p.col += g.n;
        x += g.w;
    }
}

void render_frame(struct frame *frame)
{
    struct buf          *buf;
    int                 perc;
    struct render_info  ri;
    line_t              line;
    int                 i, j;
    line_t              l;
    line_t              last_line;
    int                 x, y, w, h;
    int                 orig_x;
    struct match        *match;
    struct selection    sel;
    col_t               start, end;
    size_t              paren_i, match_i;
    struct pos          p;
    int                 p_x, p_y;
    int                 hi;

    buf = frame->buf;

    orig_x = frame->x > 0;
    get_text_rect(frame, &x, &y, &w, &h);

    /* render line number view if there is enough space */
    if (x > 2) {
        set_highlight(stdscr, HI_LINE_NO);
        line = frame->scroll.line + 1;
        for (i = y; i < h; i++) {
            if (line > buf->num_lines) {
                set_highlight(stdscr, HI_NORMAL);
                mvaddstr(frame->y + i, frame->x + orig_x, " ~");
                for (j = orig_x + 2; j < x; j++) {
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
    ri.cur_line = &buf->lines[frame->cur.line];
    ri.off_x = frame->x + x - frame->scroll.col;
    ri.x = frame->scroll.col;
    ri.w = frame->scroll.col + w;

    last_line = frame->scroll.line + frame->h - 1;
    last_line = MIN(last_line, buf->num_lines);
    for (l = frame->scroll.line; l < last_line; l++) {
        ri.off_y = frame->y + l - frame->scroll.line;
        ri.line_i = l;
        ri.line = &buf->lines[l];
        render_line(&ri);
    }

    /* overlay with matches */
    match_i = get_match_line(buf, frame->scroll.line);
    for (match = &buf->matches[match_i];
            match < &buf->matches[buf->num_matches] &&
                match->from.line < last_line;
            match++) {
        if (match->from.col >= ri.w) {
            break;
        }
        mvchgat(frame->y + match->from.line - frame->scroll.line,
                frame->x + x + match->from.col - frame->scroll.col,
                MIN((int) (match->to.col - match->from.col), w),
                get_attrib_of(HI_SEARCH), HI_SEARCH, NULL);
    }

    if (frame == SelFrame) {
        /* render the selection if it exists */
        if (get_selection(&sel)) {
            start = sel.beg.col;
            for (l = MAX(sel.beg.line, frame->scroll.line);
                 l <= sel.end.line && l < last_line;
                 l++) {
                if (sel.is_block) {
                    start = sel.beg.col;
                    end = MIN(sel.end.col + 1, buf->lines[l].n);
                } else {
                    end = l == sel.end.line ? sel.end.col + 1 :
                        buf->lines[l].n + 1;
                }
                if (start < frame->scroll.col) {
                    start = frame->scroll.col;
                }
                if (end <= start) {
                    continue;
                }
                mvchgat(frame->y + l - frame->scroll.line,
                        frame->x + x + start - frame->scroll.col,
                        MIN((int) (end - start), w),
                        get_attrib_of(HI_VISUAL), HI_VISUAL, NULL);
                start = 0;
            }
        }
        /* highlight matching parentheses */
        paren_i = get_paren(buf, &frame->cur);
        if (paren_i != SIZE_MAX) {
            match_i = get_matching_paren(buf, paren_i);
            hi = match_i == SIZE_MAX ? HI_ERROR : HI_PAREN_MATCH;
            p = buf->parens[paren_i].pos;
            if (get_visual_pos(frame, &p, &p_x, &p_y)) {
                mvchgat(p_y, p_x, 1, get_attrib_of(hi), hi, NULL);
            }
            if (match_i != SIZE_MAX) {
                p = buf->parens[match_i].pos;
                if (get_visual_pos(frame, &p, &p_x, &p_y)) {
                    mvchgat(p_y, p_x, 1, get_attrib_of(hi), hi, NULL);
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
    for (i = orig_x; i < frame->w; i++) {
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

    mvwprintw(OffScreen, 0, 0, "%d%% ¶"PRLINE"/"PRLINE"☰℅"PRCOL,
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
    int             x, w, h;
    int             dg_cnt;
    line_t          n;

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

bool get_visual_pos(const struct frame *frame, const struct pos *pos,
                    int *p_x, int *p_y)
{
    int             x, y, w, h;
    struct line     *line;
    size_t          v_x;

    get_text_rect(frame, &x, &y, &w, &h);

    line = &frame->buf->lines[pos->line];
    v_x = get_advance(line->s, line->n, pos->col);
    *p_x = frame->x + x + v_x - frame->scroll.col;
    *p_y = frame->y + y + pos->line - frame->scroll.line;

    return pos->col >= frame->scroll.col &&
            pos->line >= frame->scroll.line &&
            pos->col - frame->scroll.col < (col_t) w &&
            pos->line - frame->scroll.line < (line_t) h;
}
