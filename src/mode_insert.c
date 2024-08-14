#include "xalloc.h"
#include "frame.h"
#include "buf.h"
#include "util.h"
#include "purec.h"

#include <ctype.h>
#include <string.h>
#include <ncurses.h>

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
        return 1;

    case '\t':
        ch = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, n);
        ev->undo_cur = SelFrame->cur;
        (void) move_dir(SelFrame, n, 1);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case KEY_DC:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, 1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = old_cur;
            ev->redo_cur = old_cur;
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
        }
        return r;
    }
    if (c < 0x100 && (ch >= ' ' || ch < 0) && motions[c] == 0) {
        lines[0].s = &ch;
        lines[0].n = 1;
        ev = insert_lines(buf, &SelFrame->cur, lines, 1, 1);
        ev->undo_cur = SelFrame->cur;
        (void) move_dir(SelFrame, 1, 1);
        ev->redo_cur = SelFrame->cur;
        return 1;
    }
    return do_motion(SelFrame, motions[c]);
}
