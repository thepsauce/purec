#include "xalloc.h"
#include "frame.h"
#include "buf.h"
#include "util.h"
#include "purec.h"

#include <ctype.h>
#include <string.h>
#include <ncurses.h>

/**
 * Checks if any events between the event at `Core.ev_from_ins` and the last
 * event can be combined and combines them by setting the `IS_TRANSIENT` flag.
 */
static void attempt_join(void)
{
    struct undo_event *prev_ev, *ev;

    for (size_t i = Core.ev_from_ins + 1; i < SelFrame->buf->event_i; i++) {
        prev_ev = &SelFrame->buf->events[i - 1];
        ev = &SelFrame->buf->events[i];
        if (should_join(prev_ev, ev)) {
            prev_ev->flags |= IS_TRANSIENT;
        }
    }
}

static void repeat_last_insertion(void)
{
    struct buf *buf;
    struct pos cur;
    struct undo_event *ev;
    struct raw_line *lines = NULL, *line;
    size_t num_lines = 0;
    struct undo_seg *seg;
    size_t orig_col;
    size_t repeat;

    buf = SelFrame->buf;

    if ((Core.counter <= 1 && Core.move_down_count == 0) ||
            Core.ev_from_ins == buf->event_i) {
        return;
    }

    /* check if there is only a SINGLE transient chain */
    for (size_t i = Core.ev_from_ins + 1; i < buf->event_i; i++) {
        ev = &buf->events[i - 1];
        if (!(ev->flags & IS_TRANSIENT)) {
            return;
        }
    }

    /* TODO: re write this!! it crashes!! */
    /* combine the events (we only expect insertion and deletion events) */
    cur = buf->events[Core.ev_from_ins].pos;
    for (size_t i = Core.ev_from_ins; i < buf->event_i; i++) {
        ev = &buf->events[i];
        seg = ev->seg;
        load_undo_data(seg);
        if ((ev->flags & IS_INSERTION)) {
            /* join the current and the event text */
            if (num_lines == 0) {
                lines = xreallocarray(NULL, seg->num_lines, sizeof(*lines));
                num_lines = seg->num_lines;
                for (size_t i = 0; i < num_lines; i++) {
                    init_raw_line(&lines[i], seg->lines[i].s,
                            seg->lines[i].n);
                }
                unload_undo_data(seg);
                continue;
            }
            lines = xreallocarray(lines, num_lines + seg->num_lines - 1,
                    sizeof(*lines));
            if (is_point_equal(&cur, &ev->pos)) {
                line = &lines[0];
                line->s = xrealloc(line->s, line->n + seg->lines[0].n);
                memmove(&line->s[seg->lines[seg->num_lines - 1].n],
                        &line->s[0], line->n);
                memcpy(&line->s[0], &seg->lines[seg->num_lines - 1].s[0],
                        seg->lines[seg->num_lines - 1].n);
                line->n += seg->lines[seg->num_lines - 1].n;

                memmove(&lines[seg->num_lines - 1], &lines[0],
                        sizeof(*lines) * num_lines);
                for (size_t i = 1; i < seg->num_lines; i++) {
                    init_raw_line(&lines[i - 1], seg->lines[i].s,
                            seg->lines[i].n);
                }
            } else {
                line = &lines[num_lines - 1];

                line->s = xrealloc(line->s, line->n + seg->lines[0].n);
                memcpy(&line->s[line->n], &seg->lines[0].s[0],
                        seg->lines[0].n);
                line->n += seg->lines[0].n;

                for (size_t i = 0; i < seg->num_lines - 1; i++) {
                    line++;
                    init_raw_line(line, seg->lines[i].s, seg->lines[i].n);
                }
            }
            num_lines += seg->num_lines - 1;
        } else {
            line = &lines[ev->pos.line - cur.line];
            line->n -= seg->lines[0].n;
            if (seg->num_lines == 1) {
                memmove(&line->s[ev->pos.col - cur.col],
                        &line->s[ev->pos.col - cur.col + seg->lines[0].n],
                        line->n - (ev->pos.col - cur.col));
            } else {
                line->s = xrealloc(line->s, line->n + line[seg->num_lines - 1].n);
                memcpy(&lines->s[line->n], &line[seg->num_lines - 1].s[0],
                        line[seg->num_lines - 1].n);
                line->n += line[seg->num_lines - 1].n -
                    seg->lines[seg->num_lines - 1].n;
                num_lines -= seg->num_lines - 1;
                memmove(&line[1], &line[seg->num_lines], sizeof(*line) *
                        (num_lines - 1 - (lines - line)));
            }
        }
        unload_undo_data(seg);
    }

    orig_col = cur.col;

    if (num_lines == 1) {
        cur.col += lines[0].n;
    } else {
        cur.line += num_lines - 1;
        cur.col = lines[num_lines - 1].n;
    }
    if (!is_point_equal(&SelFrame->cur, &cur) ||
            (num_lines == 1 && lines[0].n == 0)) {
        goto end;
    }

    buf->events[buf->event_i - 1].flags |= IS_TRANSIENT;
    repeat = Core.counter - 1;
    for (size_t i = 0; i <= Core.move_down_count; i++, cur.line++) {
        if (cur.line >= SelFrame->buf->num_lines) {
            break;
        }
        if (orig_col > SelFrame->buf->lines[cur.line].n) {
            continue;
        }

        cur.col = orig_col;
        ev = insert_lines(SelFrame->buf, &cur, lines, num_lines, repeat);

        if (num_lines == 1) {
            cur.col += lines[0].n;
        } else {
            cur.line += repeat * (num_lines - 1);
            cur.col = lines[num_lines - 1].n;
        }
        repeat = Core.counter;
    }
    cur.line--;

    set_cursor(SelFrame, &cur);

end:
    for (size_t i = 0; i < num_lines; i++) {
        free(lines[i].s);
    }
    free(lines);
}

