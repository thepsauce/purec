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
    size_t              i;
    struct undo_event   *prev_ev, *ev;

    for (i = Core.ev_from_insert + 1; i < SelFrame->buf->event_i; i++) {
        prev_ev = &SelFrame->buf->events[i - 1];
        ev = &SelFrame->buf->events[i];
        if (should_join(prev_ev, ev)) {
            prev_ev->flags |= IS_TRANSIENT;
        }
    }
}

static void repeat_last_insertion(void)
{
    struct buf          *buf;
    size_t              e;
    line_t              i;
    struct undo_event   *ev;
    struct pos          cur;
    struct raw_line     *lines = NULL, *line;
    line_t              num_lines = 0;
    struct undo_seg     *seg;
    col_t               orig_col;
    size_t              repeat;

    buf = SelFrame->buf;

    if ((Core.counter <= 1 && Core.move_down_count == 0) ||
            Core.ev_from_insert == buf->event_i) {
        return;
    }

    /* check if there is only a SINGLE transient chain */
    for (e = Core.ev_from_insert + 1; e < buf->event_i; e++) {
        ev = &buf->events[e - 1];
        if (!(ev->flags & IS_TRANSIENT)) {
            return;
        }
    }

    /* combine the events (we only expect insertion and deletion events) */
    cur = buf->events[Core.ev_from_insert].pos;
    for (e = Core.ev_from_insert; e < buf->event_i; e++) {
        ev = &buf->events[e];
        seg = ev->seg;
        load_undo_data(seg);
        if ((ev->flags & IS_INSERTION)) {
            /* join the current and the event text */
            if (num_lines == 0) {
                lines = xreallocarray(NULL, seg->num_lines, sizeof(*lines));
                num_lines = seg->num_lines;
                for (i = 0; i < num_lines; i++) {
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
                for (i = 1; i < seg->num_lines; i++) {
                    init_raw_line(&lines[i - 1], seg->lines[i].s,
                            seg->lines[i].n);
                }
            } else {
                line = &lines[num_lines - 1];

                line->s = xrealloc(line->s, line->n + seg->lines[0].n);
                memcpy(&line->s[line->n], &seg->lines[0].s[0],
                        seg->lines[0].n);
                line->n += seg->lines[0].n;

                for (i = 0; i < seg->num_lines - 1; i++) {
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
    repeat = Core.repeat_insert - 1;
    for (i = 0; i <= Core.move_down_count; i++, cur.line++) {
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
        repeat = Core.repeat_insert;
    }
    cur.line--;

    set_cursor(SelFrame, &cur);

end:
    for (i = 0; i < num_lines; i++) {
        free(lines[i].s);
    }
    free(lines);
}

int insert_handle_input(int c)
{
    struct buf          *buf;
    char                ch;
    size_t              n;
    struct raw_line     lines[2];
    struct line         *line;
    col_t               i;

    buf = SelFrame->buf;
    ch = c;
    switch (c) {
    case '\x1b':
        attempt_join();
        repeat_last_insertion();
        Core.last_insert.buf = buf;
        Core.last_insert.pos = SelFrame->cur;
        (void) do_motion(SelFrame, KEY_LEFT);
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case CONTROL('C'):
        attempt_join();
        (void) do_motion(SelFrame, KEY_LEFT);
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case '\n':
        if (buf->ev_last_indent + 1 == buf->event_i) {
            undo_event_no_trans(buf);
            /* since the indentation is now trimmed, move to the first column */
            SelFrame->cur.col = 0;
        }
        (void) break_line(buf, &SelFrame->cur);
        set_cursor(SelFrame, &buf->events[buf->event_i - 1].end);
        return UPDATE_UI;

    case '\t':
        ch = ' ';
        n = Core.tab_size - SelFrame->cur.col % Core.tab_size;
        lines[0].s = &ch;
        lines[0].n = 1;
        (void) insert_lines(buf, &SelFrame->cur, lines, 1, n);
        SelFrame->cur.col += n;
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;

    case KEY_DC:
        if (SelFrame->cur.col == buf->lines[SelFrame->cur.line].n) {
            if (SelFrame->cur.line == buf->num_lines - 1) {
                return 0;
            }
            SelFrame->next_cur.line = SelFrame->cur.line + 1;
            SelFrame->next_cur.col = 0;
            SelFrame->next_vct = 0;
        } else {
            (void) prepare_motion(SelFrame, KEY_RIGHT);
        }
        (void) delete_range(buf, &SelFrame->cur, &SelFrame->next_cur);
        SelFrame->vct = SelFrame->next_vct;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;

    case 0x7f:
    case KEY_BACKSPACE:
    case '\b':
        SelFrame->next_cur = SelFrame->cur;
        if (SelFrame->cur.col == 0) {
            if (SelFrame->cur.line == 0) {
                return 0;
            }
            SelFrame->next_cur.line--;
            SelFrame->next_cur.col = buf->lines[SelFrame->next_cur.line].n;
            SelFrame->next_vct = compute_vct(SelFrame, &SelFrame->next_cur);
        } else {
            if (SelFrame->cur.col % Core.tab_size == 0) {
                Core.counter = Core.tab_size;
                line = &buf->lines[SelFrame->cur.line];
                for (i = SelFrame->cur.col;
                     i > SelFrame->cur.col - Core.tab_size; ) {
                    i--;
                    if (line->s[i] != ' ') {
                        Core.counter = 1;
                        break;
                    }
                }
            } else {
                Core.counter = 1;
            }
            (void) prepare_motion(SelFrame, KEY_LEFT);
        }
        (void) delete_range(buf, &SelFrame->next_cur, &SelFrame->cur);
        SelFrame->cur = SelFrame->next_cur;
        SelFrame->vct = SelFrame->next_vct;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;
    }

    if (c >= ' ' && c < 0x100) {
        lines[0].s = &ch;
        lines[0].n = 1;
        (void) insert_lines(buf, &SelFrame->cur, lines, 1, 1);
        SelFrame->cur.col++;
        if (SelFrame->cur.col == buf->lines[SelFrame->cur.line].n &&
                (ch == '}' || ch == ':')) {
            (void) indent_line(buf, SelFrame->cur.line);
            SelFrame->cur.col = buf->lines[SelFrame->cur.line].n;
        }
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;
    }
    return do_motion(SelFrame, c);
}
