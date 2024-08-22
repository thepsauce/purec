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
        prev_ev = SelFrame->buf->events[i - 1];
        ev = SelFrame->buf->events[i];
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
    struct raw_line *ev_lines;
    size_t ev_num_lines;
    size_t orig_col;
    size_t repeat;

    buf = SelFrame->buf;

    if ((Core.counter <= 1 && Core.move_down_count == 0) ||
            Core.ev_from_ins == buf->event_i) {
        return;
    }

    /* check if there is only a SINGLE transient chain */
    for (size_t i = Core.ev_from_ins + 1; i < buf->event_i; i++) {
        ev = buf->events[i - 1];
        if (!(ev->flags & IS_TRANSIENT)) {
            return;
        }
    }

    /* combine the events (we only expect insertion and deletion events) */
    cur = buf->events[Core.ev_from_ins]->pos;
    for (size_t i = Core.ev_from_ins; i < buf->event_i; i++) {
        ev = buf->events[i];
        ev_lines = load_undo_data(ev->data_i, &ev_num_lines);
        if ((ev->flags & IS_INSERTION)) {
            /* join the current and the event text */
            if (num_lines == 0) {
                lines = xreallocarray(NULL, ev_num_lines, sizeof(*lines));
                num_lines = ev_num_lines;
                for (size_t i = 0; i < num_lines; i++) {
                    init_raw_line(&lines[i], ev_lines[i].s,
                            ev_lines[i].n);
                }
                unload_undo_data(ev->data_i);
                continue;
            }
            lines = xreallocarray(lines, num_lines + ev_num_lines - 1,
                    sizeof(*lines));
            if (is_point_equal(&cur, &ev->pos)) {
                line = &lines[0];
                line->s = xrealloc(line->s, line->n + ev_lines[0].n);
                memmove(&line->s[ev_lines[ev_num_lines - 1].n],
                        &line->s[0], line->n);
                memcpy(&line->s[0], &ev_lines[ev_num_lines - 1].s[0],
                        ev_lines[ev_num_lines - 1].n);
                line->n += ev_lines[ev_num_lines - 1].n;

                memmove(&lines[ev_num_lines - 1], &lines[0],
                        sizeof(*lines) * num_lines);
                for (size_t i = 1; i < ev_num_lines; i++) {
                    init_raw_line(&lines[i - 1], ev_lines[i].s,
                            ev_lines[i].n);
                }
            } else {
                line = &lines[num_lines - 1];

                line->s = xrealloc(line->s, line->n + ev_lines[0].n);
                memcpy(&line->s[line->n], &ev_lines[0].s[0],
                        ev_lines[0].n);
                line->n += ev_lines[0].n;

                for (size_t i = 0; i < ev_num_lines - 1; i++) {
                    line++;
                    init_raw_line(line, ev_lines[i].s, ev_lines[i].n);
                }
            }
            num_lines += ev_num_lines - 1;
        } else {
            line = &lines[ev->pos.line - cur.line];
            line->n -= ev_lines[0].n;
            if (ev_num_lines == 1) {
                memmove(&line->s[ev->pos.col],
                        &line->s[ev->pos.col + ev_lines[0].n],
                        line->n - ev->pos.col);
            } else {
                line->s = xrealloc(line->s, line->n + line[ev_num_lines - 1].n);
                memcpy(&lines->s[line->n], &line[ev_num_lines - 1].s[0],
                        line[ev_num_lines - 1].n);
                line->n += line[ev_num_lines - 1].n -
                    ev_lines[ev_num_lines - 1].n;
                num_lines -= ev_num_lines - 1;
                memmove(&line[1], &line[ev_num_lines], sizeof(*line) *
                        (num_lines - 1 - (lines - line)));
            }
        }
        unload_undo_data(ev->data_i);
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

    buf->events[buf->event_i - 1]->flags |= IS_TRANSIENT;
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
    static int motions[KEY_MAX] = {
        [KEY_LEFT] = MOTION_LEFT,
        [KEY_RIGHT] = MOTION_RIGHT,
        [KEY_UP] = MOTION_UP,
        [KEY_DOWN] = MOTION_DOWN,

        [KEY_HOME] = MOTION_HOME,
        [KEY_END] = MOTION_END,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,
    };

    int r = 0;
    struct buf *buf;
    char ch;
    struct mark *mark;
    struct undo_event *ev;
    size_t n;
    struct pos old_cur;
    struct raw_line lines[2];

    buf = SelFrame->buf;
    ch = c;
    switch (c) {
    case '\x1b':
        attempt_join();
        repeat_last_insertion();
        mark = &Core.marks['^' - MARK_MIN];
        mark->buf = buf;
        mark->pos = SelFrame->cur;
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case CONTROL('C'):
        attempt_join();
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    case '\n':
        ev = break_line(buf, &SelFrame->cur);
        ev->cur = SelFrame->cur;
        set_cursor(SelFrame, &ev->end);
        return UPDATE_UI;

    case '\t':
        ch = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, n);
        ev->cur = SelFrame->cur;
        (void) move_dir(SelFrame, n, 1);
        return UPDATE_UI;

    case KEY_DC:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, 1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->cur = old_cur;
        }
        SelFrame->cur = old_cur;
        return r;

    case 0x7f:
    case KEY_BACKSPACE:
    case '\b':
        /* TODO: add handling for indentation deletion */
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, -1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->cur = old_cur;
        }
        return r;
    }

    if (c < 0x100 && (ch >= ' ' || ch < 0) && motions[c] == 0) {
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, 1);
        ev->cur = SelFrame->cur;
        (void) move_dir(SelFrame, 1, 1);
        return UPDATE_UI;
    }
    return do_motion(SelFrame, motions[c]);
}