int insert_handle_input(int c)
{
    struct buf          *buf;
    char                ch;
    struct undo_event   *ev;
    size_t              n;
    struct pos          old_cur;
    struct raw_line     lines[2];
    struct line         *line;
    size_t              amount;
    size_t              i;

    buf = SelFrame->buf;
    ch = c;
    switch (c) {
    case '\x1b':
        attempt_join();
        repeat_last_insertion();
        Core.last_insert.buf = buf;
        Core.last_insert.pos = SelFrame->cur;
        if (SelFrame->cur.col > 0) {
            SelFrame->cur.col--;
            (void) adjust_scroll(SelFrame);
        }
        SelFrame->vct = SelFrame->cur.col;
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case CONTROL('C'):
        attempt_join();
        if (SelFrame->cur.col > 0) {
            SelFrame->cur.col--;
            (void) adjust_scroll(SelFrame);
        }
        SelFrame->vct = SelFrame->cur.col;
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case '\n':
        if (buf->ev_last_indent + 1 == buf->event_i) {
            undo_event_no_trans(buf);
            /* since the indentation is now trimmed, move to the first column */
            SelFrame->cur.col = 0;
        }
        ev = break_line(buf, &SelFrame->cur);
        ev->cur = SelFrame->cur;
        set_cursor(SelFrame, &buf->events[buf->event_i - 1].end);
        return UPDATE_UI;

    case '\t':
        ch = ' ';
        n = Core.tab_size - SelFrame->cur.col % Core.tab_size;
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, n);
        ev->cur = SelFrame->cur;
        SelFrame->cur.col += n;
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;

    case KEY_DC:
        old_cur = SelFrame->cur;
        if (old_cur.col == buf->lines[old_cur.line].n) {
            if (old_cur.line == buf->num_lines - 1) {
                return 0;
            }
            old_cur.line++;
            old_cur.col = 0;
        } else {
            old_cur.col++;
        }
        ev = delete_range(buf, &SelFrame->cur, &old_cur);
        ev->cur = SelFrame->cur;
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;

    case 0x7f:
    case KEY_BACKSPACE:
    case '\b':
        old_cur = SelFrame->cur;
        if (old_cur.col == 0) {
            if (old_cur.line == 0) {
                return 0;
            }
            SelFrame->cur.line--;
            SelFrame->cur.col = buf->lines[SelFrame->cur.line].n;
        } else {
            if (old_cur.col % Core.tab_size == 0) {
                amount = Core.tab_size;
                line = &buf->lines[old_cur.line];
                for (i = old_cur.col; i > old_cur.col - Core.tab_size; ) {
                    i--;
                    if (line->s[i] != ' ') {
                        amount = 1;
                        break;
                    }
                }
            } else {
                amount = 1;
            }
            SelFrame->cur.col -= amount;
        }
        ev = delete_range(buf, &SelFrame->cur, &old_cur);
        ev->cur = old_cur;
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;
    }

    if (c >= ' ' && c < 0x7f) {
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, 1);
        ev->cur = SelFrame->cur;
        SelFrame->cur.col++;
        if (SelFrame->cur.col == buf->lines[SelFrame->cur.line].n &&
                ch == '}') {
            ev = indent_line(buf, SelFrame->cur.line);
            if (ev != NULL) {
                ev->cur = SelFrame->cur;
            }
            SelFrame->cur.col = buf->lines[SelFrame->cur.line].n;
        }
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;
    }
    return do_motion(SelFrame, c);
}
