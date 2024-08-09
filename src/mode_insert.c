#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
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
    struct undo_event *ev;
    char ch;
    size_t n;
    struct pos old_cur;
    struct raw_line lines[2];

    ch = c;
    switch (c) {
    case '\x1b':
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        return 1;

    case '\n':
        lines[0].n = 0;
        lines[1].n = 0;
        ev = insert_lines(SelFrame->buf, &SelFrame->cur, lines, 2, 1);
        attempt_join();
        ev->undo_cur = SelFrame->cur;
        (void) do_motion(SelFrame, MOTION_NEXT);
        ev->redo_cur = SelFrame->cur;
        if (indent_line(SelFrame->buf, SelFrame->cur.line) != NULL) {
            ev->flags |= IS_TRANSIENT;
        }
        (void) do_motion(SelFrame, MOTION_END);
        return 1;

    case '\t':
        ch = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(SelFrame->buf, &SelFrame->cur, lines, 1, n);
        ev->undo_cur = SelFrame->cur;
        Mode.counter = n;
        (void) do_motion(SelFrame, MOTION_NEXT);
        ev->redo_cur = SelFrame->cur;
        attempt_join();
        return 1;

    case KEY_DC:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, 1);
        ev = delete_range(SelFrame->buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = old_cur;
            ev->redo_cur = old_cur;
            attempt_join();
        }
        SelFrame->cur = old_cur;
        return r;

    case 0x7f:
    case KEY_BACKSPACE:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, -1);
        ev = delete_range(SelFrame->buf, &old_cur, &SelFrame->cur);
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
        ev = insert_lines(SelFrame->buf, &SelFrame->cur, lines, 1, 1);
        ev->undo_cur = SelFrame->cur;
        (void) do_motion(SelFrame, MOTION_NEXT);
        ev->redo_cur = SelFrame->cur;
        attempt_join();
        return 1;
    }
    return do_motion(SelFrame, motions[c]);
}
