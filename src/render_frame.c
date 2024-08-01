#include "buf.h"
#include "frame.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>

struct render_info {
    struct frame *frame;
    struct line *cur_line;

    struct line *prev_line;
    struct line *line;
    size_t line_i;

    struct pos sel_beg;
    struct pos sel_end;
    bool has_sel;
};

#define STATE_NULL 0
#define STATE_START 1
#define STATE_WORD 2

/**
 * Simply highlight all words in bold.
 */
static void highlight_line(struct render_info *ri, size_t state)
{
    struct line *line;
    size_t i;
    char ch;

    line = ri->line;

    line->attribs = xreallocarray(line->attribs, line->n,
            sizeof(*line->attribs));
    memset(line->attribs, 0, sizeof(*line->attribs) * line->n);

    for (i = 0; i < line->n; i++) {
        ch = line->s[i];
        switch (state) {
        case STATE_START:
            if (isalpha(ch) || ch == '_') {
                state = STATE_WORD;
            }
            break;

        case STATE_WORD:
            if (isalnum(ch) || ch == '_') {
                state = STATE_WORD;
            } else {
                state = STATE_START;
            }
            break;
        }
        if (state == STATE_WORD) {
            line->attribs[i].cp = 0;
            line->attribs[i].a = A_BOLD;
            line->attribs[i].cc = NULL;
        }
    }

    state = STATE_START;

    line->state = state;
}

/**
 * Updates line highlighting and renders the line.
 */
static void render_line(struct render_info *ri)
{
    struct line *line;
    int w = 0;
    struct attr *a;
    struct pos p;

    line = ri->line;
    if (line->state == 0) {
        if (ri->line_i == 0) {
            highlight_line(ri, STATE_START);
        } else {
            highlight_line(ri, ri->prev_line->state == 0 ? STATE_START :
                    ri->prev_line->state);
        }
    }

    p.col = 0;
    p.line = ri->line_i;

    for (p.col = 0; p.col < line->n && w != ri->frame->w;) {
        a = &line->attribs[p.col];
        if (ri->has_sel && is_in_range(&p, &ri->sel_beg, &ri->sel_end)) {
            attr_set(a->a ^ A_REVERSE, 0, NULL);
        } else {
            attr_set(a->a, a->cp, NULL);
        }
        if (a->cc == NULL || line == ri->cur_line) {
            addch(line->s[p.col]);
            p.col++;
        } else {
            addstr(a->cc);
            p.col += 2;
        }
        w++;
    }
    if (ri->has_sel && is_in_range(&p, &ri->sel_beg, &ri->sel_end)) {
        attr_set(A_REVERSE, 0, NULL);
        addch(' ');
    }
}

void render_frame(struct frame *frame)
{
    struct buf *buf;
    int perc;
    struct render_info ri;

    buf = frame->buf;

    ri.frame = frame;
    ri.cur_line = &buf->lines[frame->cur.line];
    ri.prev_line = frame->scroll.line == 0 ? NULL :
        &buf->lines[frame->scroll.line - 1];

    ri.has_sel = Mode.type == VISUAL_MODE && frame == SelFrame;
    if (ri.has_sel) {
        ri.sel_beg = frame->cur;
        ri.sel_end = Mode.pos;
        sort_positions(&ri.sel_beg, &ri.sel_end);
    }

    for (size_t i = frame->scroll.line; i < MIN(buf->num_lines,
                (size_t) (frame->scroll.line + frame->h)); i++) {
        move(i - frame->scroll.line, 0);
        ri.line_i = i;
        ri.line = &buf->lines[i];
        render_line(&ri);
        ri.prev_line = ri.line;
    }

    attr_set(0, 0, NULL);
    move(frame->y + frame->h, 0);
    clrtoeol();
    move(frame->y + frame->h, 0);
    perc = 100 * (frame->cur.line + 1) / SelFrame->buf->num_lines;
    printw("%s %d%% ¶%zu/%zu☰℅%zu", buf->path == NULL ? "[No name]" : buf->path,
            perc, frame->cur.line + 1, SelFrame->buf->num_lines,
            frame->cur.col + 1);
}
