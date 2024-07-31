#include "buf.h"
#include "frame.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>

#define STATE_NULL 0
#define STATE_START 1
#define STATE_WORD 2

/**
 * Simply highlight all words in bold.
 */
static void highlight_line(struct frame *frame, struct line *line, size_t state)
{
    size_t i = 0;
    char ch;

    line->attribs = xreallocarray(line->attribs, line->n,
            sizeof(*line->attribs));
    memset(line->attribs, 0, sizeof(*line->attribs) * line->n);

    (void) frame;
    //fprintf(stderr, "highlighting line no. %zu {%.*s}\n", line - frame->buf->lines, (int) line->n, line->s);

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
static void render_line(struct frame *frame, struct line *line)
{
    struct line *prev_line;
    struct line *cur_line;
    int w = 0;
    struct attr *a;

    if (line == frame->buf->lines) {
        prev_line = NULL;
    } else {
        prev_line = line - 1;
    }

    if (line->state == 0) {
        if (prev_line == NULL) {
            highlight_line(frame, line, STATE_START);
        } else {
            highlight_line(frame, line,
                    prev_line->state == 0 ? STATE_START : prev_line->state);
        }
    }

    cur_line = &frame->buf->lines[frame->cur.line];
    for (size_t i = 0; i < line->n && w != frame->w;) {
        a = &line->attribs[i];
        attr_set(a->a, a->cp, NULL);
        if (a->cc == NULL || line == cur_line) {
            addch(line->s[i]);
            i++;
        } else {
            addstr(a->cc);
            i += 2;
        }
        w++;
    }
}

void render_frame(struct frame *frame)
{
    struct buf *buf;
    int perc;

    buf = frame->buf;
    for (size_t i = frame->scroll.line; i < MIN(buf->num_lines,
                (size_t) (frame->scroll.line + frame->h)); i++) {
        move(i - frame->scroll.line, 0);
        render_line(frame, &buf->lines[i]);
    }

    move(frame->y + frame->h, 0);
    clrtoeol();
    move(frame->y + frame->h, 0);
    perc = 100 * (frame->cur.line + 1) / SelFrame->buf->num_lines;
    printw("%s %d%% ¶%zu/%zu☰℅%zu", buf->path == NULL ? "[No name]" : buf->path,
            perc, frame->cur.line + 1, SelFrame->buf->num_lines,
            frame->cur.col + 1);
}
