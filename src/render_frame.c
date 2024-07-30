#include "frame.h"
#include "util.h"
#include "buf.h"

#include <ncurses.h>

void render_frame(struct frame *frame)
{
    struct buf *buf;
    struct line *line;
    int perc;

    buf = frame->buf;
    for (size_t i = frame->scroll.line; i < MIN(buf->num_lines,
                (size_t) (frame->scroll.line + frame->h)); i++) {
        line = &buf->lines[i];
        mvaddnstr(i - frame->scroll.line, 0, line->s, line->n);
    }
    move(frame->y + frame->h, 0);
    clrtoeol();
    move(frame->y + frame->h, 0);
    perc = 100 * (frame->cur.line + 1) / SelFrame->buf->num_lines;
    printw("%s %d%% ¶%zu/%zu☰℅%zu", buf->path == NULL ? "[No name]" : buf->path,
            perc, frame->cur.line + 1, SelFrame->buf->num_lines,
            frame->cur.col + 1);
}
