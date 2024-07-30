#include "frame.h"
#include "util.h"
#include "buf.h"

#include <ncurses.h>

void render_frame(struct frame *frame)
{
    struct line *line;

    for (size_t i = 0; i < MIN(frame->buf->num_lines, (size_t) frame->h); i++) {
        line = &frame->buf->lines[i + frame->scroll.line];
        mvaddnstr(i, 0, line->s, line->n);
    }
    move(frame->y + frame->h, 0);
    clrtoeol();
    move(frame->y + frame->h, 0);
    printw("%zu/%zu:%zu", frame->cur.line + 1, SelFrame->buf->num_lines,
            frame->cur.col + 1);
}
