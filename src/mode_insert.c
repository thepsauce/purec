#include "xalloc.h"
#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <string.h>
#include <ncurses.h>

static void attempt_join(void)
{
    struct undo_event *prev_ev, *ev;

    if (SelFrame->buf->num_events <= Mode.ev_from_ins + 1) {
        return;
    }

    prev_ev = SelFrame->buf->events[SelFrame->buf->num_events - 2];
    ev = SelFrame->buf->events[SelFrame->buf->num_events - 1];
    if (should_join(prev_ev, ev)) {
        prev_ev->flags |= IS_TRANSIENT;
    }
}

static void repeat_last_insertion(void)
{
    struct buf *buf;
    struct pos cur;
    struct undo_event new_ev, *ev;
    struct raw_line *lines = NULL, *line;
    size_t num_lines = 0;
    size_t orig_col;

    buf = SelFrame->buf;

    if ((Mode.repeat_count <= 1 && Mode.num_dup <= 1) ||
            Mode.ev_from_ins == buf->event_i) {
        return;
    }

    /* check if there is only a SINGLE transient chain */
    for (size_t i = Mode.ev_from_ins + 1; i < buf->event_i; i++) {
        ev = buf->events[i - 1];
        if (!(ev->flags & IS_TRANSIENT)) {
            return;
        }
    }

    /* combine the events (we only expect insertion and deletion events) */
    cur = buf->events[Mode.ev_from_ins]->pos;
    for (size_t i = Mode.ev_from_ins; i < buf->event_i; i++) {
        ev = buf->events[i];
        if ((ev->flags & IS_INSERTION)) {
            /* join the current and the event text */
            if (num_lines == 0) {
                lines = xreallocarray(NULL, ev->num_lines, sizeof(*lines));
                num_lines = ev->num_lines;
                for (size_t i = 0; i < num_lines; i++) {
                    init_raw_line(&lines[i], ev->lines[i].s,
                            ev->lines[i].n);
                }
                continue;
            }
            lines = xreallocarray(lines, num_lines + ev->num_lines - 1,
                    sizeof(*lines));
            if (is_point_equal(&cur, &ev->pos)) {
                line = &lines[0];
                line->s = xrealloc(line->s, line->n + ev->lines[0].n);
                memmove(&line->s[ev->lines[ev->num_lines - 1].n],
                        &line->s[0], line->n);
                memcpy(&line->s[0], &ev->lines[ev->num_lines - 1].s[0],
                        ev->lines[ev->num_lines - 1].n);
                line->n += ev->lines[ev->num_lines - 1].n;

                memmove(&lines[ev->num_lines - 1], &lines[0],
                        sizeof(*lines) * num_lines);
                for (size_t i = 1; i < ev->num_lines; i++) {
                    init_raw_line(&lines[i - 1], ev->lines[i].s,
                            ev->lines[i].n);
                }
            } else {
                line = &lines[num_lines - 1];

                line->s = xrealloc(line->s, line->n + ev->lines[0].n);
                memcpy(&line->s[line->n], &ev->lines[0].s[0],
                        ev->lines[0].n);
                line->n += ev->lines[0].n;

                for (size_t i = 0; i < ev->num_lines - 1; i++) {
                    line++;
                    init_raw_line(line, ev->lines[i].s, ev->lines[i].n);
                }
            }
            num_lines += ev->num_lines - 1;
        } else {
            line = &lines[ev->pos.line - cur.line];
            line->n -= ev->lines[0].n;
            if (ev->num_lines == 1) {
                memmove(&line->s[ev->pos.col],
                        &line->s[ev->pos.col + ev->lines[0].n],
                        line->n - ev->pos.col);
            } else {
                line->s = xrealloc(line->s, line->n + line[ev->num_lines - 1].n);
                memcpy(&lines->s[line->n], &line[ev->num_lines - 1].s[0],
                        line[ev->num_lines - 1].n);
                line->n += line[ev->num_lines - 1].n -
                    ev->lines[ev->num_lines - 1].n;
                num_lines -= ev->num_lines - 1;
                memmove(&line[1], &line[ev->num_lines], sizeof(*line) *
                        (num_lines - 1 - (lines - line)));
            }
        }
    }

    new_ev.flags = IS_INSERTION;
    new_ev.pos = cur;
    new_ev.lines = lines;
    new_ev.num_lines = num_lines;

    orig_col = cur.col;

    get_end_pos(&new_ev, &cur);
    if (!is_point_equal(&SelFrame->cur, &cur) ||
            (num_lines == 1 && lines[0].n == 0)) {
        goto end;
    }

    buf->events[buf->event_i - 1]->flags |= IS_TRANSIENT;

    for (size_t i = 1; i < Mode.repeat_count; i++) {
        new_ev.pos = cur;
        ev = perform_event(buf, &new_ev);
        ev->flags |= IS_TRANSIENT;
        get_end_pos(&new_ev, &cur);
    }

    for (size_t i = 1; i < Mode.num_dup; i++) {
        cur.line++;
        if (orig_col > buf->lines[cur.line].n) {
            continue;
        }
        cur.col = orig_col;
        for (size_t j = 0; j < Mode.repeat_count; j++) {
            new_ev.pos = cur;
            ev = perform_event(buf, &new_ev);
            ev->flags |= IS_TRANSIENT;
            get_end_pos(&new_ev, &cur);
        }
    }

    buf->events[buf->event_i - 1]->flags ^= IS_TRANSIENT;

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
    struct undo_event *ev;
    char ch;
    size_t n;
    struct pos old_cur;
    struct raw_line lines[2];

    buf = SelFrame->buf;
    ch = c;
    switch (c) {
    case '\x1b':
        repeat_last_insertion();
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        return 1;

    case '\n':
        /* TODO: make line break function */
        lines[0].n = 0;
        lines[1].n = 0;
        ev = insert_lines(buf, &SelFrame->cur, lines, 2, 1);
        ev->undo_cur = SelFrame->cur;
        SelFrame->cur.line++;
        SelFrame->cur.col = 0;
        set_cursor(SelFrame, &SelFrame->cur);
        attempt_join();
        return 1;

    case '\t':
        ch = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, n);
        ev->undo_cur = SelFrame->cur;
        Mode.counter = n;
        (void) do_motion(SelFrame, MOTION_NEXT);
        ev->redo_cur = SelFrame->cur;
        attempt_join();
        return 1;

    case KEY_DC:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, 1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = old_cur;
            ev->redo_cur = old_cur;
            attempt_join();
        }
        SelFrame->cur = old_cur;
        return r;

    case 0x7f:
    case KEY_BACKSPACE:
        /* TODO: add handling for indentation deletion */
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, -1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = old_cur;
            ev->redo_cur = SelFrame->cur;
            attempt_join();
        }
        return r;
    }
    if (c < 0x100 && ch >= ' ' && motions[c] == 0) {
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, 1);
        ev->undo_cur = SelFrame->cur;
        (void) do_motion(SelFrame, MOTION_NEXT);
        ev->redo_cur = SelFrame->cur;
        attempt_join();
        return 1;
    }
    return do_motion(SelFrame, motions[c]);
}
