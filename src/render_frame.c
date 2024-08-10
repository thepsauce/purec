#include "buf.h"
#include "frame.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>

struct render_info {
    /// the language to use
    size_t lang;

    /// the width of the rendering
    int w;
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

struct state_ctx {
    /// the selected language
    size_t lang;
    /// the current highlight group
    unsigned hi;
    /// the current state
    unsigned state;
    /// if the current state is finished and wants to continue to the next line
    bool multi;
    /// the current line
    char *s;
    /// the index on the current line
    size_t i;
    /// the length of the current line
    size_t n;
};

#define HI_NORMAL       0
#define HI_COMMENT      1
#define HI_JAVADOC      2
#define HI_TYPE         3
#define HI_TYPE_MOD     4
#define HI_IDENTIFIER   5
#define HI_NUMBER       6
#define HI_STRING       7
#define HI_CHAR         8
#define HI_PREPROC      9
#define HI_OPERATOR     10
#define HI_ERROR        11

#define STATE_NULL      0
#define STATE_START     1

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

/// if the state always wants to continue to the next line
#define STATE_MULTI_LINE 0x1

struct lang_state {
    /// flags of the state
    int flags;
    /// the procedure that handles this state
    unsigned (*proc)(struct state_ctx *ctx);
};

#include "highlight_c.h"

static const struct {
    /// the color pair
    int cp;
    /// the attributes
    int a;
} hi_styles[] = {
    [HI_NORMAL] = { 0, A_NORMAL },
    [HI_COMMENT] = { 0, A_BOLD },
    [HI_JAVADOC] = { 0, A_BOLD | A_ITALIC },
    [HI_TYPE] = { 0, A_BOLD },
    [HI_TYPE_MOD] = { 0, A_ITALIC },
    [HI_IDENTIFIER] = { 0, A_BOLD },
    [HI_NUMBER] = { 0, A_ITALIC },
    [HI_STRING] = { 0, A_BOLD },
    [HI_CHAR] = { 0, A_ITALIC },
    [HI_PREPROC] = { 0, A_UNDERLINE },
    [HI_OPERATOR] = { 0, A_BOLD },
    [HI_ERROR] = { 0, A_REVERSE },
};

struct lang {
    struct lang_state *states;
    const char *file_ext;
} langs[] = {
    { c_lang_states, "c\0h\0cpp\0c++\0hpp\0h++\0" },
};

/**
 * Highlight a line with syntax highlighting.
 *
 * @param ri    Render information.
 * @param state The starting state.
 */
static void highlight_line(struct render_info *ri, size_t state)
{
    struct line *line;
    struct state_ctx ctx;
    size_t n;

    line = ri->line;

    line->attribs = xreallocarray(line->attribs, line->n,
            sizeof(*line->attribs));
    memset(line->attribs, 0, sizeof(*line->attribs) * line->n);

    ctx.lang = 0;
    ctx.state = state;
    ctx.multi = false;
    ctx.hi = HI_NORMAL;
    ctx.s = line->s;
    ctx.i = 0;
    ctx.n = line->n;

    for (ctx.i = 0; ctx.i < ctx.n; ) {
        n = (*langs[ctx.lang].states[ctx.state].proc)(&ctx);
        for (; n > 0; n--) {
            line->attribs[ctx.i].cp = hi_styles[ctx.hi].cp;
            line->attribs[ctx.i].a = hi_styles[ctx.hi].a;
            ctx.i++;
        }
    }

    if (!ctx.multi && !(langs[ctx.lang].states[ctx.state].flags &
                STATE_MULTI_LINE)) {
        ctx.state = STATE_START;
    }

    line->state = ctx.state;
}

/**
 * Renders a line using its cached attribute data.
 *
 * @param ri    Render information.
 */
static void render_line(struct render_info *ri)
{
    int w = 0;
    struct attr *a;
    struct pos p;

    p.line = ri->line_i;
    for (p.col = 0; p.col < ri->line->n && w != ri->w;) {
        a = &ri->line->attribs[p.col];
        if (ri->sel_exists && is_in_selection(&ri->sel, &p)) {
            attr_set(a->a ^ A_REVERSE, 0, NULL);
        } else {
            attr_set(a->a, a->cp, NULL);
        }
        addch(ri->line->s[p.col]);
        p.col++;
        w++;
    }
    if (ri->sel_exists && is_in_selection(&ri->sel, &p)) {
        attr_set(A_REVERSE, 0, NULL);
        addch(' ');
    }
}

void render_frame(struct frame *frame)
{
    struct buf *buf;
    int perc;
    struct render_info ri;
    int x;
    size_t last_line;
    size_t prev_state;

    buf = frame->buf;

    /* correcting for the vertical separator */
    if (frame->x == 0) {
        x = 0;
        ri.w = frame->w;
    } else {
        x = frame->x + 1;
        ri.w = frame->w - 1;
    }
    ri.cur_line = &buf->lines[frame->cur.line];
    if (SelFrame != frame) {
        ri.sel_exists = false;
    } else {
        ri.sel_exists = get_selection(&ri.sel);
    }

    last_line = MIN(buf->num_lines,
            (size_t) (frame->scroll.line + frame->h - 1));

    prev_state = buf->min_dirty_i == 0 ? STATE_START :
        buf->min_dirty_i == SIZE_MAX ? 0 :
        buf->lines[buf->min_dirty_i - 1].state;
    for (size_t i = buf->min_dirty_i; i < last_line; i++) {
        ri.line_i = i;
        ri.line = &buf->lines[i];
        if (ri.line->state == 0) {
            highlight_line(&ri, prev_state);
            if ((ri.line->state != STATE_START ||
                    ri.line->prev_state != STATE_START) &&
                    i + 1 != buf->num_lines) {
                mark_dirty(&ri.line[1]);
            }
        } else if (i > buf->max_dirty_i) {
            break;
        }
        prev_state = ri.line->state;
    }

    if (last_line > buf->max_dirty_i) {
        buf->min_dirty_i = SIZE_MAX;
        buf->max_dirty_i = 0;
    } else {
        buf->min_dirty_i = MAX(buf->min_dirty_i, last_line);
    }

    for (size_t i = frame->scroll.line; i < last_line; i++) {
        move(frame->y + i - frame->scroll.line, x);
        ri.line_i = i;
        ri.line = &buf->lines[i];
        render_line(&ri);
    }

    attr_set(0, 0, NULL);
    if (frame->x > 0) {
        mvvline(frame->y, frame->x, ACS_VLINE, frame->h);
    }
    perc = 100 * (frame->cur.line + 1) / buf->num_lines;
    mvprintw(frame->y + frame->h - 1, x, "%s%s %d%% ¶%zu/%zu☰℅%zu",
            buf->path == NULL ? "[No name]" : buf->path,
            buf->path == NULL || buf->event_i == buf->save_event_i ?
                "" : "[+]",
            perc, frame->cur.line + 1, buf->num_lines, frame->cur.col + 1);
}
