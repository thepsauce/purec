#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
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
    struct undo_event *ev;
    char ch;
    size_t n;
    struct pos old_cur;

    ch = c;
    switch (c) {
    case '\x1b':
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        return 1;

    case '\n':
        ev = insert_text(SelFrame->buf, &SelFrame->cur, &ch, 1, 1);
        ev->cur = SelFrame->cur;
        do_motion(SelFrame, MOTION_NEXT);
        return 1;

    case '\t':
        ch = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        ev = insert_text(SelFrame->buf, &SelFrame->cur, &ch, 1, n);
        ev->cur = SelFrame->cur;
        Mode.counter = n;
        do_motion(SelFrame, MOTION_NEXT);
        return 1;

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
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, -1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->cur = old_cur;
        }
        return r;
    }
    if (c < 0x100 && ch >= ' ' && motions[c] == 0) {
        ev = insert_text(SelFrame->buf, &SelFrame->cur, &ch, 1, 1);
        ev->cur = SelFrame->cur;
        do_motion(SelFrame, MOTION_NEXT);
        return 1;
    }
    return do_motion(SelFrame, motions[c]);
}
